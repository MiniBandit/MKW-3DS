// NW4R snd runtime port (see include/mkw/snd_engine.hpp). Function names in
// comments are the doldecomp/ogws originals; retail DOL differences checked
// with Ghidra are noted inline and in docs/AUDIO_SOURCE.md.
#include <mkw/snd_engine.hpp>
#include <mkw/snd_tables.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>

namespace mkw::snd {
namespace {
inline u16 be16(const u8* p) { return u16((p[0] << 8) | p[1]); }
inline u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3]; }
inline s16 bes16(const u8* p) { return s16(be16(p)); }
inline f32 bef32(const u8* p) { u32 v = be32(p); f32 f; std::memcpy(&f, &v, 4); return f; }
template <class T> T clamp(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }

// ------------------------------------------------------------------ Util
constexpr f32 VolumeMinDb = -90.4f, VolumeMaxDb = 6.0f;
u32 randomState = 0x12345678;
}

u16 random() { randomState = randomState * 0x19660D + 0x3C6EF35F; return u16(randomState >> 16); }
void seedRandom(u32 seed) { randomState = seed; }

f32 pitchRatio(int pitch) {
 int octave = 0;
 while (pitch < 0) { --octave; pitch += 12 * 256; }
 while (pitch >= 12 * 256) { ++octave; pitch -= 12 * 256; }
 f32 octaveFloat = 1.0f;
 while (octave > 0) { octaveFloat *= 2.0f; --octave; }
 while (octave < 0) { octaveFloat /= 2.0f; ++octave; }
 const int note = pitch / 256;
 pitch %= 256;
 f32 ratio = octaveFloat;
 if (note != 0) ratio *= tables::Note[note];
 if (pitch != 0) ratio *= tables::Pitch[pitch];
 return ratio;
}
f32 volumeRatio(f32 db) {
 db = clamp(db, VolumeMinDb, VolumeMaxDb);
 return tables::Decibel2Ratio[904 + int(10 * db)];
}
f32 panRatio(f32 pan, int curve, bool centerZero, bool zeroClamp) {
 pan = (clamp(pan, -1.0f, 1.0f) + 1.0f) / 2.0f;
 const f32* table = curve == 1 ? tables::PanSinCos : curve == 2 ? tables::PanLinear : tables::PanSqrt;
 f32 ratio = table[int(pan * 256 + 0.5f)];
 if (centerZero) ratio /= table[128];
 return zeroClamp ? clamp(ratio, 0.0f, 1.0f) : clamp(ratio, 0.0f, 2.0f);
}
namespace {
// Util::CalcLpfFreq: 32000 * 2^(10 * (scale - 1)); AxVoice bypasses >= 16000.
f32 lpfFrequency(f32 scale) { scale = clamp(scale, 0.0f, 1.0f); return f32(32000 * std::pow(2.0, 10.0 * (scale - 1.0))); }

// ---------------------------------------------------------- MoveValue
// ut::detail::MoveValue<f32,int>: linear move to a target over N updates.
struct MoveValue {
 f32 origin = 0, target = 0; int frame = 0, count = 0;
 void init(f32 v) { origin = target = v; frame = count = 0; }
 void setTarget(f32 t, int frames) { origin = value(); target = t; frame = 0; count = frames; }
 void update() { if (frame < count) ++frame; }
 bool finished() const { return frame >= count; }
 f32 value() const { return finished() ? target : origin + (target - origin) * frame / count; }
};

// --------------------------------------------------------- EnvGenerator
// Retail EnvGenerator (DOL 80091620..80091950): attack, HOLD, decay, sustain,
// release. Attack -> hold ends the update; hold hands its remainder to decay.
struct Envelope {
 enum Status { Attack = 0, Hold = 1, Decay = 2, Sustain = 3, Release = 4 };
 Status status = Attack;
 f32 value = 0, attack = 0, decay = 0, release = 0; int sustain = 127; u16 hold = 0, holdCounter = 0;
 static f32 releaseRate(int r) {
  if (r == 127) return 65535.0f;
  if (r == 126) return 24.0f;
  if (r < 50) return (r * 2 + 1) * 0.0078125f / 5.0f;
  return 60.0f / (126 - r) / 5.0f;
 }
 void setAttack(int a) { attack = tables::Attack[clamp(a, 0, 127)]; }
 void setHold(int h) { const int v = (h + 1) * (h + 1); hold = u16(v / 4); }
 void setDecay(int d) { decay = releaseRate(clamp(d, 0, 127)); }
 void setSustain(int s) { sustain = clamp(s, 0, 127); }
 void setRelease(int r) { release = releaseRate(clamp(r, 0, 127)); }
 void init(f32 db = VolumeMinDb) { setAttack(127); setDecay(127); setSustain(127); setRelease(127); hold = 0; reset(db); }
 void reset(f32 db = VolumeMinDb) { value = db * 10.0f; status = Attack; }
 f32 get() const { return status == Attack && attack == 0.0f ? 0.0f : value / 10.0f; }
 void update(int ms) {
  switch (status) {
  case Attack:
   for (; ms > 0; --ms) {
    value *= attack;
    if (value > -0.03125f) { value = 0; status = Hold; holdCounter = hold; return; }
   }
   return;
  case Hold:
   if (ms < holdCounter) { holdCounter = u16(holdCounter - ms); return; }
   ms -= holdCounter; holdCounter = 0; status = Decay;
   [[fallthrough]];
  case Decay: {
   const f32 target = tables::DecibelSquare[sustain];
   value -= decay * f32(ms);
   if (value >= target) return;
   value = target; status = Sustain; return;
  }
  case Sustain: return;
  case Release: value -= release * f32(ms); return;
  }
 }
};

// ----------------------------------------------------------------- Lfo
struct LfoParam { f32 depth = 0, speed = 6.25f; u32 delay = 0; u8 range = 1; };
struct Lfo {
 LfoParam param; f32 counter = 0; u32 delayCounter = 0;
 void reset() { counter = 0; delayCounter = 0; }
 void update(int ms) {
  if (delayCounter < param.delay) {
   if (delayCounter + ms <= param.delay) { delayCounter += ms; return; }
   ms -= int(param.delay - delayCounter); delayCounter = param.delay;
  }
  counter += param.speed * ms / 1000.0f;
  counter -= int(counter);
 }
 static int sinIdx(int i) {
  if (i < 32) return tables::LfoSin[i];
  if (i < 64) return tables::LfoSin[32 - (i - 32)];
  if (i < 96) return -tables::LfoSin[i - 64];
  return -tables::LfoSin[32 - (i - 96)];
 }
 f32 value() const {
  if (param.depth == 0.0f || delayCounter < param.delay) return 0.0f;
  f32 v = sinIdx(int(4 * (32 * counter))) / f32(32 * 4 - 1);
  return v * param.depth * param.range;
 }
};
}

// ---------------------------------------------------------------- Archive
namespace {
struct Cursor {
 const std::vector<u8>& d; size_t at = 0; bool ok = true;
 template <class T> T get() { T v{}; if (at + sizeof(T) > d.size()) { ok = false; return v; } std::memcpy(&v, d.data() + at, sizeof(T)); at += sizeof(T); return v; }
 template <class T> void array(std::vector<T>& out, u32 n) { if (at + size_t(n) * sizeof(T) > d.size()) { ok = false; return; } out.resize(n); if (n) std::memcpy(out.data(), d.data() + at, size_t(n) * sizeof(T)); at += size_t(n) * sizeof(T); }
};
}
bool Archive::load(std::vector<u8> bytes) {
 Cursor c{bytes};
 if (c.get<u32>() != 0x4153534Du || c.get<u32>() != 3) return false; // "MSSA" v3
 const u32 sounds = c.get<u32>(), files = c.get<u32>(), banks = c.get<u32>(), groups = c.get<u32>(), items = c.get<u32>(), players = c.get<u32>(), strings = c.get<u32>(), bridges = c.get<u32>();
 const u32 waves = c.get<u32>(), pool = c.get<u32>();
 info.seqSounds = c.get<u16>(); info.seqTracks = c.get<u16>(); info.strmSounds = c.get<u16>(); info.strmTracks = c.get<u16>();
 info.strmChannels = c.get<u16>(); info.waveSounds = c.get<u16>(); info.waveTracks = c.get<u16>(); (void)c.get<u16>();
 c.array(sounds_, sounds); c.array(files_, files); c.array(banks_, banks); c.array(groups_, groups);
 c.array(groupItems_, items); c.array(players_, players); c.array(strings_, strings); c.array(bridges_, bridges);
 c.array(waves_, waves); c.array(reach_, sounds); c.array(reachPool_, pool);
 c.at = (c.at + 3) & ~size_t(3);
 if (!c.ok || c.at != bytes.size()) { sounds_.clear(); return false; }
 for (const auto& g : groups_) if (g.firstItem + g.itemCount > groupItems_.size()) { sounds_.clear(); return false; }
 fileWaves_.resize(files_.size());
 u32 at = 0;
 for (size_t i = 0; i < files_.size(); ++i) { fileWaves_[i] = at; at += files_[i].waveCount; }
 if (at != waves_.size()) { sounds_.clear(); return false; }
 for (const auto& r : reach_) if (r.first + r.count > reachPool_.size()) { sounds_.clear(); return false; }
 return true;
}
const WaveEntry* Archive::wave(u32 fileId, u32 index) const {
 return fileId < files_.size() && index < files_[fileId].waveCount ? &waves_[fileWaves_[fileId] + index] : nullptr;
}
WaveReach Archive::reach(u32 soundId) const {
 WaveReach r;
 const SoundEntry* s = sound(soundId);
 if (!s || soundId >= reach_.size() || !reach_[soundId].count) return r;
 r.file = s->type == TypeSeq ? bankFile(s->bankId) : s->type == TypeWave ? s->fileId : InvalidId;
 if (r.file == InvalidId) return r;
 r.waves = reachPool_.data() + reach_[soundId].first; r.count = reach_[soundId].count;
 return r;
}
void Archive::groupFiles(u32 group, std::vector<u32>& out) const {
 out.clear(); if (group >= groups_.size()) return;
 const auto& g = groups_[group];
 out.assign(groupItems_.begin() + g.firstItem, groupItems_.begin() + g.firstItem + g.itemCount);
}
const char* Archive::string(u32 index) const { return index < strings_.size() ? strings_.data() + index : ""; }
const LoopBridge* Archive::bridge(u32 fileId, u32 wave, int channel) const {
 // Sorted by (file, wave, channel) as cooked.
 auto it = std::lower_bound(bridges_.begin(), bridges_.end(), std::tuple(fileId, wave, channel), [](const LoopBridge& b, const std::tuple<u32, u32, int>& k) {
  return std::tuple(b.fileId, u32(b.wave), int(b.channel)) < k; });
 return it != bridges_.end() && it->fileId == fileId && it->wave == wave && it->channel == channel ? &*it : nullptr;
}

// ------------------------------------------------------------ file readers
namespace {
// RWAR TABL entry -> RWAV; WaveFileReader::ReadWaveInfo (sample units).
bool readRwav(const u8* rwav, u32 size, WaveData& w, bool checkData = true) {
 if (size < 0x20 || std::memcmp(rwav, "RWAV", 4)) return false;
 const u32 infoOffset = be32(rwav + 0x10), dataOffset = be32(rwav + 0x18);
 if (infoOffset + 0x24 > size || dataOffset + 8 > size) return false;
 const u8* info = rwav + infoOffset + 8;
 const u8* data = rwav + dataOffset + 8;
 w.format = info[0]; w.loop = info[1]; w.channels = std::min<u8>(info[2], ChannelMax);
 w.rate = (u32(info[3]) << 16) + be16(info + 4);
 const u32 loopStart = be32(info + 8), loopEnd = be32(info + 12);
 if (w.format == 2) { w.loopStart = loopStart / 16 * 14 + (loopStart % 16 - 2); w.loopEnd = loopEnd / 16 * 14 + (loopEnd % 16 - 2) + 1; }
 else { w.loopStart = loopStart; w.loopEnd = loopEnd + 1; }
 const u8* table = info + be32(info + 0x10);
 // WaveInfo.dataOffset (info+0x14) names the DATA block relative to the info;
 // samples follow its 8-byte header (first byte == ADPCM ps in retail data).
 for (int c = 0; c < w.channels; ++c) {
  const u8* ci = info + be32(table + c * 4);
  auto& ch = w.channel[c];
  ch.data = data + be32(ci);
  const u32 adpcm = be32(ci + 4);
  if (adpcm) {
   const u8* a = info + adpcm;
   for (int i = 0; i < 16; ++i) ch.adpcm.coef[i] = bes16(a + i * 2);
   ch.adpcm.ps = be16(a + 34); ch.adpcm.yn1 = bes16(a + 36); ch.adpcm.yn2 = bes16(a + 38);
   ch.loopParam.ps = be16(a + 40); ch.loopParam.yn1 = bes16(a + 42); ch.loopParam.yn2 = bes16(a + 44);
  }
  ch.bytes = w.format == 2 ? (w.loopEnd + 13) / 14 * 8 : w.format == 1 ? w.loopEnd * 2 : w.loopEnd;
  if (checkData && ch.data + ch.bytes > rwav + size) return false;
 }
 return w.channels > 0;
}
const Archive* currentArchive = nullptr;
void attachBridges(const LoadedFile* f, s32 index, WaveData& w) {
 if (currentArchive && w.loop && w.format == 2 && w.loopStart % 14)
  for (int c = 0; c < w.channels; ++c) w.channel[c].bridge = currentArchive->bridge(f->fileId, u32(index), c);
}
bool waveFromFile(const LoadedFile* f, s32 index, WaveData& w) {
 if (!f || index < 0 || u32(index) >= f->waveOffset.size()) return false;
 const u32 at = f->waveOffset[index];
 if (at == InvalidId) {
  for (const auto& st : f->streamed) {
   if (st.index != u32(index)) continue;
   if (!readRwav(st.header.data(), u32(st.header.size()), w, false) || w.format != 2) return false;
   w.streamed = true; w.headSamples = st.headSamples;
   for (int c = 0; c < w.channels; ++c) { w.channel[c].data = st.head[c]; w.channel[c].fileOffset = st.fileOffset[c]; }
   attachBridges(f, index, w);
   return true;
  }
  // On demand: the wave cache's copy, once loaded.
  const WaveEntry* entry = currentArchive ? currentArchive->wave(f->fileId, u32(index)) : nullptr;
  if (u32(index) >= f->cached.size() || !f->cached[index] || !entry || !readRwav(f->cached[index], entry->size, w)) return false;
  attachBridges(f, index, w);
  return true;
 }
 if (!f->waves || at + 0x20 > f->waveBytes) return false;
 if (!readRwav(f->waves + at, f->waveBytes - at, w)) return false;
 attachBridges(f, index, w);
 return true;
}
// Util::GetDataRefAddress on a DataRef { u8 refType, u8 dataType, u16, u32 value } (offset form).
struct DataRef { u8 refType = 0, dataType = 0; u32 value = 0; };
DataRef dataRef(const u8* p) { return DataRef{p[0], p[1], be32(p + 4)}; }

struct InstInfo { s32 waveIndex = -1; u8 attack = 127, hold = 0, decay = 127, sustain = 127, release = 127, originalKey = 60, pan = 64, volume = 127, noteOffType = 0, keyGroup = 0; f32 tune = 1; };
// BankFileReader::ReadInstInfo (RBNK 1.2: InstParam carries an RWAR index).
bool bankInstInfo(const std::vector<u8>& d, int prg, int key, int velocity, InstInfo& out) {
 if (d.size() < 0x20 || std::memcmp(d.data(), "RBNK", 4)) return false;
 const u16 version = be16(d.data() + 6);
 const u8* data = d.data() + be32(d.data() + 0x10);
 const u8* base = data + 8;
 const u32 count = be32(data + 8);
 if (prg < 0 || u32(prg) >= count) return false;
 const u8* ref = data + 12 + prg * 8;
 auto sub = [&](const u8* r, int split) -> const u8* {
  const DataRef dr = dataRef(r);
  if (dr.dataType == 1) return r;
  if (dr.dataType == 2) {
   const u8* t = base + dr.value; const int size = t[0];
   int i = 0;
   while (split > t[1 + i]) if (++i >= size) return nullptr;
   const u32 start = (size + 1 + 3) & ~3u;
   return t + start + i * 8;
  }
  if (dr.dataType == 3) {
   const u8* t = base + dr.value;
   if (split < t[0] || split > t[1]) return nullptr;
   return t + 4 + (split - t[0]) * 8;
  }
  return nullptr;
 };
 DataRef dr = dataRef(ref);
 if (dr.dataType == 0) return false;
 if (dr.dataType != 1) { ref = sub(ref, key); if (!ref) return false; dr = dataRef(ref); }
 if (dr.dataType == 0) return false;
 if (dr.dataType != 1) { ref = sub(ref, velocity); if (!ref) return false; dr = dataRef(ref); }
 if (dr.dataType != 1 || !dr.value) return false;
 const u8* p = base + dr.value;
 if (p + 0x14 > d.data() + d.size()) return false;
 // Retail ReadInstInfo (8008d6a0): +8 hold, +9 wave location type (0 = RWAR index),
 // +A note-off type (1 = ignore), +B key group.
 if (p[9] != 0) return false;
 out.waveIndex = s32(be32(p)); out.attack = p[4]; out.decay = p[5]; out.sustain = p[6]; out.release = p[7];
 out.hold = p[8]; out.noteOffType = p[0xA]; out.keyGroup = p[0xB];
 if (out.noteOffType > 1) return false;
 out.originalKey = p[0xC];
 out.pan = p[0xE];
 if (version >= 0x0101) { out.volume = p[0xD]; out.tune = bef32(p + 0x10); } else { out.volume = 127; out.tune = 1; }
 return out.waveIndex >= 0;
}
struct WsdSoundInfo { f32 pitch = 1; u8 pan = 64, surroundPan = 0, fxSendA = 0, fxSendB = 0, fxSendC = 0, mainSend = 127; };
struct WsdNoteInfo { s32 waveIndex = -1; u8 attack = 127, hold = 0, decay = 127, sustain = 127, release = 127, originalKey = 60, pan = 64, surroundPan = 0, volume = 127; f32 pitch = 1; };
// WsdFileReader::ReadWaveSoundInfo / ReadWaveSoundNoteInfo (RWSD 1.3).
bool wsdInfo(const std::vector<u8>& d, int index, int note, WsdSoundInfo& info, WsdNoteInfo& n) {
 if (d.size() < 0x20 || std::memcmp(d.data(), "RWSD", 4)) return false;
 const u16 version = be16(d.data() + 6);
 const u8* data = d.data() + be32(d.data() + 0x10);
 const u8* base = data + 8;
 const u32 count = be32(data + 8);
 if (index < 0 || u32(index) >= count) return false;
 const u8* wsd = base + dataRef(data + 12 + index * 8).value;
 const u8* wi = base + dataRef(wsd).value;
 if (version >= 0x0102) { info.pitch = bef32(wi); info.pan = wi[4]; info.surroundPan = wi[5]; info.fxSendA = wi[6]; info.fxSendB = wi[7]; info.fxSendC = wi[8]; info.mainSend = wi[9]; }
 else if (version == 0x0101) { info.pitch = bef32(wi); info.pan = wi[4]; info.surroundPan = wi[5]; }
 const u8* notes = base + dataRef(wsd + 16).value;
 if (u32(note) >= be32(notes)) return false;
 const u8* ni = base + dataRef(notes + 4 + note * 8).value;
 // Retail ReadWaveSoundNoteInfo (800adac0): hold is the byte at +8.
 n.waveIndex = s32(be32(ni)); n.attack = ni[4]; n.hold = ni[8]; n.decay = ni[5]; n.sustain = ni[6]; n.release = ni[7];
 n.originalKey = ni[0xC]; n.volume = ni[0xD];
 if (version >= 0x0101) { n.pan = ni[0xE]; n.surroundPan = ni[0xF]; n.pitch = bef32(ni + 0x10); }
 return true;
}
}

// ======================================================================
// Engine implementation
// ======================================================================
namespace {
struct Channel;
struct Voice;

// --------------------------------------------------------------- Voice
// detail::Voice with one voice-out (single listener). Owns 1-2 hardware voices.
struct Voice {
 enum Callback { FinishWave, Cancel, DropVoice, DropDsp };
 bool active = false, starting = false, started = false, paused = false;
 int channels = 0; int hw[ChannelMax] = {-1, -1};
 int priority = 0; u32 order = 0;
 f32 volume = 1, veInit = 0, veTarget = 1, lpf = 1, pan = 0, surroundPan = 0, mainOutVolume = 1, mainSend = 1, pitch = 1;
 f32 fxSend[3]{};
 u8 panMode = PanDual, panCurve = CurveSqrt;
 WaveData wave; u32 offset = 0; bool stream = false; StreamFeed* feed = nullptr; int streamChannel[ChannelMax] = {0, 1}; u32 rate = 32000;
 Channel* owner = nullptr; void* streamOwner = nullptr;
};

// ------------------------------------------------------------- Channel
struct Channel {
 bool used = false, active = false, pause = false, autoSweep = true, releaseFix = false;
 s32 length = 0; int key = 60, originalKey = 60;
 f32 initVolume = 1, initPan = 0, initSurroundPan = 0, tune = 1;
 f32 userVolume = 1, userPitch = 0, userPitchRatio = 1, userPan = 0, userSurroundPan = 0, userLpf = 0;
 f32 mainOutVolume = 1, mainSend = 0, fxSend[3]{};
 MoveValue silence; // 0..255
 f32 sweepPitch = 0; int sweepLength = 0, sweepCounter = 0;
 Envelope envelope; Lfo lfo; u8 lfoTarget = 0; // 0 pitch, 1 volume, 2 pan
 u8 panMode = PanDual, panCurve = CurveSqrt;
 Voice* voice = nullptr;
 void (*callback)(Channel*, int status, void* arg) = nullptr; void* callbackArg = nullptr;
 Channel* nextTrackChannel = nullptr;
 int keyGroup = 0; bool ignoreNoteOff = false; // RBNK 1.2 InstParam +0xB / +0xA (retail Bank::NoteOn 8008d300)
 enum Status { Stopped = 0, Drop = 1, Finish = 2, Cancel = 3 };
 f32 sweepValue() const {
  if (sweepPitch == 0.0f || sweepCounter >= sweepLength) return 0.0f;
  return sweepPitch * (sweepLength - sweepCounter) / sweepLength;
 }
};

// ---------------------------------------------------------- BasicPlayer
struct PlayerParams {
 f32 volume = 1, pitch = 1, pan = 0, surroundPan = 0, lpf = 0, mainOutVolume = 1, mainSend = 0, panRange = 1;
 f32 fxSend[3]{};
 u8 panMode = PanDual, panCurve = CurveSqrt; u32 id = InvalidId;
};

// ----------------------------------------------------------- Sequencer
constexpr int TrackMax = 16, CallStack = 3;
struct SeqPlayer;
struct SeqTrack {
 SeqPlayer* player = nullptr; int no = 0; bool open = false;
 // ParserTrackParam
 u32 base = 0, pc = 0; bool hasPc = false;
 int bank = 0, prg = 0; int priority = 64; s32 wait = 0;
 bool mute = false, silence = false, noteFinishWait = false, portaFlag = false, damper = false;
 // Retail MoveParam ramps (MML 0xA3-0xA5 time prefix, DOL 80096974/800969e8/80096a50):
 // current = start + counter*(target-start)/length, advanced once per tick.
 struct Ramp { int start = 0, target = 0, length = 0, counter = 0;
  int value() const { return counter < length ? start + counter * (target - start) / length : target; }
  void set(int t, int len) { start = value(); target = t; length = len; counter = 0; }
  void tick() { if (counter < length) ++counter; } };
 Ramp volumeRamp, panRamp, surroundRamp;
 int volume2 = 127, initPan = 0, pitchBend = 0;
 int attack = 255, decay = 255, sustain = 255, release = 255, hold = 255, mainSend = 127, fxSend[3]{}, lpfFreq = 64, bendRange = 2;
 bool monophonic = false;
 int portaKey = 60, portaTime = 0; f32 sweepPitch = 0; int transpose = 0;
 LfoParam lfo; int lfoTarget = 0;
 s16 vars[16];
 // MmlParserParam
 bool noteWait = true, tie = false, cmp = true; int depth = 0;
 struct Frame { bool loop = false; u8 count = 0; u32 address = 0; } stack[CallStack];
 // Ext (SetVolume/SetPitch from SeqSoundHandle)
 f32 extVolume = 1, extPitch = 1, extPan = 0, extSurroundPan = 0, panRangeT = 1, extLpf = 0, extMainSend = 0, extFxSend[3]{};
 MoveValue extVolumeMove; bool extVolumeMoving = false;
 Channel* channels = nullptr;
 void init() {
  open = false; hasPc = false; base = pc = 0; bank = prg = 0; priority = 64; wait = 0;
  mute = silence = noteFinishWait = portaFlag = damper = false;
  volumeRamp = {127, 127, 0, 0}; panRamp = {}; surroundRamp = {}; volume2 = 127; initPan = pitchBend = 0;
  attack = decay = sustain = release = hold = 255; mainSend = 127; fxSend[0] = fxSend[1] = fxSend[2] = 0; lpfFreq = 64; bendRange = 2; monophonic = false;
  portaKey = 60; portaTime = 0; sweepPitch = 0; transpose = 0; lfo = {}; lfoTarget = 0;
  for (auto& v : vars) v = -1;
  noteWait = true; tie = false; cmp = true; depth = 0;
  extVolume = extPitch = 1; extPan = extSurroundPan = extLpf = extMainSend = 0; panRangeT = 1; extFxSend[0] = extFxSend[1] = extFxSend[2] = 0;
  extVolumeMove.init(1); extVolumeMoving = false; channels = nullptr;
 }
};
}

struct Engine::Impl {
 const Archive* archive = nullptr;
 Hardware* hw = nullptr;
 EngineStats stats;
 f32 masterVolume = 1;
 std::vector<LoadedFile*> files; // indexed by file id
 std::unique_ptr<StreamFeed> (*openStream)(void*, const char*, u32) = nullptr;
 void* openStreamCtx = nullptr;
 WaveResidency* residency = nullptr;

 // ---------------- voices
 Voice voices[VoiceMax];
 int hwOwner[VoiceMax];            // voice index or -1
 bool hwReleasedPriority[VoiceMax]{};
 u32 voiceOrder = 0;
 int hwVoices = VoiceMax;
 Channel channels[VoiceMax + 8];

 Impl() { for (auto& o : hwOwner) o = -1; }

 // VoiceManager list order: ascending priority, oldest first among equals.
 bool voiceLess(const Voice& a, const Voice& b) const { return a.priority != b.priority ? a.priority < b.priority : a.order < b.order; }
 Voice* lowestVoice(Voice* except = nullptr) {
  Voice* best = nullptr;
  for (auto& v : voices) if (v.active && &v != except && (!best || voiceLess(v, *best))) best = &v;
  return best;
 }
 int freeHw() const { for (int i = 0; i < hwVoices; ++i) if (hwOwner[i] == -1) return i; return -1; }

 void voiceStopHw(Voice& v) {
  for (int c = 0; c < v.channels; ++c) if (v.hw[c] >= 0) { if (v.started) hw->stop(v.hw[c]); }
  v.started = false; v.starting = false; v.paused = false;
 }
 void voiceFreeHw(Voice& v) {
  for (int c = 0; c < ChannelMax; ++c) if (v.hw[c] >= 0) { hwOwner[v.hw[c]] = -1; v.hw[c] = -1; }
  v.channels = 0;
 }
 // Voice::Free + VoiceManager::FreeVoice
 void voiceFree(Voice& v) { if (!v.active) return; voiceFreeHw(v); v.active = false; v.owner = nullptr; v.streamOwner = nullptr; v.feed = nullptr; v.stream = false; }
 void voiceCallback(Voice& v, int status);
 void streamVoiceLost(Voice& v);
 LoadedFile* fileData(u32 id) const;

 // AxVoiceManager-style steal of one hardware voice for `priority`.
 int stealHw(int priority, Voice* requester) {
  // AX first drops voices already in release (AX priority MAX/2), then the
  // lowest NW4R priority, oldest first. Streams (255) are never taken.
  Voice* victim = nullptr;
  for (auto& v : voices) if (v.active && &v != requester && v.priority == VoicePriorityRelease && v.channels > 0 && (!victim || v.order < victim->order)) victim = &v;
  if (!victim) {
   for (auto& v : voices) if (v.active && &v != requester && v.channels > 0 && v.priority < VoicePriorityMax && v.priority <= priority && (!victim || voiceLess(v, *victim))) victim = &v;
  }
  if (!victim) return -1;
  ++stats.voicesDropped;
  voiceStopHw(*victim);
  voiceFreeHw(*victim);
  voiceCallback(*victim, Voice::DropDsp);
  return freeHw();
 }

 // VoiceManager::AllocVoice + Voice::Acquire.
 Voice* allocVoice(int channelCount, int priority, Channel* owner) {
  channelCount = clamp(channelCount, 1, ChannelMax);
  Voice* slot = nullptr;
  for (auto& v : voices) if (!v.active) { slot = &v; break; }
  if (!slot) {
   // DropLowestPriorityVoice
   Voice* low = lowestVoice();
   if (!low || low->priority > priority) return nullptr;
   ++stats.voicesDropped;
   voiceStopHw(*low); voiceFreeHw(*low); low->active = false;
   voiceCallback(*low, Voice::DropVoice);
   slot = low;
  }
  int got[ChannelMax] = {-1, -1};
  for (int i = 0; i < channelCount; ++i) {
   int h = freeHw();
   if (h < 0) {
    int rest = channelCount - i;
    // Enough lower-or-equal priority voices to take from?
    std::vector<Voice*> order;
    for (auto& v : voices) if (v.active && v.channels > 0) order.push_back(&v);
    std::sort(order.begin(), order.end(), [&](Voice* a, Voice* b) { return voiceLess(*a, *b); });
    for (Voice* v : order) { if (priority < v->priority) break; rest -= v->channels; if (rest <= 0) break; }
    if (rest > 0) { for (int j = 0; j < i; ++j) hwOwner[got[j]] = -1; return nullptr; }
    h = stealHw(priority, slot);
    if (h < 0) { for (int j = 0; j < i; ++j) hwOwner[got[j]] = -1; return nullptr; }
   }
   got[i] = h; hwOwner[h] = int(slot - voices);
  }
  Voice& v = *slot;
  v = Voice{};
  v.active = true; v.channels = channelCount; v.priority = priority & VoicePriorityMax; v.order = ++voiceOrder; v.owner = owner;
  for (int i = 0; i < channelCount; ++i) { v.hw[i] = got[i]; hwOwner[got[i]] = int(slot - voices); }
  ++stats.voicesActive;
  return &v;
 }
 void voiceSetPriority(Voice& v, int priority) { v.priority = priority; v.order = ++voiceOrder; }

 // Voice::CalcMixParam (stereo output, single voice-out) -> hardware gains.
 void voiceCommit(Voice& v) {
  if (!v.active || !v.starting) return;
  PanCurveInfo info = panInfo(v.panCurve);
  const f32 baseVolume = v.volume * masterVolume * v.veTarget;
  for (int c = 0; c < v.channels; ++c) {
   if (v.hw[c] < 0) continue;
   f32 left, right;
   if (v.channels > 1 && v.panMode == PanBalance) {
    if (c == 0) { left = panRatio(v.pan, info.curve, info.centerZero, info.zeroClamp); right = 0; }
    else { left = 0; right = panRatio(-v.pan, info.curve, info.centerZero, info.zeroClamp); }
   } else {
    f32 voicePan = 0;
    if (v.channels == 2) voicePan = c == 0 ? -1.0f : 1.0f;
    const f32 pan = v.pan + voicePan;
    left = panRatio(pan, info.curve, info.centerZero, info.zeroClamp);
    right = panRatio(-pan, info.curve, info.centerZero, info.zeroClamp);
   }
   const f32 main = v.mainOutVolume * v.mainSend;
   // AX mix volume u16 (0x8000 == 1.0, clamped at 0xFFFF).
   auto mix = [](f32 x) { return std::min(x, 65535.0f / 32768.0f); };
   const f32 l = mix(main * left) * baseVolume, r = mix(main * right) * baseVolume;
   const f32 freq = lpfFrequency(v.lpf);
   hw->set(v.hw[c], f32(v.stream ? v.rate : v.wave.rate) * v.pitch, l, r, freq >= 16000 ? 0.0f : freq);
  }
 }
 struct PanCurveInfo { int curve = 0; bool centerZero = false, zeroClamp = false; };
 static PanCurveInfo panInfo(u8 c) {
  switch (c) {
  case CurveSqrt0Db: return {0, true, false}; case CurveSqrt0DbClamp: return {0, true, true};
  case CurveSinCos: return {1, false, false}; case CurveSinCos0Db: return {1, true, false}; case CurveSinCos0DbClamp: return {1, true, true};
  case CurveLinear: return {2, false, false}; case CurveLinear0Db: return {2, true, false}; case CurveLinear0DbClamp: return {2, true, true};
  default: return {0, false, false};
  }
 }
 // Voice::Update start path (SYNC_AX_SRC_INITIAL) and pause handling.
 void voiceStartIfNeeded(Voice& v) {
  if (!v.active || !v.starting || v.started) return;
  for (int c = 0; c < v.channels; ++c) {
   if (v.hw[c] < 0) continue;
   if (v.stream) hw->startStream(v.hw[c], v.feed, v.streamChannel[c]);
   else hw->start(v.hw[c], v.wave, c, v.offset);
  }
  v.started = true;
 }

 // -------------- channels (ChannelManager)
 Channel* allocChannel(int channelCount, int priority, void (*cb)(Channel*, int, void*), void* arg) {
  Channel* ch = nullptr;
  for (auto& c : channels) if (!c.used) { ch = &c; break; }
  if (!ch) return nullptr;
  ch->used = true;
  Voice* v = allocVoice(channelCount, priority, ch);
  if (!v) { ch->used = false; return nullptr; }
  channelInit(*ch, cb, arg);
  ch->voice = v;
  return ch;
 }
 static void channelInit(Channel& c, void (*cb)(Channel*, int, void*), void* arg) {
  c.nextTrackChannel = nullptr; c.callback = cb; c.callbackArg = arg;
  c.pause = false; c.autoSweep = true; c.releaseFix = false; c.length = 0; c.key = 60; c.originalKey = 60;
  c.initVolume = 1; c.initPan = 0; c.initSurroundPan = 0; c.tune = 1;
  c.userVolume = 1; c.userPitch = 0; c.userPitchRatio = 1; c.userPan = 0; c.userSurroundPan = 0; c.userLpf = 0;
  c.mainOutVolume = 1; c.mainSend = 0; c.fxSend[0] = c.fxSend[1] = c.fxSend[2] = 0;
  c.silence.init(255); c.sweepPitch = 0; c.sweepLength = 0; c.sweepCounter = 0;
  c.envelope.init(); c.lfo.param = {}; c.lfoTarget = 0; c.panMode = PanDual; c.panCurve = CurveSqrt;
  c.keyGroup = 0; c.ignoreNoteOff = false;
 }
 // Channel::NoteOff (retail 80090600): a length expiry respects "ignore note off".
 void channelNoteOff(Channel& c) { if (!c.ignoreNoteOff) channelRelease(c); }
 void channelFreeSlot(Channel& c) { c.used = false; c.active = false; c.voice = nullptr; }
 // Channel::Start
 void channelStart(Channel& c, const WaveData& wave, s32 length, u32 offset) {
  c.length = length; c.lfo.reset(); c.envelope.reset(); c.sweepCounter = 0;
  Voice& v = *c.voice;
  v.wave = wave; v.offset = offset; v.stream = false;
  v.starting = true; v.paused = false;
  c.active = true;
 }
 // Channel::Release
 void channelRelease(Channel& c) {
  if (c.envelope.status != Envelope::Release) {
   if (c.voice && !c.releaseFix) voiceSetPriority(*c.voice, VoicePriorityRelease);
   c.envelope.status = Envelope::Release;
  }
  c.pause = false;
 }
 // Channel::Stop
 void channelStop(Channel& c) {
  if (!c.voice) return;
  Voice* v = c.voice;
  voiceStopHw(*v); voiceFree(*v);
  c.voice = nullptr; c.pause = false; c.active = false;
  auto cb = c.callback; void* arg = c.callbackArg;
  channelFreeSlot(c);
  if (cb) cb(&c, Channel::Stopped, arg);
 }
 // Channel::Update(periodic)
 void channelUpdate(Channel& c, bool periodic) {
  if (!c.active) return;
  if (c.pause) periodic = false;
  const f32 lfoValue = c.lfo.value();
  c.silence.update();
  f32 volume = c.initVolume * c.userVolume * (c.silence.value() / 255.0f);
  f32 veInit = volumeRatio(c.envelope.get());
  if (c.lfoTarget == 1) veInit *= volumeRatio(VolumeMaxDb * lfoValue);
  if (c.envelope.status == Envelope::Release) {
   if (c.callback ? veInit == 0.0f : volume * veInit == 0.0f) { channelStop(c); return; }
  }
  f32 cent = f32(c.key - c.originalKey) + c.sweepValue() + c.userPitch;
  if (c.lfoTarget == 0) cent += lfoValue;
  const f32 pitch = pitchRatio(int(256 * cent)) * c.tune * c.userPitchRatio;
  f32 pan = c.initPan + c.userPan;
  if (c.lfoTarget == 2) pan += lfoValue;
  const f32 surroundPan = c.initSurroundPan + c.userSurroundPan;
  const f32 lpf = 1.0f + c.userLpf;
  if (periodic) {
   if (c.autoSweep) { c.sweepCounter += 3; if (c.sweepCounter > c.sweepLength) c.sweepCounter = c.sweepLength; }
   c.lfo.update(3); c.envelope.update(3);
  }
  const f32 next = c.lfo.value();
  f32 veTarget = volumeRatio(c.envelope.get());
  if (c.lfoTarget == 1) veTarget *= volumeRatio(VolumeMaxDb * next);
  if (Voice* v = c.voice) {
   v->panMode = c.panMode; v->panCurve = c.panCurve;
   v->volume = clamp(volume, 0.0f, 1.0f);
   v->veTarget = clamp(veTarget, 0.0f, 1.0f); v->veInit = clamp(veInit, 0.0f, 1.0f);
   v->pitch = pitch; v->pan = pan; v->surroundPan = surroundPan; v->lpf = clamp(lpf, 0.0f, 1.0f);
   v->mainOutVolume = clamp(c.mainOutVolume, 0.0f, 1.0f); v->mainSend = clamp(c.mainSend + 1.0f, 0.0f, 1.0f);
   for (int i = 0; i < 3; ++i) v->fxSend[i] = clamp(c.fxSend[i], 0.0f, 1.0f);
  }
 }
 void allChannelsUpdate() { for (auto& c : channels) if (c.used && c.active) channelUpdate(c, true); }

 // ------------- sounds & players: defined below
 struct Sounds;
 std::unique_ptr<Sounds> sounds;
 EngineObserver* observer = nullptr; u32 soundFrames = 0;
 void setupSounds();
 void gameFrame(bool advanceHold);
 void soundFrame();
};

// --------------------------------------------------------------------
// Sound objects. One flat pool of SoundBase records (seq, wave or strm).
// --------------------------------------------------------------------
namespace {
constexpr int SoundSlots = 96;
struct SeqPlayer {
 PlayerParams p; bool active = false, started = false, paused = false, releaseFix = false;
 f32 tempoRatio = 1, tickFraction = 0; u32 skipTicks = 0; f32 skipMs = 0; u32 tickCounter = 0;
 int tempo = 120, timebase = 48, volume = 127, priority = 64; int channelPriority = 64;
 s16 local[16];
 SeqTrack* tracks[TrackMax]{};
 const std::vector<u8>* data = nullptr; // RSEQ file
 u32 bankFile = InvalidId; u32 soundId = InvalidId;
 f32 baseTempo() const { return tempoRatio * (timebase * tempo) / 60000.0f; }
};
struct WsdPlayerState {
 PlayerParams p; bool active = false, started = false, paused = false, releaseFix = false, wavePlay = false;
 int channelPriority = 64; u32 fileId = InvalidId; int index = 0; u32 startOffsetMs = 0;
 WsdSoundInfo info; LfoParam lfo; Channel* channel = nullptr;
};
struct StrmPlayerState {
 // detail::StrmPlayer: one Voice per BRSTM track (retail 800a6d40), track
 // volume MoveValues advanced at game frames by StrmSound (800a8800).
 PlayerParams p; bool active = false, started = false, paused = false, prepared = false;
 std::unique_ptr<StreamFeed> feed; u32 trackMask = 1; int tracks = 0;
 struct Track { bool active = false; Voice* voice = nullptr; StreamTrack info; MoveValue volume; } track[8];
};
s16 globalVariables[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
}

namespace {
struct SoundBase {
 bool used = false; u16 generation = 1; u8 type = 0; u32 id = InvalidId; u32 playerId = 0;
 // BasicSound
 bool pauseFlag = false, pauseFade = false, startFlag = false, startedFlag = false, autoStop = false, fadeOut = false;
 int autoStopCounter = 0; u32 updateCounter = 0;
 MoveValue fadeVolume, pauseFadeVolume, extMoveVolume;
 f32 initVolume = 1, extPitch = 1, extPan = 0, extSurroundPan = 0, mainOutVolume = 1;
 Ambient ambient; bool hasAmbient = false;
 int priority = 64; u32 order = 0;
 // Wave residency: reach waves pinned, how many are still loading, frames waited.
 bool pinned = false; u16 wavesPending = 0, waitFrames = 0;
 SeqPlayer seq; WsdPlayerState wsd; StrmPlayerState strm;
 int calcPriority() const { return clamp(priority + ambient.priority, 0, PriorityMax); }
 PlayerParams& params() { return type == TypeSeq ? seq.p : type == TypeWave ? wsd.p : strm.p; }
 bool playerActive() const { return type == TypeSeq ? seq.active : type == TypeWave ? wsd.active : strm.active; }
 bool playerStarted() const { return type == TypeSeq ? seq.started : type == TypeWave ? wsd.started : strm.started; }
 bool playerPaused() const { return type == TypeSeq ? seq.paused : type == TypeWave ? wsd.paused : strm.paused; }
};
}

struct Engine::Impl::Sounds {
 Impl& e;
 SoundBase sound[SoundSlots];
 std::vector<SeqTrack> trackPool;
 std::vector<SeqTrack*> freeTracks;
 u32 order = 0;
 int seqLimit = 64, strmLimit = 2, waveLimit = 6;
 struct Player { f32 volume = 1; int playable = 1; };
 std::vector<Player> players;
 explicit Sounds(Impl& impl) : e(impl) {}

 SoundBase* get(Handle h) {
  if (h.slot >= SoundSlots) return nullptr;
  SoundBase& s = sound[h.slot];
  return s.used && s.generation == h.generation ? &s : nullptr;
 }
 int countType(u8 type) const { int n = 0; for (const auto& s : sound) n += s.used && s.type == type; return n; }
 int countPlayer(u32 player) const { int n = 0; for (const auto& s : sound) n += s.used && s.playerId == player; return n; }
 // Lowest current priority, oldest first (priority lists insert after equals).
 SoundBase* lowest(bool (*pred)(const SoundBase&, u32), u32 arg) {
  SoundBase* best = nullptr;
  for (auto& s : sound) if (s.used && pred(s, arg) && (!best || s.calcPriority() < best->calcPriority() || (s.calcPriority() == best->calcPriority() && s.order < best->order))) best = &s;
  return best;
 }

 // ------------------------------------------------ BasicSound::Shutdown
 void shutdown(SoundBase& s) {
  if (!s.used) return;
  if (s.playerActive()) {
   if (s.fadeOut) s.params().volume = 0;
   stopPlayer(s);
  }
  if (s.pinned && e.residency) {
   const WaveReach r = e.archive->reach(s.id);
   for (u32 i = 0; i < r.count; ++i) e.residency->release(r.file, r.waves[i]);
  }
  s.pinned = false; s.wavesPending = 0;
  s.used = false; ++s.generation; if (!s.generation) s.generation = 1;
  s.id = InvalidId;
 }
 // BasicSound::Stop(frames)
 void stop(SoundBase& s, int frames) {
  if (frames == 0 || !s.playerActive() || !s.playerStarted() || s.playerPaused()) { shutdown(s); return; }
  const int t = int(frames * s.fadeVolume.value());
  s.fadeVolume.setTarget(0.0f, t);
  s.priority = 0; s.autoStop = false; s.pauseFlag = false; s.pauseFade = false; s.fadeOut = true;
 }
 void pause(SoundBase& s, bool flag, int frames) {
  if (flag) {
   int t = int(frames * s.pauseFadeVolume.value()); s.pauseFade = true; if (t <= 0) t = 1;
   s.pauseFadeVolume.setTarget(0.0f, t);
  } else {
   if (s.pauseFlag != flag) playerPause(s, false);
   int t = int(frames * (1.0f - s.pauseFadeVolume.value())); s.pauseFade = true; if (t <= 0) t = 1;
   s.pauseFadeVolume.setTarget(1.0f, t);
  }
  s.pauseFlag = flag;
 }

 // ------------------------------------------------ allocation
 // SoundPlayer::CheckPlayableSoundCount + SoundInstanceManager::Alloc
 SoundBase* alloc(u8 type, u32 playerId, int priority, int startPriority) {
  startPriority = clamp(startPriority, 0, PriorityMax);
  if (playerId >= players.size()) return nullptr;
  const int playable = players[playerId].playable;
  if (playable == 0) return nullptr;
  while (countPlayer(playerId) >= playable) {
   SoundBase* drop = lowest([](const SoundBase& s, u32 p) { return s.playerId == p; }, playerId);
   if (!drop || startPriority < drop->calcPriority()) return nullptr;
   shutdown(*drop);
  }
  const int limit = type == TypeSeq ? seqLimit : type == TypeStrm ? strmLimit : waveLimit;
  while (countType(type) >= limit) {
   SoundBase* drop = lowest([](const SoundBase& s, u32 t) { return s.type == t; }, type);
   if (!drop || startPriority < drop->calcPriority()) return nullptr;
   stop(*drop, 0);
  }
  SoundBase* slot = nullptr;
  for (auto& s : sound) if (!s.used) { slot = &s; break; }
  if (!slot) return nullptr;
  const u16 gen = slot->generation;
  slot->~SoundBase(); new (slot) SoundBase();
  slot->generation = gen; slot->used = true; slot->type = type; slot->playerId = playerId; slot->order = ++order;
  // BasicSound::InitParam
  slot->fadeVolume.init(0.0f); slot->pauseFadeVolume.init(1.0f); slot->fadeVolume.setTarget(1.0f, 1);
  slot->extMoveVolume.init(1.0f);
  slot->priority = priority;
  return slot;
 }

 // ------------------------------------------------ players: stop/pause
 void stopPlayer(SoundBase& s) {
  if (s.type == TypeSeq) seqFinish(s);
  else if (s.type == TypeWave) wsdFinish(s);
  else strmStop(s);
 }
 void playerPause(SoundBase& s, bool flag) {
  if (s.type == TypeSeq) { s.seq.paused = flag; for (auto* t : s.seq.tracks) if (t) for (Channel* c = t->channels; c; c = c->nextTrackChannel) if (c->active && c->pause != flag) c->pause = flag; }
  else if (s.type == TypeWave) { s.wsd.paused = flag; if (s.wsd.channel) s.wsd.channel->pause = flag; }
  else { s.strm.paused = flag; for (auto& t : s.strm.track) if (Voice* v = t.voice) for (int c = 0; c < v->channels; ++c) if (v->hw[c] >= 0) e.hw->pause(v->hw[c], flag); }
 }

 // ================================================ sequence player
 SeqTrack* allocTrack(SeqPlayer& pl) {
  if (freeTracks.empty()) return nullptr;
  SeqTrack* t = freeTracks.back(); freeTracks.pop_back();
  t->init(); t->player = &pl; return t;
 }
 void freeTrack(SeqTrack* t) { t->player = nullptr; freeTracks.push_back(t); }
 static void trackChannelCallback(Channel* drop, int status, void* arg);
 // SeqTrack::ReleaseAllChannel / FreeAllChannel / StopAllChannel
 void trackRelease(SeqTrack& t, int release) {
  trackUpdateChannelParam(t);
  for (Channel* c = t.channels; c; c = c->nextTrackChannel) if (c->active) { if (release >= 0) c->envelope.setRelease(release); e.channelRelease(*c); }
 }
 void trackFreeAll(SeqTrack& t) { for (Channel* c = t.channels; c; c = c->nextTrackChannel) { c->callback = nullptr; c->callbackArg = nullptr; } t.channels = nullptr; }
 void trackStopAll(SeqTrack& t) {
  Channel* c = t.channels; t.channels = nullptr;
  while (c) { Channel* next = c->nextTrackChannel; c->callback = nullptr; c->callbackArg = nullptr; e.channelStop(*c); c = next; }
 }
 void trackClose(SeqTrack& t) { trackRelease(t, -1); trackFreeAll(t); t.open = false; }
 void seqCloseTrack(SeqPlayer& pl, int i) {
  SeqTrack* t = pl.tracks[i]; if (!t) return;
  trackClose(*t); freeTrack(t); pl.tracks[i] = nullptr;
 }
 void seqFinish(SoundBase& s) {
  SeqPlayer& pl = s.seq;
  pl.started = false; pl.active = false;
  for (int i = 0; i < TrackMax; ++i) seqCloseTrack(pl, i);
 }
 // SeqTrack::UpdateChannelParam
 void trackUpdateChannelParam(SeqTrack& t) {
  if (!t.open || !t.channels) return;
  SeqPlayer& pl = *t.player;
  const f32 v1 = u8(t.volumeRamp.value()) / 127.0f, v2 = t.volume2 / 127.0f, vm = pl.volume / 127.0f;
  const f32 ext = t.extVolumeMoving ? t.extVolumeMove.value() : t.extVolume;
  const f32 volume = (v1 * v1) * (v2 * v2) * (vm * vm) * ext * pl.p.volume;
  const f32 pitch = (t.pitchBend / 128.0f) * t.bendRange;
  const f32 ratio = pl.p.pitch * t.extPitch;
  f32 pan = clamp(s8(t.panRamp.value()) / 63.0f, -1.0f, 1.0f) * t.panRangeT * pl.p.panRange + t.extPan + pl.p.pan;
  const f32 surround = clamp(s8(t.surroundRamp.value()) / 63.0f, 0.0f, 2.0f) + t.extSurroundPan + pl.p.surroundPan;
  const f32 lpf = (t.lpfFreq - 64) / 64.0f + t.extLpf + pl.p.lpf;
  const f32 mainSend = (t.mainSend / 127.0f) - 1.0f + t.extMainSend + pl.p.mainSend;
  f32 fx[3]; for (int i = 0; i < 3; ++i) fx[i] = t.fxSend[i] / 127.0f + t.extFxSend[i] + pl.p.fxSend[i];
  for (Channel* c = t.channels; c; c = c->nextTrackChannel) {
   c->userVolume = volume; c->userPitch = pitch; c->userPitchRatio = ratio; c->userPan = pan; c->userSurroundPan = surround;
   c->userLpf = lpf; c->mainOutVolume = pl.p.mainOutVolume; c->mainSend = mainSend;
   for (int i = 0; i < 3; ++i) c->fxSend[i] = fx[i];
   c->lfo.param = t.lfo; c->lfoTarget = u8(t.lfoTarget);
  }
 }
 // SeqTrack::NoteOn -> SeqNoteOnCallback::NoteOn -> Bank::NoteOn
 Channel* trackNoteOn(SeqTrack& t, int key, int velocity, s32 length, bool tie) {
  SeqPlayer& pl = *t.player;
  if (e.observer) {
   NoteEvent ev; ev.sound = pl.soundId; ev.tick = pl.tickCounter; ev.frame = e.soundFrames; ev.track = t.no; ev.key = key; ev.velocity = velocity; ev.prg = t.prg; ev.length = length; ev.tie = tie;
   e.observer->noteOn(ev);
   if (e.observer->suppressChannels) return nullptr;
  }
  Channel* ch = nullptr;
  if (tie) {
   // The head of the list is the most recent AddChannel.
   ch = t.channels;
   if (ch) { ch->key = key; f32 iv = velocity / 127.0f; ch->initVolume = iv * iv; }
  }
  if (t.monophonic && t.channels) {
   // Retail SeqTrack::NoteOn (8009be10): a released channel is replaced,
   // a live one retunes and takes the new length.
   ch = t.channels;
   if (ch->envelope.status == Envelope::Release) { e.channelStop(*ch); ch = nullptr; }
   else { ch->key = key; f32 iv = velocity / 127.0f; ch->initVolume = iv * iv; ch->length = length; }
  }
  if (!ch) {
   const LoadedFile* bank = e.fileData(pl.bankFile);
   if (!bank) { ++e.stats.missingData; e.stats.lastMissingSound = pl.soundId; e.stats.lastMissingFile = pl.bankFile; return nullptr; }
   InstInfo inst;
   if (!bankInstInfo(bank->main, t.prg, key, velocity, inst)) return nullptr;
   WaveData wave;
   if (!waveFromFile(bank, inst.waveIndex, wave)) { ++e.stats.missingData; e.stats.lastMissingSound = pl.soundId; e.stats.lastMissingFile = pl.bankFile; e.stats.lastMissingWave = u32(inst.waveIndex); return nullptr; }
   const int priority = pl.priority + t.priority;
   ch = e.allocChannel(std::min<int>(wave.channels, ChannelMax), priority, &trackChannelCallback, &t);
   if (!ch) return nullptr;
   ch->key = key; ch->originalKey = inst.originalKey;
   f32 iv = velocity / 127.0f; iv *= iv; iv *= inst.volume / 127.0f;
   ch->initVolume = iv; ch->tune = inst.tune;
   ch->envelope.setAttack(inst.attack); ch->envelope.setHold(inst.hold); ch->envelope.setDecay(inst.decay); ch->envelope.setSustain(inst.sustain); ch->envelope.setRelease(inst.release);
   ch->initPan = (inst.pan - 64) / 63.0f + t.initPan / 63.0f;
   ch->initSurroundPan = 0;
   ch->keyGroup = inst.keyGroup; ch->ignoreNoteOff = inst.noteOffType == 1;
   e.channelStart(*ch, wave, tie ? -1 : length, 0);
   // A key group releases the track's other channels in the same group.
   if (ch->keyGroup > 0) for (Channel* o = t.channels; o; o = o->nextTrackChannel) if (o->keyGroup == ch->keyGroup) e.channelRelease(*o);
   ch->nextTrackChannel = t.channels; t.channels = ch;
  }
  if (t.attack < 0x80) ch->envelope.setAttack(t.attack);
  if (t.decay < 0x80) ch->envelope.setDecay(t.decay);
  if (t.sustain < 0x80) ch->envelope.setSustain(t.sustain);
  if (t.release < 0x80) ch->envelope.setRelease(t.release);
  if (t.hold < 0x80) ch->envelope.setHold(t.hold);
  f32 sweep = t.sweepPitch;
  if (t.portaFlag) sweep += t.portaKey - key;
  if (t.portaTime == 0) { ch->sweepPitch = sweep; ch->sweepLength = length; ch->autoSweep = false; ch->sweepCounter = 0; }
  else {
   int time = t.portaTime; time *= time; time = int(time * (sweep >= 0 ? sweep : -sweep));
   time = int(u32(time) >> 5); time = int(u32(time) << 2) + time;
   ch->sweepPitch = sweep; ch->sweepLength = time; ch->autoSweep = true; ch->sweepCounter = 0;
  }
  t.portaKey = key;
  ch->silence.setTarget(t.silence ? 0.0f : 255.0f, 0);
  ch->releaseFix = pl.releaseFix; ch->panMode = pl.p.panMode; ch->panCurve = pl.p.panCurve;
  return ch;
 }
 // SeqTrack::UpdateChannelLength
 void trackUpdateLength(SeqTrack& t) {
  if (!t.open) return;
  for (Channel* c = t.channels; c; c = c->nextTrackChannel) {
   if (c->length > 0) --c->length;
   if (c->length == 0 && c->envelope.status != Envelope::Release && !t.damper) e.channelNoteOff(*c);
   if (!c->autoSweep) { c->sweepCounter += 1; if (c->sweepCounter > c->sweepLength) c->sweepCounter = c->sweepLength; }
  }
 }
 // ---- MmlParser
 s16* variable(SeqPlayer& pl, SeqTrack& t, int idx) {
  if (idx < 16) return &pl.local[idx];
  if (idx < 32) return &globalVariables[idx - 16];
  if (idx < 48) return &t.vars[idx - 32];
  return nullptr;
 }
 u8 rb(SeqTrack& t) { const auto& d = *t.player->data; return t.pc < d.size() ? d[t.pc++] : (t.pc++, 0xFF); }
 u16 r16(SeqTrack& t) { u16 v = rb(t); v = u16((v << 8) | rb(t)); return v; }
 u32 r24(SeqTrack& t) { u32 v = rb(t); v = (v << 8) | rb(t); v = (v << 8) | rb(t); return v; }
 s32 rvar(SeqTrack& t) { u32 v = 0; u8 b; do { b = rb(t); v = (v << 7) | (b & 0x7F); } while (b & 0x80); return s32(v); }
 enum Arg { ArgU8, ArgS16, ArgVmidi, ArgRandom, ArgVariable };
 s32 arg(SeqTrack& t, Arg type) {
  switch (type) {
  case ArgU8: return rb(t);
  case ArgS16: return r16(t);
  case ArgVmidi: return rvar(t);
  case ArgVariable: { s16* v = variable(*t.player, t, rb(t)); return v ? *v : 0; }
  case ArgRandom: { const s16 lo = s16(r16(t)), hi = s16(r16(t)); s32 r = s32(u32(random()) * u32((hi - lo) + 1)); r >>= 16; return r + lo; }
  }
  return 0;
 }
 // Retail MmlParser::Parse (DOL 80096240). Prefixes: A2 if, then one of
 // A3/A4/A5 (time argument S16/random/variable for B0-DF commands), then one
 // of A0/A1 (random/variable main argument). Returns false at EOF.
 bool parse(SeqTrack& t, bool doNoteOn) {
  bool useArg = false, exec = true; Arg override = ArgVmidi; int timeType = -1;
  u32 cmd = rb(t);
  if (cmd == 0xA2) { exec = t.cmp; cmd = rb(t); }
  if (cmd == 0xA3) { timeType = ArgS16; cmd = rb(t); }
  else if (cmd == 0xA4) { timeType = ArgRandom; cmd = rb(t); }
  else if (cmd == 0xA5) { timeType = ArgVariable; cmd = rb(t); }
  if (cmd == 0xA0) { override = ArgRandom; useArg = true; cmd = rb(t); }
  else if (cmd == 0xA1) { override = ArgVariable; useArg = true; cmd = rb(t); }
  if (!(cmd & 0x80)) {
   const u8 velocity = rb(t);
   const s32 length = arg(t, useArg ? override : ArgVmidi);
   int key = int(cmd) + t.transpose;
   if (!exec) return true;
   key = clamp(key, 0, 127);
   if (!t.mute && doNoteOn) trackNoteOn(t, key, velocity, length > 0 ? length : -1, t.tie);
   if (t.noteWait) { t.wait = length; if (length == 0) t.noteFinishWait = true; }
   return true;
  }
  s32 a1 = 0, a2 = 0;
  switch (cmd & 0xF0) {
  case 0x80:
   switch (cmd) {
   case 0x80: { const s32 time = arg(t, useArg ? override : ArgVmidi); if (exec) t.wait = time; break; }
   case 0x81: a1 = arg(t, useArg ? override : ArgVmidi); if (exec) command(t, cmd, a1, 0); break;
   case 0x88: { const u8 no = rb(t); const u32 off = r24(t); if (exec) command(t, cmd, no, s32(off)); break; }
   case 0x89: case 0x8A: { const u32 off = r24(t); if (exec) command(t, cmd, s32(off), 0); break; }
   }
   break;
  case 0xB0: case 0xC0: case 0xD0: {
   const s32 v = arg(t, useArg ? override : ArgU8);
   if (timeType >= 0) a2 = arg(t, Arg(timeType));
   if (!exec) break;
   a1 = (cmd == 0xC3 || cmd == 0xC4) ? s32(s8(u8(v))) : s32(u8(v));
   command(t, cmd, a1, a2);
   break;
  }
  case 0x90: if (exec) command(t, cmd, 0, 0); break;
  case 0xE0: a1 = s16(arg(t, useArg ? override : ArgS16)); if (exec) command(t, cmd, a1, 0); break;
  case 0xF0:
   switch (cmd) {
   case 0xFE: (void)r16(t); break;
   case 0xFF: if (exec) return false; break; // an "if"-guarded EOF only ends when true
   case 0xF0: {
    const u32 ex = rb(t);
    switch (ex & 0xF0) {
    case 0xE0: a1 = s16(arg(t, useArg ? override : ArgS16)); if (exec) command(t, (cmd << 8) + ex, a1, 0); break;
    case 0x80: case 0x90: a1 = rb(t); a2 = s16(arg(t, useArg ? override : ArgS16)); if (exec) command(t, (cmd << 8) + ex, a1, a2); break;
    }
    if (exec) command(t, cmd, a1, a2);
    break;
   }
   default: if (exec) command(t, cmd, 0, 0); break;
   }
   break;
  }
  return true;
 }
 // Retail MmlParser::CommandProc (DOL 800968d0, jump table 80270238).
 void command(SeqTrack& t, u32 command, s32 a1, s32 a2) {
  SeqPlayer& pl = *t.player;
  if (command <= 0xFF) {
   switch (command) {
   case 0x81: if (a1 < 0x10000) t.prg = a1 & 0xFFFF; break;
   case 0x88: {
    SeqTrack* nt = a1 < TrackMax ? pl.tracks[a1] : nullptr;
    if (nt && nt != &t) { trackClose(*nt); nt->base = t.base; nt->pc = t.base + u32(a2); nt->hasPc = true; nt->open = true; }
    break;
   }
   case 0x89: t.pc = t.base + u32(a1); break;
   case 0x8A: if (t.depth < CallStack) { t.stack[t.depth] = {false, 0, t.pc}; ++t.depth; t.pc = t.base + u32(a1); } break;
   case 0xB0: pl.timebase = u8(a1); break;
   case 0xB1: t.hold = a1 & 0xFF; break;                       // envelope hold
   case 0xB2: t.monophonic = a1 != 0; if (t.monophonic) { trackRelease(t, -1); trackFreeAll(t); } break;
   case 0xC0: t.panRamp.set(s8(u8(a1 - 64)), a2); break;
   case 0xC1: t.volumeRamp.set(u8(a1), a2); break;
   case 0xC2: pl.volume = u8(a1); break;
   case 0xC3: t.transpose = s8(a1); break;
   case 0xC4: t.pitchBend = s8(a1); break;
   case 0xC5: t.bendRange = u8(a1); break;
   case 0xC6: t.priority = u8(a1); break;
   case 0xC7: t.noteWait = a1 != 0; break;
   case 0xC8: t.tie = a1 != 0; trackRelease(t, -1); trackFreeAll(t); break;
   case 0xC9: t.portaKey = u8(a1 + t.transpose); t.portaFlag = true; break;
   case 0xCA: t.lfo.depth = u8(a1) * 0.0078125f; break;
   case 0xCB: t.lfo.speed = u8(a1) * 0.390625f; break;
   case 0xCC: t.lfoTarget = u8(a1); break;
   case 0xCD: t.lfo.range = u8(a1); break;
   case 0xCE: t.portaFlag = a1 != 0; break;
   case 0xCF: t.portaTime = u8(a1); break;
   case 0xD0: t.attack = u8(a1); break;
   case 0xD1: t.decay = u8(a1); break;
   case 0xD2: t.sustain = u8(a1); break;
   case 0xD3: t.release = u8(a1); break;
   case 0xD4: if (t.depth < CallStack) { t.stack[t.depth] = {true, u8(a1), t.pc}; ++t.depth; } break;
   case 0xD5: t.volume2 = u8(a1); break;
   case 0xD7: t.surroundRamp.set(s8(u8(a1)), a2); break;
   case 0xD8: t.lpfFreq = u8(a1); break;
   case 0xD9: t.fxSend[0] = u8(a1); break;
   case 0xDA: t.fxSend[1] = u8(a1); break;
   case 0xDB: t.mainSend = u8(a1); break;
   case 0xDC: t.initPan = s8(u8(a1 - 64)); break;
   case 0xDD: // SeqTrack::SetMute
    switch (a1) { case 0: t.mute = false; break; case 1: trackStopAll(t); t.mute = true; break; case 2: trackRelease(t, -1); trackFreeAll(t); t.mute = true; break; case 3: t.mute = true; break; }
    break;
   case 0xDE: t.fxSend[2] = u8(a1); break;
   case 0xDF: t.damper = u8(a1) >= 64; break;
   case 0xE0: t.lfo.delay = u32(a1 * 5); break;
   case 0xE1: pl.tempo = clamp<s32>(a1, 0, 1023); break;
   case 0xE3: t.sweepPitch = f32(a1) * 0.015625f; break;         // retail: /64
   case 0xFC: {
    if (!t.depth) break;
    auto& top = t.stack[t.depth - 1];
    if (!top.loop) break;
    u8 count = top.count;
    if (count != 0 && --count == 0) { --t.depth; break; }
    top.count = count; t.pc = top.address;
    break;
   }
   case 0xFD: { // returns past any open loops to the innermost call
    SeqTrack::Frame* f = nullptr;
    while (t.depth) { --t.depth; if (!t.stack[t.depth].loop) { f = &t.stack[t.depth]; break; } }
    if (f) t.pc = f->address;
    break;
   }
   }
   return;
  }
  if (command > 0xFFFF) return;
  const u32 ex = command & 0xFF;
  s16* v = nullptr;
  if ((ex & 0xF0) == 0x80 || (ex & 0xF0) == 0x90) { v = variable(pl, t, a1); if (!v) return; }
  switch (ex) {
  case 0x80: *v = s16(a2); break;
  case 0x81: *v = s16(*v + a2); break;
  case 0x82: *v = s16(*v - a2); break;
  case 0x83: *v = s16(*v * a2); break;
  case 0x84: if (a2) *v = s16(*v / a2); break;
  case 0x85: if (a2 >= 0) *v = s16(*v << a2); else *v = s16(*v >> -a2); break;
  case 0x86: { bool minus = false; if (a2 < 0) { minus = true; a2 = s16(-a2); } s32 r = s32(u32(random()) * u32(a2 + 1)); r >>= 16; if (minus) r = -r; *v = s16(r); break; }
  case 0x87: *v = s16(*v & a2); break;
  case 0x88: *v = s16(*v | a2); break;
  case 0x89: *v = s16(*v ^ a2); break;
  case 0x8A: *v = s16(~u16(a2)); break;
  case 0x8B: if (a2) *v = s16(*v % a2); break;
  case 0x90: t.cmp = *v == a2; break;
  case 0x91: t.cmp = *v >= a2; break;
  case 0x92: t.cmp = *v > a2; break;
  case 0x93: t.cmp = *v <= a2; break;
  case 0x94: t.cmp = *v < a2; break;
  case 0x95: t.cmp = *v != a2; break;
  default: break;
  }
 }
 // SeqTrack::ParseNextTick
 int trackParseNextTick(SeqTrack& t, bool doNoteOn) {
  if (!t.open) return 0;
  t.volumeRamp.tick(); t.panRamp.tick(); t.surroundRamp.tick();
  if (t.noteFinishWait) { if (t.channels) return 1; t.noteFinishWait = false; }
  if (t.wait > 0 && --t.wait > 0) return 1;
  if (t.hasPc) {
   int guard = 0;
   while (t.wait == 0 && !t.noteFinishWait) {
    if (!parse(t, doNoteOn)) return -1;
    if (++guard > 100000) return -1; // malformed data must not hang the audio thread
   }
  }
  return 1;
 }
 // SeqPlayer::ParseNextTick
 int seqParseNextTick(SoundBase& s, bool doNoteOn) {
  SeqPlayer& pl = s.seq; bool active = false;
  for (int i = 0; i < TrackMax; ++i) {
   SeqTrack* t = pl.tracks[i]; if (!t) continue;
   trackUpdateLength(*t);
   if (trackParseNextTick(*t, doNoteOn) < 0) seqCloseTrack(pl, i);
   if (pl.tracks[i] && pl.tracks[i]->open) active = true;
  }
  return active ? 0 : 1;
 }
 // SeqPlayer::Update (3 ms)
 void seqUpdate(SoundBase& s) {
  SeqPlayer& pl = s.seq;
  if (!pl.active || !pl.started) return;
  if (pl.skipTicks || pl.skipMs > 0) seqSkip(s);
  else if (!pl.paused) seqUpdateTick(s, SoundFrameMs);
  if (!pl.active) return;
  for (auto* t : pl.tracks) if (t) {
   if (t->extVolumeMoving) { t->extVolumeMove.update(); if (t->extVolumeMove.finished()) { t->extVolume = t->extVolumeMove.target; t->extVolumeMoving = false; } }
   trackUpdateChannelParam(*t);
  }
 }
 void seqUpdateTick(SoundBase& s, int msec) {
  SeqPlayer& pl = s.seq;
  f32 tickPerMs = pl.baseTempo(); if (tickPerMs == 0.0f) return;
  f32 rest = f32(msec), next = pl.tickFraction / tickPerMs;
  while (next < rest) {
   rest -= next;
   if (seqParseNextTick(s, true) != 0) { seqFinish(s); return; }
   ++pl.tickCounter;
   tickPerMs = pl.baseTempo(); if (tickPerMs == 0.0f) return;
   next = 1.0f / tickPerMs;
  }
  next -= rest;
  pl.tickFraction = next * tickPerMs;
 }
 void seqSkip(SoundBase& s) {
  SeqPlayer& pl = s.seq;
  for (auto* t : pl.tracks) if (t) { trackRelease(*t, 127); trackFreeAll(*t); }
  int count = 0;
  while (pl.skipTicks != 0 || pl.skipMs * pl.baseTempo() >= 1.0f) {
   if (count >= 768) return;
   if (pl.skipTicks) --pl.skipTicks;
   else pl.skipMs -= 1.0f / pl.baseTempo();
   if (seqParseNextTick(s, false) != 0) { seqFinish(s); return; }
   ++count; ++pl.tickCounter;
  }
  pl.skipMs = 0;
 }

 // ================================================ wave sound player
 static void wsdChannelCallback(Channel* drop, int status, void* arg) {
  auto* s = static_cast<SoundBase*>(arg);
  (void)drop; (void)status;
  s->wsd.channel = nullptr;
 }
 void wsdFinish(SoundBase& s) {
  WsdPlayerState& w = s.wsd;
  if (w.channel) {
   wsdUpdateChannel(s);
   if (w.channel->active) e.channelRelease(*w.channel);
   w.channel->callback = nullptr; w.channel->callbackArg = nullptr;
  }
  w.channel = nullptr; w.started = false; w.active = false;
 }
 bool wsdStartChannel(SoundBase& s) {
  WsdPlayerState& w = s.wsd;
  const LoadedFile* f = e.fileData(w.fileId);
  if (!f) { ++e.stats.missingData; return false; }
  WsdNoteInfo note;
  if (!wsdInfo(f->main, w.index, 0, w.info, note)) return false;
  WaveData wave;
  if (!waveFromFile(f, note.waveIndex, wave)) { ++e.stats.missingData; e.stats.lastMissingSound = s.id; e.stats.lastMissingFile = w.fileId; e.stats.lastMissingWave = u32(note.waveIndex); return false; }
  const u32 start = u32((u64(w.startOffsetMs) * wave.rate) / 1000);
  if (start > wave.loopEnd) return false;
  Channel* ch = e.allocChannel(std::min<int>(wave.channels, ChannelMax), w.channelPriority + 64, &wsdChannelCallback, &s);
  if (!ch) return false;
  ch->envelope.setAttack(note.attack); ch->envelope.setHold(note.hold); ch->envelope.setDecay(note.decay); ch->envelope.setSustain(note.sustain); ch->envelope.setRelease(note.release);
  ch->releaseFix = w.releaseFix;
  e.channelStart(*ch, wave, -1, start);
  w.channel = ch; w.wavePlay = true;
  return true;
 }
 using u64 = unsigned long long;
 // WsdPlayer::UpdateChannel
 void wsdUpdateChannel(SoundBase& s) {
  WsdPlayerState& w = s.wsd; Channel* c = w.channel; if (!c) return;
  const f32 volume = w.p.volume;
  const f32 ratio = w.p.pitch * w.info.pitch;
  f32 pan = w.info.pan <= 1 ? (w.info.pan - 63) / 63.0f : (w.info.pan - 64) / 63.0f;
  pan = pan * w.p.panRange + w.p.pan;
  f32 surround = w.info.surroundPan <= 1 ? (w.info.surroundPan + 1) / 63.0f : w.info.surroundPan / 63.0f;
  surround += w.info.surroundPan / 64.0f; surround += w.p.surroundPan;
  const f32 mainSend = (w.info.mainSend / 127.0f) - 1.0f + w.p.mainSend;
  const u8 info[3] = {w.info.fxSendA, w.info.fxSendB, w.info.fxSendC};
  c->panMode = w.p.panMode; c->panCurve = w.p.panCurve;
  c->userVolume = volume; c->userPitchRatio = ratio; c->userPan = pan; c->userSurroundPan = surround; c->userLpf = w.p.lpf;
  c->mainOutVolume = w.p.mainOutVolume; c->mainSend = mainSend;
  for (int i = 0; i < 3; ++i) c->fxSend[i] = info[i] / 127.0f + w.p.fxSend[i];
  c->lfo.param = w.lfo;
 }
 // WsdPlayer::Update (3 ms)
 void wsdUpdate(SoundBase& s) {
  WsdPlayerState& w = s.wsd;
  if (!w.active || !w.started) return;
  if (!w.paused) {
   if (w.wavePlay && !w.channel) { wsdFinish(s); return; }
   if (!w.wavePlay && !wsdStartChannel(s)) { wsdFinish(s); return; }
  }
  wsdUpdateChannel(s);
 }

 // ================================================ stream player
 void strmStop(SoundBase& s) {
  StrmPlayerState& st = s.strm;
  for (auto& t : st.track) if (t.voice) { e.voiceStopHw(*t.voice); e.voiceFree(*t.voice); t.voice = nullptr; }
  st.feed.reset(); st.active = false; st.started = false; st.prepared = false;
 }
 // StrmPlayer::Prepare: the header and first blocks load asynchronously
 // (I/O thread); IsPrepared gates BasicSound's start as in retail.
 bool strmPrepare(SoundBase& s, const SoundEntry& info, const StartInfo* si) {
  StrmPlayerState& st = s.strm;
  const FileEntry* f = e.archive->file(info.fileId);
  if (!f || f->streamPath == InvalidId || !e.openStream) { ++e.stats.missingData; return false; }
  st.feed = e.openStream(e.openStreamCtx, e.archive->string(f->streamPath), si && si->startOffsetSamples ? si->startOffsetSamples : info.data);
  if (!st.feed) { ++e.stats.missingData; return false; }
  st.trackMask = info.allocTrack ? info.allocTrack : 1;
  st.active = true; st.prepared = false;
  return true;
 }
 // Once the header is in: StrmPlayer::SetupPlayer + AllocVoices.
 bool strmSetup(SoundBase& s) {
  StrmPlayerState& st = s.strm;
  st.tracks = std::min(st.feed->tracks(), 8);
  for (int i = 0; i < st.tracks; ++i) {
   auto& t = st.track[i];
   t.info = st.feed->track(i); t.volume.init(1.0f);
   t.active = (st.trackMask >> i) & 1 && t.info.channels > 0;
   if (!t.active) continue;
   // StrmPlayer::AllocVoices: VoiceManager::AllocVoice(channels, 1, 0xFF, ...).
   Voice* v = e.allocVoice(std::min<int>(t.info.channels, ChannelMax), VoicePriorityMax, nullptr);
   if (!v) { strmStop(s); return false; }
   v->stream = true; v->feed = st.feed.get(); v->rate = st.feed->rate(); v->streamOwner = &s;
   for (int c = 0; c < v->channels; ++c) v->streamChannel[c] = t.info.channel[c];
   t.voice = v;
  }
  st.prepared = true;
  return true;
 }
 // StrmPlayer::UpdateVoiceParams (retail 800a6ff0), per sound frame.
 void strmUpdate(SoundBase& s) {
  StrmPlayerState& st = s.strm;
  if (!st.active) return;
  if (!st.prepared) {
   const int state = st.feed ? st.feed->state() : -1;
   if (state < 0) { ++e.stats.missingData; strmStop(s); }
   else if (state > 0 && !strmSetup(s)) { ++e.stats.startsFailed; strmStop(s); }
   return;
  }
  if (!st.started) return;
  for (int i = 0; i < st.tracks; ++i) {
   auto& t = st.track[i]; Voice* v = t.voice; if (!t.active || !v) continue;
   const f32 authoredPan = t.info.pan <= 1 ? (t.info.pan - 63) / 63.0f : (t.info.pan - 64) / 63.0f;
   v->volume = clamp(st.p.volume * (t.info.volume / 127.0f) * t.volume.value(), 0.0f, 1.0f);
   v->veTarget = 1; v->veInit = 1; v->pitch = st.p.pitch;
   v->pan = st.p.pan + authoredPan;
   v->panMode = st.p.panMode; v->panCurve = st.p.panCurve;
   v->surroundPan = st.p.surroundPan; v->lpf = clamp(1.0f + st.p.lpf, 0.0f, 1.0f);
   v->mainOutVolume = clamp(st.p.mainOutVolume, 0.0f, 1.0f); v->mainSend = clamp(st.p.mainSend + 1.0f, 0.0f, 1.0f);
   v->starting = true;
  }
  if (st.feed && st.feed->ended()) {
   bool playing = false;
   for (int i = 0; i < st.tracks; ++i) if (Voice* v = st.track[i].voice) for (int c = 0; c < v->channels; ++c) if (v->hw[c] >= 0 && !e.hw->finished(v->hw[c])) playing = true;
   if (!playing) strmStop(s);
  }
 }
 void strmGameFrame(SoundBase& s) { for (auto& t : s.strm.track) t.volume.update(); }

 // ================================================ BasicSound::Update (game frame)
 // Waves still loading into the cache: recount; give up after WaveWaitFrames.
 bool refreshWaves(SoundBase& s) {
  if (!s.wavesPending) return true;
  const WaveReach r = e.archive->reach(s.id);
  u16 n = 0;
  for (u32 i = 0; i < r.count; ++i) n += !e.residency->ready(r.file, r.waves[i]);
  s.wavesPending = n;
  if (n && ++s.waitFrames > WaveWaitFrames) { ++e.stats.startsFailed; shutdown(s); return false; }
  return true;
 }
 void basicUpdate(SoundBase& s, bool advanceHold) {
  if (!refreshWaves(s)) return;
  if (advanceHold && s.autoStop && s.playerActive()) { if (s.autoStopCounter == 0) { stop(s, 0); return; } --s.autoStopCounter; }
  bool startPlayer = false;
  if (!s.startedFlag) {
   if (!s.startFlag) return;
   if (!prepared(s)) return;
   startPlayer = true;
  }
  if (s.playerStarted() && s.updateCounter < 0xFFFFFFFFu) ++s.updateCounter;
  if (!s.playerActive()) { shutdown(s); return; }
  if (s.playerPaused()) return;
  if (s.pauseFade) s.pauseFadeVolume.update(); else { s.fadeVolume.update(); s.extMoveVolume.update(); if (s.type == TypeStrm) strmGameFrame(s); }
  f32 volume = s.initVolume * players[s.playerId].volume * s.extMoveVolume.value() * s.fadeVolume.value() * s.pauseFadeVolume.value() * s.ambient.volume;
  // 3DS output calibration: preserve authored automation/attenuation, but
  // reserve summing headroom for simultaneous karts, UI and music on NDSP.
  // BRSAR player 0 is PL_BGM; SE_RC_LAP (218) is a full-volume two-track
  // sequence on player 7 and needs extra peak allowance over race ambience.
  constexpr f32 bgmOutput=.75f, effectOutput=.45f, lapOutput=.32f;
  volume *= s.playerId==0?bgmOutput:s.id==218?lapOutput:effectOutput;
  const f32 pan = s.extPan + s.ambient.pan;
  const f32 pitch = s.extPitch * s.ambient.pitch;
  const f32 surround = s.extSurroundPan + s.ambient.surroundPan;
  PlayerParams& p = s.params();
  p.volume = volume; p.pan = pan; p.surroundPan = surround; p.pitch = pitch; p.mainOutVolume = s.mainOutVolume;
  p.fxSend[0] = s.ambient.fxSend; p.lpf = s.ambient.lpf;
  if (s.fadeOut && s.fadeVolume.finished()) { s.fadeOut = false; shutdown(s); return; }
  if (s.pauseFade && s.pauseFadeVolume.finished()) { s.pauseFade = false; if (s.pauseFlag) playerPause(s, true); }
  if (startPlayer && playerStart(s)) { s.startedFlag = true; s.startFlag = false; }
 }
 bool prepared(const SoundBase& s) const { return !s.wavesPending && (s.type == TypeStrm ? s.strm.prepared : true); }
 bool playerStart(SoundBase& s) {
  if (s.type == TypeSeq) { s.seq.started = true; return true; }
  if (s.type == TypeWave) { s.wsd.started = true; return true; }
  s.strm.started = true; return true;
 }

 // ================================================ start
 bool setup(Handle& h, u32 id, bool hold, const StartInfo* si, const Ambient* amb, SoundBase** out) {
  const SoundEntry* info = e.archive ? e.archive->sound(id) : nullptr;
  if (!info || info->type == TypeInvalid) { ++e.stats.startsFailed; return false; }
  u32 playerId = info->playerId; int playerPriority = info->playerPriority;
  if (si) { if (si->playerId >= 0) playerId = u32(si->playerId); if (si->playerPriority >= 0) playerPriority = si->playerPriority; }
  const int startPriority = (hold ? playerPriority - 1 : playerPriority) + (amb ? amb->priority : 0);
  SoundBase* s = alloc(info->type, playerId, playerPriority, startPriority);
  if (!s) { ++e.stats.startsFailed; return false; }
  s->id = id;
  if (amb) { s->ambient = *amb; s->hasAmbient = true; }
  bool ok = false;
  if (info->type == TypeSeq) {
   const LoadedFile* f = e.fileData(info->fileId);
   SeqPlayer& pl = s->seq;
   pl = SeqPlayer{};
   for (auto& v : pl.local) v = -1;
   pl.p.panMode = info->panMode; pl.p.panCurve = info->panCurve; pl.p.id = id;
   pl.channelPriority = info->channelPriority; pl.priority = info->channelPriority; pl.releaseFix = info->releasePriorityFix;
   pl.bankFile = e.archive->bankFile(info->bankId); pl.soundId = id;
   if (f && f->main.size() > 0x20 && !std::memcmp(f->main.data(), "RSEQ", 4)) {
    pl.data = &f->main;
    // SeqPlayer::Setup: allocate the flagged tracks.
    int needed = 0; for (u32 m = info->allocTrack; m; m >>= 1) needed += m & 1;
    while (int(freeTracks.size()) < needed) {
     SoundBase* drop = lowest([](const SoundBase& x, u32) { return x.type == TypeSeq; }, 0);
     if (!drop || drop == s) break;
     stop(*drop, 0);
    }
    if (int(freeTracks.size()) >= needed) {
     u32 m = info->allocTrack;
     for (int i = 0; m; m >>= 1, ++i) if (m & 1) { SeqTrack* t = allocTrack(pl); t->no = i; pl.tracks[i] = t; }
     const u8* d = f->main.data();
     const u32 dataBlock = be32(d + 0x10);
     const u32 base = dataBlock + be32(d + dataBlock + 8);
     if (pl.tracks[0]) { pl.tracks[0]->base = base; pl.tracks[0]->pc = base + info->data; pl.tracks[0]->hasPc = true; pl.tracks[0]->open = true; }
     pl.active = true;
     if (si && si->startOffsetMs) pl.skipMs = f32(si->startOffsetMs);
     ok = pl.tracks[0] != nullptr;
    }
   } else ++e.stats.missingData;
  } else if (info->type == TypeWave) {
   WsdPlayerState& w = s->wsd; w = WsdPlayerState{};
   w.p.panMode = info->panMode; w.p.panCurve = info->panCurve; w.p.id = id;
   w.fileId = info->fileId; w.index = int(info->data); w.channelPriority = info->channelPriority; w.releaseFix = info->releasePriorityFix;
   w.startOffsetMs = si ? si->startOffsetMs : 0;
   ok = e.fileData(info->fileId) != nullptr; if (!ok) ++e.stats.missingData;
   w.active = ok;
  } else if (info->type == TypeStrm) {
   s->strm.p.panMode = info->panMode; s->strm.p.panCurve = info->panCurve; s->strm.p.id = id;
   ok = strmPrepare(*s, *info, si);
  }
  if (!ok) { ++e.stats.startsFailed; shutdown(*s); return false; }
  if (e.residency && (info->type == TypeSeq || info->type == TypeWave)) {
   const WaveReach r = e.archive->reach(id);
   for (u32 i = 0; i < r.count; ++i) if (!e.residency->acquire(r.file, r.waves[i])) ++s->wavesPending;
   s->pinned = r.count > 0;
  }
  s->initVolume = clamp(info->volume / 127.0f, 0.0f, 1.0f);
  h = Handle{u16(s - sound), s->generation};
  *out = s;
  return true;
 }
};

void Engine::Impl::Sounds::trackChannelCallback(Channel* drop, int status, void* arg) {
 auto* t = static_cast<SeqTrack*>(arg);
 (void)status;
 if (!t) return;
 if (t->channels == drop) { t->channels = drop->nextTrackChannel; return; }
 for (Channel* c = t->channels; c && c->nextTrackChannel; c = c->nextTrackChannel) if (c->nextTrackChannel == drop) { c->nextTrackChannel = drop->nextTrackChannel; return; }
}

// Voice -> Channel callback (Channel::VoiceCallbackFunc).
void Engine::Impl::voiceCallback(Voice& v, int status) {
 Channel* c = v.owner;
 if (v.streamOwner) streamVoiceLost(v);
 v.active = false; v.owner = nullptr; v.streamOwner = nullptr;
 if (!c) return;
 int channelStatus = status == Voice::FinishWave ? Channel::Finish : status == Voice::Cancel ? Channel::Cancel : Channel::Drop;
 auto cb = c->callback; void* arg = c->callbackArg;
 c->voice = nullptr; c->pause = false; c->active = false;
 channelFreeSlot(*c);
 if (cb) cb(c, channelStatus, arg);
}

void Engine::Impl::streamVoiceLost(Voice& v) {
 auto* s = static_cast<SoundBase*>(v.streamOwner);
 for (auto& t : s->strm.track) if (t.voice == &v) t.voice = nullptr;
}

void Engine::Impl::setupSounds() {
 sounds = std::make_unique<Sounds>(*this);
 const auto& info = archive->info;
 sounds->seqLimit = info.seqSounds ? info.seqSounds : 64;
 sounds->strmLimit = info.strmSounds ? info.strmSounds : 2;
 sounds->waveLimit = info.waveSounds ? info.waveSounds : 6;
 const int tracks = info.seqTracks ? info.seqTracks : 96;
 sounds->trackPool.resize(tracks);
 for (auto& t : sounds->trackPool) { t.init(); sounds->freeTracks.push_back(&t); }
 sounds->players.resize(std::max<u32>(1, archive->playerCount()));
 for (u32 i = 0; i < archive->playerCount(); ++i) sounds->players[i].playable = archive->player(i)->playableSoundCount;
}

void Engine::Impl::gameFrame(bool advanceHold) {
 if (!sounds) return;
 // SoundArchivePlayer::Update -> SoundPlayer::Update for every player.
 for (auto& s : sounds->sound) if (s.used) sounds->basicUpdate(s, advanceHold);
}

void Engine::Impl::soundFrame() {
 if (!sounds) return;
 ++soundFrames;
 // SoundThread::SoundThreadProc order: players, channels, random, voices.
 for (auto& s : sounds->sound) {
  if (!s.used) continue;
  if (s.type == TypeSeq) sounds->seqUpdate(s);
  else if (s.type == TypeWave) sounds->wsdUpdate(s);
  else sounds->strmUpdate(s);
 }
 allChannelsUpdate();
 (void)random();
 // VoiceManager::UpdateAllVoices: StopFinished, Calc, Update.
 for (auto& v : voices) {
  if (!v.active || !v.started || v.stream) continue;
  bool done = true;
  for (int c = 0; c < v.channels; ++c) if (v.hw[c] >= 0 && !hw->finished(v.hw[c])) done = false;
  if (done && v.channels > 0) { voiceStopHw(v); voiceFreeHw(v); voiceCallback(v, Voice::FinishWave); }
 }
 for (auto& v : voices) {
  if (!v.active) continue;
  voiceStartIfNeeded(v);
  voiceCommit(v);
 }
}

// ======================================================================
Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() { shutdown(); }
void Engine::setup(const Archive* archive, Hardware* hardware) {
 impl_->archive = archive; impl_->hw = hardware; currentArchive = archive;
 impl_->hwVoices = std::min(hardware->voiceCount(), VoiceMax);
 impl_->files.assign(archive->fileCount(), nullptr);
 impl_->setupSounds();
}
void Engine::shutdown() {
 auto& e = *impl_;
 if (e.sounds) for (auto& s : e.sounds->sound) if (s.used) e.sounds->shutdown(s);
 for (auto& v : e.voices) if (v.active) { e.voiceStopHw(v); e.voiceFree(v); }
 for (auto& c : e.channels) e.channelFreeSlot(c);
 e.sounds.reset();
}
const Archive* Engine::archive() const { return impl_->archive; }
void Engine::setResidency(WaveResidency* residency) { impl_->residency = residency; }
void Engine::addFile(LoadedFile* file) { auto& e = *impl_; if (file && file->fileId < e.files.size()) e.files[file->fileId] = file; }
LoadedFile* Engine::fileData(u32 id) const { return impl_->fileData(id); }
void Engine::setStreamOpener(std::unique_ptr<StreamFeed> (*open)(void*, const char*, u32), void* ctx) { impl_->openStream = open; impl_->openStreamCtx = ctx; }
void Engine::removeFile(u32 id) {
 auto& e = *impl_;
 if (id >= e.files.size() || !e.files[id]) return;
 // SoundArchivePlayer::InvalidateData / InvalidateWaveData: stop users first.
 if (e.sounds) for (auto& s : e.sounds->sound) {
  if (!s.used) continue;
  const SoundEntry* info = e.archive->sound(s.id);
  bool uses = info && info->fileId == id;
  if (info && info->type == TypeSeq && e.archive->bankFile(info->bankId) == id) uses = true;
  if (uses) e.sounds->shutdown(s);
 }
 // Channels still holding waves from this file (released tails) stop too.
 const LoadedFile* f = e.files[id];
 for (auto& c : e.channels) if (c.used && c.voice && f->waves) {
  const u8* d = c.voice->wave.channel[0].data;
  if (d >= f->waves && d < f->waves + f->waveBytes) e.channelStop(c);
 }
 e.files[id] = nullptr;
}
LoadedFile* Engine::Impl::fileData(u32 id) const { return id < files.size() ? files[id] : nullptr; }

bool Engine::startSound(Handle& h, u32 id, const StartInfo* si, const Ambient* amb) {
 SoundBase* s = nullptr;
 if (!impl_->sounds || !impl_->sounds->setup(h, id, false, si, amb, &s)) { h = {}; return false; }
 if (!s->startedFlag) s->startFlag = true; // StartPrepared
 return true;
}
bool Engine::holdSound(Handle& h, u32 id, const StartInfo* si, const Ambient* amb) {
 if (SoundBase* s = impl_->sounds ? impl_->sounds->get(h) : nullptr; s && s->id == id) {
  s->autoStopCounter = 1; s->autoStop = true;
  if (amb) s->ambient = *amb;
  return true;
 }
 SoundBase* s = nullptr;
 if (!impl_->sounds || !impl_->sounds->setup(h, id, true, si, amb, &s)) { h = {}; return false; }
 if (!s->startedFlag) s->startFlag = true;
 s->autoStopCounter = 1; s->autoStop = true;
 return true;
}
bool Engine::prepareSound(Handle& h, u32 id, const StartInfo* si, const Ambient* amb) {
 SoundBase* s = nullptr;
 if (!impl_->sounds || !impl_->sounds->setup(h, id, false, si, amb, &s)) { h = {}; return false; }
 return true;
}
void Engine::startPrepared(Handle h) { if (auto* s = impl_->sounds->get(h)) if (!s->startedFlag) s->startFlag = true; }
bool Engine::attached(Handle h) const { return impl_->sounds && impl_->sounds->get(h); }
bool Engine::started(Handle h) const { const auto* s=impl_->sounds?impl_->sounds->get(h):nullptr;return s&&s->startedFlag; }
u32 Engine::soundId(Handle h) const { auto* s = impl_->sounds ? impl_->sounds->get(h) : nullptr; return s ? s->id : InvalidId; }
void Engine::detach(Handle& h) { h = {}; }
void Engine::stop(Handle h, int frames) { if (auto* s = impl_->sounds->get(h)) impl_->sounds->stop(*s, frames); }
void Engine::pause(Handle h, bool p, int frames) { if (auto* s = impl_->sounds->get(h)) impl_->sounds->pause(*s, p, frames); }
void Engine::fadeIn(Handle h, int frames) { if (auto* s = impl_->sounds->get(h)) { if (s->fadeOut) return; s->fadeVolume.setTarget(1.0f, int(frames * (1.0f - s->fadeVolume.value()))); } }
void Engine::setVolume(Handle h, f32 v, int frames) { if (auto* s = impl_->sounds->get(h)) s->extMoveVolume.setTarget(clamp(v, 0.0f, 1.0f), frames); }
void Engine::setPitch(Handle h, f32 p) { if (auto* s = impl_->sounds->get(h)) s->extPitch = p; }
void Engine::setPan(Handle h, f32 p) { if (auto* s = impl_->sounds->get(h)) s->extPan = p; }
void Engine::setSurroundPan(Handle h, f32 p) { if (auto* s = impl_->sounds->get(h)) s->extSurroundPan = p; }
void Engine::setLpf(Handle h, f32 f) { if (auto* s = impl_->sounds->get(h)) s->params().lpf = f; }
void Engine::setFxSend(Handle h, int bus, f32 v) { if (auto* s = impl_->sounds->get(h)) if (bus >= 0 && bus < 3) s->params().fxSend[bus] = v; }
void Engine::setAmbient(Handle h, const Ambient& a) { if (auto* s = impl_->sounds->get(h)) { s->ambient = a; s->hasAmbient = true; } }
void Engine::setPlayerPriority(Handle h, int priority) { if (auto* s = impl_->sounds->get(h)) s->priority = clamp(priority, 0, PriorityMax); }
int Engine::playerPriority(Handle h) const { auto* s = impl_->sounds ? impl_->sounds->get(h) : nullptr; return s ? s->priority : 0; }
const Ambient* Engine::ambient(Handle h) const { auto* s = impl_->sounds ? impl_->sounds->get(h) : nullptr; return s ? &s->ambient : nullptr; }
void Engine::setAutoStop(Handle h, int frames) { if (auto* s = impl_->sounds->get(h)) { s->autoStopCounter = frames; s->autoStop = frames > 0; } }
void Engine::setSeqVariable(Handle h, int index, s16 value) { if (auto* s = impl_->sounds->get(h)) if (s->type == TypeSeq && index >= 0 && index < 16) s->seq.local[index] = value; }
void Engine::setGlobalVariable(int index, s16 value) { if (index >= 0 && index < 16) globalVariables[index] = value; }
void Engine::setTrackVolume(Handle h, u32 mask, f32 volume, int frames) {
 auto* s = impl_->sounds->get(h); if (!s || s->type != TypeSeq) return;
 for (int i = 0; i < TrackMax && mask; ++i, mask >>= 1) if ((mask & 1) && s->seq.tracks[i]) {
  SeqTrack& t = *s->seq.tracks[i];
  if (frames <= 0) { t.extVolume = volume; t.extVolumeMoving = false; }
  else { t.extVolumeMove.init(t.extVolumeMoving ? t.extVolumeMove.value() : t.extVolume); t.extVolumeMove.setTarget(volume, frames * 16667 / 3000); t.extVolumeMoving = true; }
 }
}
void Engine::setTrackPitch(Handle h, u32 mask, f32 pitch) {
 auto* s = impl_->sounds->get(h); if (!s || s->type != TypeSeq) return;
 for (int i = 0; i < TrackMax && mask; ++i, mask >>= 1) if ((mask & 1) && s->seq.tracks[i]) s->seq.tracks[i]->extPitch = pitch;
}
void Engine::setTempoRatio(Handle h, f32 r) { if (auto* s = impl_->sounds->get(h)) if (s->type == TypeSeq) s->seq.tempoRatio = r; }
void Engine::setStreamTrackVolume(Handle h, u32 mask, f32 volume, int frames) {
 auto* s = impl_->sounds->get(h); if (!s || s->type != TypeStrm) return;
 for (int i = 0; i < 8 && mask; ++i, mask >>= 1) if (mask & 1) s->strm.track[i].volume.setTarget(clamp(volume, 0.0f, 1.0f), std::max(0, frames));
}
int Engine::reserveHardwareVoice() {
 auto& e = *impl_;
 for (int i = e.hwVoices - 1; i >= 0; --i) if (e.hwOwner[i] == -1) { e.hwOwner[i] = -2; return i; }
 return -1;
}
void Engine::releaseHardwareVoice(int v) { if (v >= 0 && v < VoiceMax && impl_->hwOwner[v] == -2) impl_->hwOwner[v] = -1; }
bool Engine::prepared(Handle h) const { auto* s = impl_->sounds->get(h); return s && impl_->sounds->prepared(*s); }
u32 Engine::playingSamples(Handle) const { return 0; }
u32 Engine::seqTick(Handle h) const { auto* s = impl_->sounds ? impl_->sounds->get(h) : nullptr; return s && s->type == TypeSeq ? s->seq.tickCounter : 0; }
void Engine::setPlayerVolume(u32 player, f32 v) { if (impl_->sounds && player < impl_->sounds->players.size()) impl_->sounds->players[player].volume = clamp(v, 0.0f, 1.0f); }
void Engine::stopPlayer(u32 player, int frames) { if (impl_->sounds) for (auto& s : impl_->sounds->sound) if (s.used && s.playerId == player) impl_->sounds->stop(s, frames); }
void Engine::pausePlayer(u32 player, bool p, int frames) { if (impl_->sounds) for (auto& s : impl_->sounds->sound) if (s.used && s.playerId == player) impl_->sounds->pause(s, p, frames); }
int Engine::playingCount(u32 player) const { return impl_->sounds ? impl_->sounds->countPlayer(player) : 0; }
void Engine::setMasterVolume(f32 v) { impl_->masterVolume = clamp(v, 0.0f, 1.0f); }
void Engine::updateGameFrame(bool advanceHold) { impl_->gameFrame(advanceHold); }
void Engine::updateSoundFrame() { impl_->soundFrame(); }
void setObserver(Engine& engine, EngineObserver* observer) { engine.impl_->observer = observer; }
u32 soundFrameCount(const Engine& engine) { return engine.impl_->soundFrames; }
int testAllocVoice(Engine& engine, int channels, int priority) {
 auto& e = *engine.impl_;
 Voice* v = e.allocVoice(channels, priority, nullptr);
 if (!v) return -1;
 v->starting = true; v->wave.rate = 32000;
 return int(v - e.voices);
}
void testReleaseVoice(Engine& engine, int v) { auto& e = *engine.impl_; if (v >= 0 && v < VoiceMax && e.voices[v].active) e.voiceSetPriority(e.voices[v], VoicePriorityRelease); }
void testFreeVoice(Engine& engine, int v) { auto& e = *engine.impl_; if (v >= 0 && v < VoiceMax && e.voices[v].active) { e.voiceStopHw(e.voices[v]); e.voiceFree(e.voices[v]); } }
int testVoicePriority(const Engine& engine, int v) { const auto& e = *engine.impl_; return v >= 0 && v < VoiceMax && e.voices[v].active ? e.voices[v].priority : -1; }

bool parseRwav(const u8* rwav, u32 size, WaveData& out, bool checkData) { return readRwav(rwav, size, out, checkData); }

bool parseBrstm(const u8* file, u32 size, BrstmInfo& o) {
 if (size < 0x40 || std::memcmp(file, "RSTM", 4)) return false;
 const u32 head = be32(file + 0x10), headSize = be32(file + 0x14);
 o.adpcOffset = be32(file + 0x18); o.adpcSize = be32(file + 0x1C);
 const u32 data = be32(file + 0x20);
 if (head + headSize > size || headSize < 0x40) return false;
 const u8* base = file + head + 8;
 auto ref = [&](u32 at) -> const u8* { const u32 v = be32(file + head + at + 4); return v && v + 8 < headSize ? base + v : nullptr; };
 const u8* info = ref(8);
 if (!info) return false;
 o.format = info[0]; o.loop = info[1]; o.channels = std::min<u8>(info[2], 8);
 o.rate = (u32(info[3]) << 16) + be16(info + 4);
 o.loopStart = be32(info + 8); o.loopEnd = be32(info + 12);
 o.blocks = be32(info + 0x14); o.blockSize = be32(info + 0x18); o.blockSamples = be32(info + 0x1C);
 o.lastBlockSize = be32(info + 0x20); o.lastBlockSamples = be32(info + 0x24); o.lastBlockPadded = be32(info + 0x28);
 if (data + 12 > size) return false;
 o.audioOffset = data + 8 + be32(file + data + 8);
 o.trackCount = 0;
 if (const u8* tracks = ref(0x10)) {
  const int count = tracks[0], type = tracks[1];
  for (int t = 0; t < count && t < 8; ++t) {
   const u8* tr = base + be32(tracks + 4 + t * 8 + 4);
   StreamTrack st;
   if (type == 0) { st.channels = std::min<u8>(tr[0], 2); for (int c = 0; c < st.channels; ++c) st.channel[c] = tr[1 + c]; }
   else { st.volume = tr[0]; st.pan = tr[1]; st.channels = std::min<u8>(tr[8], 2); for (int c = 0; c < st.channels; ++c) st.channel[c] = tr[9 + c]; }
   o.track[o.trackCount++] = st;
  }
 }
 if (const u8* chans = ref(0x18)) {
  for (int c = 0; c < o.channels; ++c) {
   const u8* ci = base + be32(chans + 4 + c * 8 + 4);
   const u32 a = be32(ci + 4); if (!a || o.format != 2) continue;
   const u8* p = base + a;
   for (int i = 0; i < 16; ++i) o.adpcm[c].coef[i] = bes16(p + i * 2);
   o.adpcm[c].ps = be16(p + 34); o.adpcm[c].yn1 = bes16(p + 36); o.adpcm[c].yn2 = bes16(p + 38);
   o.loopParam[c].ps = be16(p + 40); o.loopParam[c].yn1 = bes16(p + 42); o.loopParam[c].yn2 = bes16(p + 44);
  }
 }
 if (!o.trackCount) { o.trackCount = 1; o.track[0].channels = u8(std::min<int>(o.channels, 2)); o.track[0].channel[0] = 0; o.track[0].channel[1] = 1; }
 return o.channels && o.blocks && o.blockSize && o.blockSamples && o.format <= 2;
}

const EngineStats& Engine::stats() const {
 auto& e = *impl_;
 e.stats.soundsActive = 0; if (e.sounds) for (auto& s : e.sounds->sound) e.stats.soundsActive += s.used;
 e.stats.voicesActive = 0; for (auto& v : e.voices) e.stats.voicesActive += v.active;
 return e.stats;
}
}
