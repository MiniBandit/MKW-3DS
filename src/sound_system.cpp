// 3DS sound system: NDSP backend for the NW4R engine port, audio thread,
// stream/loader I/O thread and the main-thread command queue.
// Hardware rules (docs/HARDWARE.md, Galaxy/BOTW ports): dspfirm.cdc may be
// absent (silent run), DSP_FlushDataCache before queueing, and teardown resets
// channels, drops wave buffer references, then frees memory a frame later.
#include <mkw/sound_system.hpp>
#include <mkw/sound_scene.hpp>
#include <mkw/menu_movie_audio.hpp>
#include <mkw/snd_layout.hpp>
#include <mkw/snd_short_loop.hpp>
#include <3ds.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>
// Allocation diagnostics; none in release builds (MKW_RELEASE).
#ifdef MKW_RELEASE
#define SOUND_DIAG(...) ((void)0)
#else
#define SOUND_DIAG(...) std::fprintf(stderr, __VA_ARGS__)
#endif

namespace mkw {
using namespace snd;
namespace {
constexpr int QueueSize = 1024;           // commands (main -> audio)
constexpr f32 DspFrameMs = 160.0f * 1000.0f / 32728.0f; // NDSP frame (160 samples @ 32728 Hz)
constexpr int MaxBufs = 6;
inline u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3]; }
inline u16 be16(const u8* p) { return u16((p[0] << 8) | p[1]); }
inline s16 bes16(const u8* p) { return s16(be16(p)); }
f32 nowMs() { return f32(double(svcGetSystemTick()) * 1000.0 / SYSCLOCK_ARM11); }

// Decoder history after `samples` samples of a DSP-ADPCM channel
// (AxVoice::CalcOffsetAdpcmParam). Only used for mid-wave start offsets.
void adpcmAdvance(const u8* data, u32 samples, const s16* coef, s16& yn1, s16& yn2) {
 u32 done = 0;
 while (done < samples) {
  const u8 header = *data++; const int scale = 1 << (header & 15), pred = header >> 4;
  const int c1 = coef[pred * 2], c2 = coef[pred * 2 + 1];
  for (int i = 0; i < 14 && done < samples; ++i, ++done) {
   const int nib = (i & 1) ? (data[i >> 1] & 15) : (data[i >> 1] >> 4);
   const int n = nib >= 8 ? nib - 16 : nib;
   int v = ((n * scale) << 11) + 1024 + c1 * yn1 + c2 * yn2; v >>= 11;
   v = std::clamp(v, -32768, 32767); yn2 = yn1; yn1 = s16(v);
  }
  data += 7;
 }
}

// ------------------------------------------------------------ BRSTM feed
// NW4R StrmPlayer data path: blocks of every channel are read together into
// a LINEAR ring by the I/O thread; each hardware voice drains its channel.
// Feeds pumped by the I/O thread (BRSTM streams, long RWAV waves).
class IoFeed : public StreamFeed {
public:
 virtual void openAsync() {}
 virtual bool pending() const { return false; }
 virtual bool pump(FILE* brsar) = 0;
};

class BrstmFeed final : public IoFeed {
public:
 static constexpr int Slots = 4;
 FILE* file = nullptr;
 u8 fmt = 2; u32 sampleRate = 32000; int channelCount = 0; bool looping = false;
 u32 loopStart = 0, totalSamples = 0, blocks = 0, blockSize = 0, blockSamples = 0, lastBlockSize = 0, lastBlockSamples = 0, lastBlockPadded = 0;
 u32 audioOffset = 0;
 std::array<AdpcmParam, 8> params{}; std::array<AdpcmLoop, 8> loopParams{};
 std::vector<StreamTrack> trackList;
 std::vector<u8> adpc; // per block per channel: yn1, yn2 (BE s16)
 // Ring: slot s holds block `slotBlock[s]` for all channels.
 u8* ring = nullptr; u32 slotBytes = 0;
 struct Slot { int block = -1; bool ready = false; u32 samples = 0, startFrame = 0; int consumers = 0; bool context = false; std::array<AdpcmLoop, 8> ctx{}; };
 std::array<Slot, Slots> slot{};
 // Read side (I/O thread): next block to load and the order counter.
 int nextLoadBlock = 0; u32 loadSeq = 0; u32 firstBlockOffset = 0; bool reachedEnd = false; bool loopedOnce = false;
 // Play side (audio thread): per channel the next sequence to hand out.
 std::array<u32, 8> playSeq{}; std::array<u32, Slots> slotSeq{};
 LightLock lock;
 std::atomic<bool> failed{false};
 std::atomic<int> openState{0}; // 0 pending, 1 header read, -1 failed
 std::string path; u32 startAt = 0;

 BrstmFeed() { LightLock_Init(&lock); }
 ~BrstmFeed() override { if (file) std::fclose(file); if (ring) linearFree(ring); }
 u8 format() const override { return fmt; }
 u32 rate() const override { return sampleRate; }
 int channels() const override { return channelCount; }
 int tracks() const override { return int(trackList.size()); }
 StreamTrack track(int i) const override { return i >= 0 && i < int(trackList.size()) ? trackList[i] : StreamTrack{}; }
 const AdpcmParam& adpcm(int c) const override { return params[std::clamp(c, 0, 7)]; }

 int state() const override {
  const int o = openState.load();
  if (o <= 0) return o;
  if (failed.load()) return -1;
  LightLock_Lock(const_cast<LightLock*>(&lock)); const bool primed = loadSeq > 0 || reachedEnd; LightLock_Unlock(const_cast<LightLock*>(&lock));
  return primed ? 1 : 0;
 }
 // I/O thread: header, then the first block (StrmPlayer::LoadHeader task).
 void openAsync() override { openState.store(open(path.c_str(), startAt) ? 1 : -1); }
 bool pending() const override { return openState.load() == 0; }
 bool open(const char* path, u32 startSample) {
  file = std::fopen(path, "rb");
  if (!file) return false;
  std::setvbuf(file, nullptr, _IONBF, 0);
  // Header and HEAD block (parsed by the host-tested snd::parseBrstm).
  u8 h[0x40];
  if (std::fread(h, 1, sizeof h, file) != sizeof h || std::memcmp(h, "RSTM", 4)) return false;
  const u32 headEnd = be32(h + 0x10) + be32(h + 0x14), dataOffset = be32(h + 0x20);
  if (headEnd > 0x20000 || dataOffset + 12 > 0x40000) return false;
  std::vector<u8> head(std::max(headEnd, dataOffset + 12));
  if (std::fseek(file, 0, SEEK_SET) || std::fread(head.data(), 1, head.size(), file) != head.size()) return false;
  BrstmInfo info;
  if (!parseBrstm(head.data(), u32(head.size()), info)) return false;
  fmt = info.format; looping = info.loop; channelCount = info.channels; sampleRate = info.rate;
  loopStart = info.loopStart; totalSamples = info.loopEnd; audioOffset = info.audioOffset;
  blocks = info.blocks; blockSize = info.blockSize; blockSamples = info.blockSamples;
  lastBlockSize = info.lastBlockSize; lastBlockSamples = info.lastBlockSamples; lastBlockPadded = info.lastBlockPadded;
  for (int c = 0; c < channelCount; ++c) { params[c] = info.adpcm[c]; loopParams[c] = info.loopParam[c]; }
  for (int t = 0; t < info.trackCount; ++t) trackList.push_back(info.track[t]);
  if (fmt == 2 && info.adpcSize > 8) {
   adpc.resize(info.adpcSize - 8);
   if (std::fseek(file, long(info.adpcOffset + 8), SEEK_SET) || std::fread(adpc.data(), 1, adpc.size(), file) != adpc.size()) adpc.clear();
  }
  slotBytes = (blockSize + 31) & ~31u;
  ring = static_cast<u8*>(linearAlloc(size_t(slotBytes) * channelCount * Slots));
  if (!ring) return false;
  if (startSample >= totalSamples) startSample = 0;
  nextLoadBlock = int(startSample / blockSamples);
  firstBlockOffset = startSample % blockSamples;
  if (fmt == 2) firstBlockOffset = firstBlockOffset / 14 * 14;
  return true;
 }
 u32 blockSamplesOf(int b) const { return u32(b) + 1 == blocks ? lastBlockSamples : blockSamples; }
 // I/O thread: fill free slots in play order. Returns true if it read anything.
 bool pump(FILE*) override {
  if (openState.load() != 1 || failed.load() || reachedEnd) return false;
  bool did = false;
  for (int pass = 0; pass < Slots; ++pass) {
   int free = -1;
   LightLock_Lock(&lock);
   for (int s = 0; s < Slots; ++s) if (!slot[s].ready && slot[s].consumers == 0) { free = s; break; }
   LightLock_Unlock(&lock);
   if (free < 0 || reachedEnd) break;
   const int b = nextLoadBlock;
   const u32 size = u32(b) + 1 == blocks ? lastBlockSize : blockSize;
   const u32 stride = u32(b) + 1 == blocks ? lastBlockPadded : blockSize;
   const u32 at = audioOffset + u32(b) * blockSize * channelCount;
   Slot& s = slot[free];
   bool ok = !std::fseek(file, long(at), SEEK_SET);
   for (int c = 0; ok && c < channelCount; ++c) {
    u8* dst = ring + (size_t(free) * channelCount + c) * slotBytes;
    if (c) ok = !std::fseek(file, long(at + stride * c), SEEK_SET);
    ok = ok && std::fread(dst, 1, size, file) == size;
    if (ok && fmt == 1) for (u32 i = 0; i + 1 < size; i += 2) std::swap(dst[i], dst[i + 1]); // BE PCM16 -> LE
   }
   if (!ok) { failed.store(true); return did; }
   s.samples = blockSamplesOf(b); s.startFrame = 0; s.context = false;
   const bool first = loadSeq == 0, seam = looping && loopedOnce && u32(b) * blockSamples == loopStart;
   if (fmt == 2 && (first || seam)) {
    // Context at a block start: ADPC table (yn1, yn2 per channel); a loop seam uses HEAD's loop context.
    s.context = true;
    for (int c = 0; c < channelCount; ++c) {
     AdpcmLoop& x = s.ctx[c];
     const u8* blockData = ring + (size_t(free) * channelCount + c) * slotBytes;
     x.ps = blockData[0];
     if (seam) { x = loopParams[c]; x.ps = blockData[0]; }
     else if (b == 0) { x.yn1 = params[c].yn1; x.yn2 = params[c].yn2; }
     else if (!adpc.empty()) { const size_t at4 = (size_t(b) * channelCount + c) * 4; if (at4 + 4 <= adpc.size()) { x.yn1 = bes16(&adpc[at4]); x.yn2 = bes16(&adpc[at4 + 2]); } }
    }
   }
   if (first && firstBlockOffset) {
    s.startFrame = firstBlockOffset;
    if (fmt == 2) for (int c = 0; c < channelCount; ++c) {
     const u8* blockData = ring + (size_t(free) * channelCount + c) * slotBytes;
     adpcmAdvance(blockData, firstBlockOffset, params[c].coef, s.ctx[c].yn1, s.ctx[c].yn2);
     s.ctx[c].ps = blockData[firstBlockOffset / 14 * 8];
    }
   }
   DSP_FlushDataCache(ring + size_t(free) * channelCount * slotBytes, slotBytes * channelCount);
   LightLock_Lock(&lock);
   slotSeq[free] = loadSeq++; s.block = b; s.ready = true; s.consumers = channelCount;
   LightLock_Unlock(&lock);
   did = true;
   if (u32(b) + 1 >= blocks) {
    if (looping) { nextLoadBlock = int(loopStart / blockSamples); loopedOnce = true; }
    else reachedEnd = true;
   } else nextLoadBlock = b + 1;
  }
  return did;
 }
 bool next(int channel, StreamBlock& out) override {
  if (channel < 0 || channel >= channelCount) return false;
  LightLock_Lock(&lock);
  bool found = false;
  for (int s = 0; s < Slots; ++s) {
   if (!slot[s].ready || slotSeq[s] != playSeq[channel]) continue;
   const u32 offset = slot[s].startFrame;
   const u8* data = ring + (size_t(s) * channelCount + channel) * slotBytes;
   out.data = data + (fmt == 2 ? offset / 14 * 8 : offset * (fmt == 1 ? 2 : 1));
   out.samples = slot[s].samples - offset;
   out.context = slot[s].context; out.adpcm = slot[s].ctx[channel];
   ++playSeq[channel]; found = true; break;
  }
  LightLock_Unlock(&lock);
  return found;
 }
 void consumed(int channel, const u8* data) override {
  (void)channel;
  LightLock_Lock(&lock);
  for (int s = 0; s < Slots; ++s) {
   const u8* lo = ring + size_t(s) * channelCount * slotBytes, *hi = lo + size_t(channelCount) * slotBytes;
   if (data >= lo && data < hi && slot[s].ready && --slot[s].consumers == 0) { slot[s].ready = false; slot[s].block = -1; }
  }
  LightLock_Unlock(&lock);
 }
 bool ended() const override {
  if (!reachedEnd && !failed.load()) return false;
  for (const auto& s : slot) if (s.ready) return false;
  return true;
 }
 bool drained(int channel) const override {
  if (channel < 0 || channel >= channelCount) return true;
  LightLock_Lock(const_cast<LightLock*>(&lock));
  const bool done = (reachedEnd || failed.load()) && playSeq[channel] == loadSeq;
  LightLock_Unlock(const_cast<LightLock*>(&lock));
  return done;
 }
};

// ------------------------------------------------------------ wave feed
// One channel of a long RWAV (menu BGM stems, title wave): its resident head
// plays first while the I/O thread streams the rest from the BRSAR in
// DSP-ADPCM chunks; loops restart at the loop start (or its bridge frame).
class WaveFeed final : public IoFeed {
public:
 static constexpr int Slots = 3;
 static constexpr u32 ChunkBytes = 16 * 1024, ChunkSamples = ChunkBytes / 8 * 14;
 WaveFeed(const WaveData& w, int channel, const u8* bridgeFrame) {
  const auto& c = w.channel[channel];
  param = c.adpcm; loopCtx = c.loopParam; bridge = c.bridge; bridgeData = bridgeFrame;
  rate_ = w.rate; total = w.loopEnd; loop = w.loop; loopStart = w.loopStart; fileOffset = c.fileOffset;
  ring = static_cast<u8*>(linearMemAlign(size_t(ChunkBytes) * Slots, 32));
  LightLock_Init(&lock);
  // The resident head is the first block.
  const u32 head = std::min(w.headSamples, total);
  if (head) push({c.data, head, true, AdpcmLoop{param.ps, param.yn1, param.yn2}, -1});
  pos = head; first = head == 0;
  if (pos >= total) wrap();
 }
 ~WaveFeed() override { if (ring) linearFree(ring); }
 u8 format() const override { return 2; }
 u32 rate() const override { return rate_; }
 int channels() const override { return 1; }
 int tracks() const override { return 1; }
 StreamTrack track(int) const override { StreamTrack t; t.channels = 1; return t; }
 const AdpcmParam& adpcm(int) const override { return param; }
 int state() const override { return ring ? 1 : -1; }
 bool ended() const override { LightLock_Lock(const_cast<LightLock*>(&lock)); const bool e = done && count == 0; LightLock_Unlock(const_cast<LightLock*>(&lock)); return e; }
 bool next(int, StreamBlock& out) override {
  LightLock_Lock(&lock);
  bool ok = false;
  if (count) { const Block& b = fifo[headIndex]; out.data = b.data; out.samples = b.samples; out.context = b.context; out.adpcm = b.ctx; headIndex = (headIndex + 1) % Fifo; --count; ok = true; }
  LightLock_Unlock(&lock);
  return ok;
 }
 void consumed(int, const u8* data) override {
  LightLock_Lock(&lock);
  for (int s = 0; s < Slots; ++s) if (data >= ring + s * ChunkBytes && data < ring + (s + 1) * ChunkBytes) busy[s] = false;
  LightLock_Unlock(&lock);
 }
 bool pump(FILE* brsar) override {
  bool did = false;
  while (!done && ring) {
   int slot = -1;
   LightLock_Lock(&lock);
   if (count < Fifo - 1) for (int s = 0; s < Slots; ++s) if (!busy[s]) { slot = s; busy[s] = true; break; }
   LightLock_Unlock(&lock);
   if (slot < 0) break;
   const u32 samples = std::min(ChunkSamples, total - pos);
   const u32 bytes = (samples + 13) / 14 * 8;
   u8* dst = ring + slot * ChunkBytes;
   if (std::fseek(brsar, long(fileOffset + pos / 14 * 8), SEEK_SET) || std::fread(dst, 1, bytes, brsar) != bytes) { done = true; break; }
   DSP_FlushDataCache(dst, bytes);
   Block b{dst, samples, seamContext, seamCtx, slot};
   if (first) { b.context = true; b.ctx = AdpcmLoop{param.ps, param.yn1, param.yn2}; first = false; }
   seamContext = false;
   LightLock_Lock(&lock); push(b); LightLock_Unlock(&lock);
   pos += samples; did = true;
   if (pos >= total) wrap();
  }
  return did;
 }
private:
 struct Block { const u8* data; u32 samples; bool context; AdpcmLoop ctx; int slot; };
 static constexpr int Fifo = 8;
 void push(const Block& b) { fifo[(headIndex + count) % Fifo] = b; ++count; }
 void wrap() {
  if (!loop) { done = true; return; }
  if (bridge && bridgeData) {
   LightLock_Lock(&lock);
   push({bridgeData, bridge->samples, true, AdpcmLoop{bridgeData[0], loopCtx.yn1, loopCtx.yn2}, -1});
   LightLock_Unlock(&lock);
   pos = bridge->bodyStart; seamContext = true; seamCtx = AdpcmLoop{bridge->bodyPs, bridge->bodyYn1, bridge->bodyYn2};
  } else { pos = loopStart / 14 * 14; seamContext = true; seamCtx = loopCtx; }
  if (pos >= total) done = true;
 }
 AdpcmParam param; AdpcmLoop loopCtx; const LoopBridge* bridge = nullptr; const u8* bridgeData = nullptr;
 u32 rate_ = 32000, total = 0, loopStart = 0, fileOffset = 0, pos = 0; bool loop = false, done = false, first = false;
 bool seamContext = false; AdpcmLoop seamCtx{};
 u8* ring = nullptr; bool busy[Slots]{};
 Block fifo[Fifo]{}; int headIndex = 0, count = 0;
 LightLock lock;
};

// ------------------------------------------------------------ NDSP backend
class NdspHardware final : public Hardware {
public:
 // I/O registration for wave feeds (set by SoundSystem::Impl).
 void (*addFeed)(void* ctx, IoFeed* feed) = nullptr; void (*retireFeed)(void* ctx, IoFeed* feed) = nullptr; void* feedCtx = nullptr;
 struct Chan {
  enum Mode : u8 { Idle, Wave, Stream };
  Mode mode = Idle; bool paused = false, pendingStart = false, reserved = false, manualLoop = false;
  std::array<ndspWaveBuf, MaxBufs> buf{}; std::array<ndspAdpcmData, MaxBufs> ctx{};
  int bufCount = 0; int lastBuf = -1;
  // manual (bridge) loop
  const u8* bridge = nullptr; u32 bridgeSamples = 0; const u8* body = nullptr; u32 bodySamples = 0; ndspAdpcmData bodyCtx{};
  // stream
  StreamFeed* feed = nullptr; int feedChannel = 0; IoFeed* ownFeed = nullptr;
  f32 rate = -1, left = -1, right = -1, lpf = -1;
 };
 std::array<Chan, VoiceMax> ch{};
 u8* bridgePool = nullptr; u32 bridgeUsed = 0;
 static constexpr u32 BridgePoolBytes = 4096;
 struct PcmLoop {
  std::vector<u8> encoded;
  AdpcmParam adpcm; AdpcmLoop loop;
  u32 first = 0, end = 0;
  const s16* pcm = nullptr;
 };
 std::vector<PcmLoop> pcmLoops;
 s16* pcmLoopPool = nullptr; u32 pcmLoopUsed = 0;
 // All seven matching waves in the retail archive occupy 10,112 bytes.
 static constexpr u32 PcmLoopPoolBytes = 16384;
 u32 underruns = 0;

 NdspHardware() { bridgePool = static_cast<u8*>(linearAlloc(BridgePoolBytes)); }
 ~NdspHardware() override { for (int v = 0; v < VoiceMax; ++v) hardStop(v); if (bridgePool) linearFree(bridgePool); if (pcmLoopPool) linearFree(pcmLoopPool); }
 int voiceCount() const override { return VoiceMax; }

 const s16* shortLoopPcm(const WaveData& w, int channel) {
  if (!shortAdpcmLoop(w)) return nullptr;
  const auto& c = w.channel[channel];
  // Compare the content, not a bank address: banks can be unloaded and
  // allocated again between races. Cached PCM remains immutable while
  // any DSP channel may still reference it.
  for (const auto& old : pcmLoops)
   if (old.first == w.loopStart && old.end == w.loopEnd && old.encoded.size() == c.bytes &&
       old.adpcm.ps == c.adpcm.ps && old.adpcm.yn1 == c.adpcm.yn1 && old.adpcm.yn2 == c.adpcm.yn2 &&
       old.loop.ps == c.loopParam.ps && old.loop.yn1 == c.loopParam.yn1 && old.loop.yn2 == c.loopParam.yn2 &&
       !std::memcmp(old.adpcm.coef, c.adpcm.coef, sizeof c.adpcm.coef) &&
       !std::memcmp(old.encoded.data(), c.data, c.bytes)) return old.pcm;
  const u32 samples = shortLoopSamples(w);
  if ((pcmLoopUsed + samples) * sizeof(s16) > PcmLoopPoolBytes) return nullptr;
  if (!pcmLoopPool) pcmLoopPool = static_cast<s16*>(linearMemAlign(PcmLoopPoolBytes, 32));
  if (!pcmLoopPool) return nullptr;
  s16* pcm = pcmLoopPool + pcmLoopUsed;
  if (!decodeShortLoop(w, channel, pcm)) return nullptr;
  DSP_FlushDataCache(pcm, samples * sizeof(s16));
  pcmLoops.push_back({std::vector<u8>(c.data, c.data + c.bytes), c.adpcm, c.loopParam, w.loopStart, w.loopEnd, pcm});
  pcmLoopUsed += samples;
  return pcm;
 }

 const u8* bridgeData(const LoopBridge* b) {
  // Copy each distinct bridge frame once into DSP-visible memory.
  struct Entry { const LoopBridge* key; const u8* data; };
  static std::vector<Entry> map;
  for (auto& e : map) if (e.key == b) return e.data;
  if (!bridgePool || bridgeUsed + 8 > BridgePoolBytes) return nullptr;
  u8* d = bridgePool + bridgeUsed; bridgeUsed += 8;
  std::memcpy(d, b->frame, 8); DSP_FlushDataCache(d, 8);
  map.push_back({b, d});
  return d;
 }
 void hardStop(int v) {
  ndspChnWaveBufClear(v); ndspChnReset(v);
  Chan& c = ch[v];
  if (c.ownFeed) { if (retireFeed) retireFeed(feedCtx, c.ownFeed); else delete c.ownFeed; c.ownFeed = nullptr; }
  c.mode = Chan::Idle; c.bufCount = 0; c.lastBuf = -1; c.feed = nullptr; c.manualLoop = false; c.pendingStart = false; c.paused = false;
  c.rate = c.left = c.right = c.lpf = -1;
  for (auto& b : c.buf) b = ndspWaveBuf{};
 }
 void setupChannel(int v, u8 format, const s16* coef, u32 rate) {
  ndspChnWaveBufClear(v); ndspChnReset(v);
  ndspChnSetInterp(v, NDSP_INTERP_POLYPHASE);
  ndspChnSetFormat(v, format == 2 ? NDSP_FORMAT_MONO_ADPCM : format == 1 ? NDSP_FORMAT_MONO_PCM16 : NDSP_FORMAT_MONO_PCM8);
  if (format == 2 && coef) { u16 c16[16]; for (int i = 0; i < 16; ++i) c16[i] = u16(coef[i]); ndspChnSetAdpcmCoefs(v, c16); }
  ndspChnSetRate(v, f32(rate));
  float mix[12] = {}; ndspChnSetMix(v, mix);
  ndspChnSetPaused(v, true); // unpaused in commit() once its first params are set
 }
 static u32 bytesFor(u8 format, u32 samples) { return format == 2 ? (samples + 13) / 14 * 8 : format == 1 ? samples * 2 : samples; }
 ndspWaveBuf* queue(int v, const u8* data, u32 samples, bool loop, const ndspAdpcmData* ctx) {
  Chan& c = ch[v];
  int slot = -1;
  for (int i = 0; i < MaxBufs; ++i) if (c.buf[i].status == NDSP_WBUF_FREE || c.buf[i].status == NDSP_WBUF_DONE) { if (i >= c.bufCount || c.buf[i].status == NDSP_WBUF_DONE || c.buf[i].status == NDSP_WBUF_FREE) { slot = i; break; } }
  if (slot < 0 || !samples) return nullptr;
  ndspWaveBuf& b = c.buf[slot]; b = ndspWaveBuf{};
  b.data_vaddr = data; b.nsamples = samples; b.looping = loop;
  if (ctx) { c.ctx[slot] = *ctx; b.adpcm_data = &c.ctx[slot]; }
  c.bufCount = std::max(c.bufCount, slot + 1); c.lastBuf = slot;
  ndspChnWaveBufAdd(v, &b);
  return &b;
 }
 void start(int v, const WaveData& w, int channel, u32 offset) override {
  Chan& c = ch[v]; if (c.reserved) return;
  hardStop(v);
  const auto& wc = w.channel[channel];
  if (w.streamed) {
   auto* feed = new WaveFeed(w, channel, wc.bridge ? bridgeData(wc.bridge) : nullptr);
   if (addFeed) addFeed(feedCtx, feed);
   setupChannel(v, 2, wc.adpcm.coef, w.rate);
   c.mode = Chan::Stream; c.feed = feed; c.ownFeed = feed; c.feedChannel = 0; c.pendingStart = true;
   refillStream(v);
   return;
  }
  if (const s16* pcm = shortLoopPcm(w, channel)) {
   // SE_RC_GOAL's original oscillator is a 43-sample loop. Its old
   // bridge/body queue emptied several times within one DSP frame.
   // Keep its exact decoded samples, loop length, rate and sequence pitch.
   setupChannel(v, 1, nullptr, w.rate);
   c.mode = Chan::Wave; c.pendingStart = true;
   if (offset >= w.loopEnd) offset = 0;
   offset = offset / 14 * 14;
   queue(v, reinterpret_cast<const u8*>(pcm + offset), w.loopEnd - offset, false, nullptr);
   queue(v, reinterpret_cast<const u8*>(pcm + shortLoopOffset(w)), w.loopEnd - w.loopStart, true, nullptr);
   return;
  }
  setupChannel(v, w.format, wc.adpcm.coef, w.rate);
  c.mode = Chan::Wave; c.pendingStart = true;
  const u8* data = wc.data; const u32 end = w.loopEnd;
  ndspAdpcmData start{wc.adpcm.ps, wc.adpcm.yn1, wc.adpcm.yn2};
  if (offset >= end) offset = 0;
  if (offset) {
   if (w.format == 2) {
    offset = offset / 14 * 14; s16 yn1 = wc.adpcm.yn1, yn2 = wc.adpcm.yn2;
    adpcmAdvance(data, offset, wc.adpcm.coef, yn1, yn2);
    start = {data[offset / 14 * 8], yn1, yn2};
   }
  }
  auto at = [&](u32 sample) { return data + (w.format == 2 ? sample / 14 * 8 : sample * (w.format == 1 ? 2 : 1)); };
  if (!w.loop) { queue(v, at(offset), end - offset, false, w.format == 2 ? &start : nullptr); return; }
  const ndspAdpcmData loopCtx{wc.loopParam.ps, wc.loopParam.yn1, wc.loopParam.yn2};
  if (w.format == 2 && w.loopStart % 14) {
   // Mid-frame loop start: retail data to the next frame, then bridge + body forever.
   const LoopBridge* br = wc.bridge;
   const u32 bodyStart = br ? br->bodyStart : (w.loopStart + 13) / 14 * 14;
   if (offset < bodyStart) queue(v, at(offset), bodyStart - offset, false, &start);
   queue(v, at(bodyStart), end - bodyStart, false, offset < bodyStart ? nullptr : &start);
   c.manualLoop = true;
   c.body = at(bodyStart); c.bodySamples = end - bodyStart;
   c.bodyCtx = br ? ndspAdpcmData{br->bodyPs, br->bodyYn1, br->bodyYn2} : ndspAdpcmData{data[bodyStart / 14 * 8], wc.loopParam.yn1, wc.loopParam.yn2};
   c.bridge = br ? bridgeData(br) : nullptr; c.bridgeSamples = br ? br->samples : 0;
   refillManual(v);
   return;
  }
  if (offset < w.loopStart) { queue(v, at(offset), w.loopStart - offset, false, w.format == 2 ? &start : nullptr); queue(v, at(w.loopStart), end - w.loopStart, true, w.format == 2 ? &loopCtx : nullptr); }
  else { queue(v, at(offset), end - offset, false, w.format == 2 ? &start : nullptr); queue(v, at(w.loopStart), end - w.loopStart, true, w.format == 2 ? &loopCtx : nullptr); }
 }
 void refillManual(int v) {
  Chan& c = ch[v];
  if (!c.manualLoop) return;
  int queued = 0;
  for (int i = 0; i < c.bufCount; ++i) if (c.buf[i].status == NDSP_WBUF_QUEUED || c.buf[i].status == NDSP_WBUF_PLAYING) ++queued;
  // Keep at least two loop iterations queued (bridge + body pairs).
  while (queued + 2 <= MaxBufs && queued < 4) {
   if (c.bridge && c.bridgeSamples) { if (!queue(v, c.bridge, c.bridgeSamples, false, nullptr)) break; ++queued; }
   if (!queue(v, c.body, c.bodySamples, false, &c.bodyCtx)) break; ++queued;
  }
 }
 void startStream(int v, StreamFeed* feed, int channel) override {
  Chan& c = ch[v]; if (c.reserved) return;
  hardStop(v);
  setupChannel(v, feed->format(), feed->adpcm(channel).coef, feed->rate());
  c.mode = Chan::Stream; c.feed = feed; c.feedChannel = channel; c.pendingStart = true;
  refillStream(v);
 }
 void refillStream(int v) {
  Chan& c = ch[v]; if (!c.feed) return;
  for (int i = 0; i < c.bufCount; ++i) if (c.buf[i].status == NDSP_WBUF_DONE && c.buf[i].data_vaddr) { c.feed->consumed(c.feedChannel, static_cast<const u8*>(c.buf[i].data_vaddr)); c.buf[i].data_vaddr = nullptr; c.buf[i].status = NDSP_WBUF_FREE; }
  int queued = 0; for (int i = 0; i < c.bufCount; ++i) if (c.buf[i].status == NDSP_WBUF_QUEUED || c.buf[i].status == NDSP_WBUF_PLAYING) ++queued;
  if (queued == 0 && !c.pendingStart && !c.feed->drained(c.feedChannel)) ++underruns;
  StreamBlock blk;
  while (queued < 4 && c.feed->next(c.feedChannel, blk)) {
   const ndspAdpcmData ctx{blk.adpcm.ps, blk.adpcm.yn1, blk.adpcm.yn2};
   if (!queue(v, blk.data, blk.samples, false, blk.context ? &ctx : nullptr)) { c.feed->consumed(c.feedChannel, blk.data); break; }
   ++queued;
  }
 }
 void stop(int v) override { if (!ch[v].reserved) { if (ch[v].feed) releaseStreamBufs(v); hardStop(v); } }
 void releaseStreamBufs(int v) {
  Chan& c = ch[v];
  ndspChnWaveBufClear(v);
  for (int i = 0; i < c.bufCount; ++i) if (c.buf[i].data_vaddr && c.buf[i].status != NDSP_WBUF_FREE) { c.feed->consumed(c.feedChannel, static_cast<const u8*>(c.buf[i].data_vaddr)); c.buf[i].data_vaddr = nullptr; }
 }
 void pause(int v, bool p) override { if (!ch[v].reserved) { ch[v].paused = p; if (!ch[v].pendingStart) ndspChnSetPaused(v, p); } }
 void set(int v, f32 rate, f32 left, f32 right, f32 lpfHz) override {
  Chan& c = ch[v]; if (c.reserved || c.mode == Chan::Idle) return;
  if (rate != c.rate) { ndspChnSetRate(v, std::clamp(rate, 1.0f, 192000.0f)); c.rate = rate; }
  if (left != c.left || right != c.right) { float mix[12] = {}; mix[0] = left; mix[1] = right; ndspChnSetMix(v, mix); c.left = left; c.right = right; }
  if (lpfHz != c.lpf) {
   if (lpfHz > 0) { ndspChnIirMonoSetParamsLowPassFilter(v, lpfHz); ndspChnIirMonoSetEnable(v, true); }
   else ndspChnIirMonoSetEnable(v, false);
   c.lpf = lpfHz;
  }
 }
 bool finished(int v) override {
  const Chan& c = ch[v];
  if (c.reserved || c.mode == Chan::Idle) return true;
  if (c.pendingStart || c.manualLoop) return false;
  if (c.mode == Chan::Stream) {
   if (!c.feed->ended()) return false;
   for (int i = 0; i < c.bufCount; ++i) if (c.buf[i].status == NDSP_WBUF_QUEUED || c.buf[i].status == NDSP_WBUF_PLAYING) return false;
   return true;
  }
  for (int i = 0; i < c.bufCount; ++i) if (c.buf[i].looping && c.buf[i].status != NDSP_WBUF_FREE) return false;
  return c.lastBuf < 0 || c.buf[c.lastBuf].status == NDSP_WBUF_DONE || !ndspChnIsPlaying(v);
 }
 void commit() override {
  for (int v = 0; v < VoiceMax; ++v) {
   Chan& c = ch[v]; if (c.reserved || c.mode == Chan::Idle) continue;
   if (c.mode == Chan::Stream) refillStream(v); else if (c.manualLoop) refillManual(v);
   if (c.pendingStart) { c.pendingStart = false; if (!c.paused) ndspChnSetPaused(v, false); }
  }
 }
 void reserveChannel(int v) { hardStop(v); ch[v].reserved = true; }
 void unreserve(int v) { if (v >= 0 && v < VoiceMax) { hardStop(v); ch[v].reserved = false; } }
};

// SPSC ring of commands.
template <class T, int N> struct Ring {
 std::array<T, N> items{}; std::atomic<u32> head{0}, tail{0};
 bool push(const T& t) { const u32 h = head.load(std::memory_order_relaxed); if (h - tail.load(std::memory_order_acquire) >= N) return false; items[h % N] = t; head.store(h + 1, std::memory_order_release); return true; }
 bool pop(T& t) { const u32 tl = tail.load(std::memory_order_relaxed); if (tl == head.load(std::memory_order_acquire)) return false; t = items[tl % N]; tail.store(tl + 1, std::memory_order_release); return true; }
};
}

// ======================================================================
struct SoundSystem::Impl {
 std::string root;
 Archive archive; Engine engine; std::unique_ptr<NdspHardware> hw;
 FILE* brsar = nullptr;
 Thread audioThread = nullptr, ioThread = nullptr;
 LightEvent frameEvent, ioEvent;
 std::atomic<bool> quit{false};
 Ring<SoundCommand, QueueSize> commands;
 std::array<Handle, 512> slots{};
 // Main thread: sequence of the last Start/Hold/Prepare per tracked slot.
 // Audio thread: attached bits, then the last sequence it handled (release).
 std::array<u32, SoundSystem::TrackedSlots> slotPosted{};
 u32 postSeq = 0;
 std::atomic<u32> attachedBits{0}, handledSeq{0};
 u32 runSeq = 0;
 SoundLayer* layer = nullptr; std::atomic<SoundLayer*> pendingLayer{nullptr}; std::atomic<bool> layerChanged{false};
 // ---- residency
 // mask: per RWAR entry, 1 = keep resident (empty = every wave), set by loadScene.
 struct File { std::unique_ptr<LoadedFile> data; int refs = 0; bool ready = false; bool wanted = false; unsigned loadFailures = 0; f32 retryAfter = 0; std::vector<u8> mask; };
 std::vector<File> files;
 LightLock fileLock;               // guards `files` wanted/refs between main and I/O
 std::vector<u32> sceneFiles;
 std::atomic<u32> sceneGeneration{0};
 std::atomic<bool> sceneLoaded{true};
 Ring<u32, 2048> readyFiles;        // I/O -> audio: file id loaded
 struct Drop { u32 file; f32 due; };
 Ring<Drop, 2048> dropFiles;        // main -> audio: file id to unload after `due` (audio clock ms)
 std::vector<Drop> delayedDrops;
 struct Retired { void* linear; u32 frame; };
 std::deque<Retired> retired;
 std::atomic<u32> waveBytes{0};
 // ---- wave cache (audio thread owns blocks; the I/O thread fills them)
 struct CacheJob { u8* dst; u32 offset, size, key; };
 struct CacheResult { u32 key; bool okay; };
 Ring<CacheJob, 256> cacheJobs;     // audio -> I/O
 Ring<CacheResult, 256> cacheDone;   // A failed read is never a ready silent wave.
 std::atomic<u32> wantArena{0};
 struct Cache final : WaveResidency {
  Impl& s;
  f32 retryAfter = 0;
  struct Block { u32 key, at, size; u32 lastUse; bool ready, orphan; };
  std::vector<Block> blocks;        // sorted by `at`
  std::unordered_map<u32, int> pins;
  u8* arena = nullptr; u32 arenaBytes = 0;
  u32 loads = 0, evictions = 0;
  explicit Cache(Impl& impl) : s(impl) {}
  static u32 key(u32 file, u32 wave) { return file << 16 | wave; }
  Block* find(u32 k) { for (auto& b : blocks) if (b.key == k) return &b; return nullptr; }
  u32 used() const { u32 n = 0; for (auto& b : blocks) n += b.size; return n; }
  bool acquire(u32 file, u32 wave) override { ++pins[key(file, wave)]; return ready(file, wave); }
  void release(u32 file, u32 wave) override {
   auto it = pins.find(key(file, wave)); if (it == pins.end()) return;
   if (--it->second <= 0) pins.erase(it);
   if (Block* b = find(key(file, wave))) b->lastUse = ndspGetFrameCount();
  }
  bool ready(u32 file, u32 wave) override {
   const LoadedFile* f = s.engine.fileData(file);
   if (!f) { LightLock_Lock(&s.fileLock); const bool loading = file < s.files.size() && s.files[file].wanted; LightLock_Unlock(&s.fileLock); return !loading; }
   if (wave >= f->waveOffset.size() || f->waveOffset[wave] != InvalidId) return true;
   for (const auto& st : f->streamed) if (st.index == wave) return true;
   if (f->cached[wave]) { if (Block* b = find(key(file, wave))) b->lastUse = ndspGetFrameCount(); return true; }
   request(file, wave);
   return false;
  }
  void unmap(const Block& b) { if (LoadedFile* f = s.engine.fileData(b.key >> 16)) if ((b.key & 0xFFFF) < f->cached.size()) f->cached[b.key & 0xFFFF] = nullptr; }
  // Unpinned, loaded, and not read by the DSP for two frames.
  bool evictable(const Block& b) const { return b.ready && !pins.count(b.key) && ndspGetFrameCount() - b.lastUse >= 2; }
  bool evictOldest() {
   int best = -1;
   for (int i = 0; i < int(blocks.size()); ++i) if (evictable(blocks[i]) && (best < 0 || blocks[i].lastUse < blocks[best].lastUse)) best = i;
   if (best < 0) return false;
   unmap(blocks[best]); blocks.erase(blocks.begin() + best); ++evictions; return true;
  }
  int fit(u32 size) const {  // offset of the first gap that holds `size`, or -1
   u32 at = 0;
   for (const auto& b : blocks) { if (b.at - at >= size) return int(at); at = b.at + b.size; }
   return arenaBytes - at >= size ? int(at) : -1;
  }
  void request(u32 file, u32 wave) {
   const u32 k = key(file, wave);
   if (find(k) || !arena) return;
   const WaveEntry* e = s.archive.wave(file, wave);
   const FileEntry* fe = s.archive.file(file);
   if (!e || !fe) return;
   const u32 size = (e->size + 31) & ~31u;
   if (size > arenaBytes) return;
   int at;
   while ((at = fit(size)) < 0) if (!evictOldest()) return;  // retried next frame
   Block b{k, u32(at), size, ndspGetFrameCount(), false, false};
   blocks.insert(std::upper_bound(blocks.begin(), blocks.end(), b, [](const Block& x, const Block& y) { return x.at < y.at; }), b);
   if (!s.cacheJobs.push({arena + at, fe->waveOffset + e->offset, e->size, k})) { blocks.erase(std::find_if(blocks.begin(), blocks.end(), [&](const Block& x) { return x.key == k; })); return; }
   ++loads;
   LightEvent_Signal(&s.ioEvent);
  }
  void done(u32 k,bool okay) {
   Block* b = find(k); if (!b) return;
   if (b->orphan||!okay) { blocks.erase(blocks.begin() + (b - blocks.data())); return; }
   b->ready = true; b->lastUse = ndspGetFrameCount();
   if (LoadedFile* f = s.engine.fileData(k >> 16)) if ((k & 0xFFFF) < f->cached.size()) f->cached[k & 0xFFFF] = arena + b->at;
  }
  // A file leaves the scene: its blocks go (in-flight ones when they land).
  void dropFile(u32 file) {
   for (size_t i = 0; i < blocks.size();) {
    if ((blocks[i].key >> 16) != file) { ++i; continue; }
    if (!blocks[i].ready) { blocks[i].orphan = true; ++i; continue; }
    blocks.erase(blocks.begin() + i);
   }
  }
  // Scene change: resize the arena once nothing in it is pinned or loading.
  void resize(u32 want) {
   if (want == arenaBytes || nowMs() < retryAfter) return;
   while (evictOldest()) {}
   if (!blocks.empty()) return;
   if (arena) {
    s.retired.push_back({arena, ndspGetFrameCount()}); s.waveBytes.fetch_sub(arenaBytes); arena = nullptr; arenaBytes = 0;
    // The DSP can still read the retired arena for two frames. Allocate on
    // a later pass, after retirement, instead of requiring both arenas at once.
    retryAfter = nowMs() + 25.f;
    return;
   }
   if (!want) return;
   arena = static_cast<u8*>(linearMemAlign(want, 32));
   if (!arena) {
    SOUND_DIAG("sound: LINEAR full for a %lu byte wave cache\n", (unsigned long)want);
    // A transient allocation failure must not turn off every on-demand
    // race sound until the next scene. Keep the request and retry with backoff.
    retryAfter = nowMs() + 250.f;
    return;
   }
   retryAfter = 0;
   arenaBytes = want; s.waveBytes.fetch_add(want);
  }
 } cache{*this};
 // ---- streams (opened on the audio thread, pumped by the I/O thread)
 LightLock streamLock; std::vector<IoFeed*> feeds;
 // ---- stats
 std::atomic<u32> statFrames{0}; f32 audioMs = 0, audioPeak = 0;
 f32 audioClock = 0, lastGameFrameMs = 0, acc = 0; u32 lastDspFrame = 0;
 LightLock statLock; Stats published{};
 std::atomic<int> reserveRequest{-1}; std::atomic<int> reservedChannel{-1}; std::atomic<int> releaseRequest{-1};

 Impl() { LightEvent_Init(&frameEvent, RESET_ONESHOT); LightEvent_Init(&ioEvent, RESET_ONESHOT); LightLock_Init(&fileLock); LightLock_Init(&streamLock); LightLock_Init(&statLock); }

 static void dspCallback(void* self) { LightEvent_Signal(&static_cast<Impl*>(self)->frameEvent); }

 // ---------------- I/O thread
 bool loadFile(u32 id) {
  const FileEntry* f = archive.file(id);
  if (!f || !brsar) return false;
  auto lf = std::make_unique<LoadedFile>();
  lf->fileId = id;
  if (f->mainSize) {
   lf->main.resize(f->mainSize);
   if (std::fseek(brsar, long(f->mainOffset), SEEK_SET) || std::fread(lf->main.data(), 1, f->mainSize, brsar) != f->mainSize) return false;
  }
  std::vector<u8> mask;
  LightLock_Lock(&fileLock); mask = files[id].mask; LightLock_Unlock(&fileLock);
  if (f->waveSize && !loadWaves(*f, *lf, mask)) return false;
  LightLock_Lock(&fileLock);
  files[id].data = std::move(lf);
  files[id].loadFailures = 0; files[id].retryAfter = 0;
  LightLock_Unlock(&fileLock);
  return true;
 }
 // RWAR into DSP-visible memory. Waves the scene keeps resident (`mask`,
 // empty = every wave) are copied, in runs as they sit in the RWAR; waves over
 // StreamThreshold keep a header copy and a head per channel and stream the
 // rest (WaveFeed); any other wave stays on demand for the wave cache.
 static constexpr u32 StreamThreshold = 128 * 1024, HeadBytes = 32 * 1024;
 bool loadWaves(const FileEntry& f, LoadedFile& lf, const std::vector<u8>& mask) {
  const u32 count = f.waveCount;
  lf.waveOffset.assign(count, InvalidId); lf.cached.assign(count, nullptr);
  bool anyStreamed = false, all = mask.empty();
  for (u32 i = 0; i < count; ++i) {
   const WaveEntry* e = archive.wave(lf.fileId, i);
   if (!e || e->offset + e->size > f.waveSize) return false;
   anyStreamed |= e->size > StreamThreshold;
   all &= mask.empty() || mask[i];
  }
  if (all && !anyStreamed) {
   u8* w = static_cast<u8*>(linearMemAlign(f.waveSize, 32));
   if (!w) { SOUND_DIAG("sound: LINEAR full loading file %lu (%lu bytes)\n", (unsigned long)lf.fileId, (unsigned long)f.waveSize); return false; }
   if (!readChunked(w, f.waveOffset, f.waveSize)) { linearFree(w); return false; }
   for (u32 i = 0; i < count; ++i) lf.waveOffset[i] = archive.wave(lf.fileId, i)->offset;
   DSP_FlushDataCache(w, f.waveSize);
   lf.waves = w; lf.waveBytes = f.waveSize; waveBytes.fetch_add(f.waveSize);
   return true;
  }
  // Resident waves in runs as they sit in the RWAR, heads of streamed ones
  // (snd_layout.hpp: one high-water mark, nothing overlaps).
  std::vector<LayoutWave> plan(count);
  std::vector<WaveData> streamInfo(count);
  for (u32 i = 0; i < count; ++i) {
   const WaveEntry* e = archive.wave(lf.fileId, i);
   plan[i].offset = e->offset; plan[i].size = e->size;
   if (e->size > StreamThreshold) {
    u8 rh[0x20];
    if (std::fseek(brsar, long(f.waveOffset + e->offset), SEEK_SET) || std::fread(rh, 1, sizeof rh, brsar) != sizeof rh || std::memcmp(rh, "RWAV", 4)) return false;
    LoadedFile::Streamed st; st.index = i;
    st.header.resize(be32(rh + 0x18) + 8);
    if (std::fseek(brsar, long(f.waveOffset + e->offset), SEEK_SET) || std::fread(st.header.data(), 1, st.header.size(), brsar) != st.header.size()) return false;
    WaveData& w = streamInfo[i];
    if (!parseRwav(st.header.data(), u32(st.header.size()), w, false) || w.format != 2) return false;
    const u32 channelBytes = (w.loopEnd + 13) / 14 * 8;
    plan[i].headBytes = std::min(HeadBytes, channelBytes) / 8 * 8; plan[i].channels = w.channels;
    st.headSamples = std::min(w.loopEnd, plan[i].headBytes / 8 * 14);
    for (int c = 0; c < w.channels; ++c) st.fileOffset[c] = f.waveOffset + e->offset + u32(w.channel[c].data - st.header.data());
    lf.streamed.push_back(std::move(st));
    continue;
   }
   plan[i].resident = mask.empty() || mask[i];
  }
  const Layout layout = layoutWaves(plan);
  for (u32 i = 0; i < count; ++i) if (plan[i].resident) lf.waveOffset[i] = layout.waveAt[i];
  const u32 total = layout.total;
  if (!total) return true;   // every wave on demand
  u8* w = static_cast<u8*>(linearMemAlign(total, 32));
  if (!w) { SOUND_DIAG("sound: LINEAR full loading file %lu (%lu bytes)\n", (unsigned long)lf.fileId, (unsigned long)total); return false; }
  for (const auto& r : layout.runs) if (!readChunked(w + r.at, f.waveOffset + r.src, r.end - r.src)) { linearFree(w); return false; }
  for (auto& st : lf.streamed) {
   const u32 bytes = plan[st.index].headBytes;
   for (int c = 0; c < plan[st.index].channels; ++c) {
    u8* head = w + layout.headAt[st.index] + c * bytes;
    if (!readChunked(head, st.fileOffset[c], bytes)) { linearFree(w); return false; }
    st.head[c] = head;
   }
  }
  DSP_FlushDataCache(w, total);
  lf.waves = w; lf.waveBytes = total; waveBytes.fetch_add(total);
  return true;
 }
 // I/O thread: one pass over the live feeds. A feed retired meanwhile is
 // skipped; the audio thread frees it only once `pumping` no longer names it.
 bool pumpFeeds() {
  bool did = false;
  for (size_t i = 0;; ++i) {
   LightLock_Lock(&streamLock);
   IoFeed* f = i < feeds.size() ? feeds[i] : nullptr;
   if (f) pumping.store(f);
   LightLock_Unlock(&streamLock);
   if (!f) break;
   if (f->pending()) { f->openAsync(); did = true; }
   did |= f->pump(brsar);
   pumping.store(nullptr);
  }
  return did;
 }
 // Large scene reads in pieces, keeping stream refills prompt in between.
 bool readChunked(void* dst, u32 offset, u32 size) {
  std::clearerr(brsar);
  constexpr u32 Piece = 64 * 1024;
  u8* out = static_cast<u8*>(dst);
  for (u32 done = 0; done < size;) {
   const u32 n = std::min(Piece, size - done);
   if (std::fseek(brsar, long(offset + done), SEEK_SET) || std::fread(out + done, 1, n, brsar) != n) return false;
   done += n;
   pumpFeeds();
  }
  return true;
 }
 void ioLoop() {
  while (!quit.load()) {
   bool did = false;
   // Streams first: they feed the DSP in real time; then cache waves a
   // waiting sound needs.
   did |= pumpFeeds();
   for (CacheJob j; cacheJobs.pop(j); did = true) {
    bool okay=false;
    for(unsigned attempt=0;attempt<3&&!quit.load();++attempt){
     if((okay=readChunked(j.dst,j.offset,j.size)))break;
     svcSleepThread(5*1000000LL);
    }
    if(okay)DSP_FlushDataCache(j.dst, j.size);
    while (!cacheDone.push({j.key,okay}) && !quit.load()) svcSleepThread(1000000);
   }
   // Then at most one scene file per pass so refills stay prompt.
   u32 next = InvalidId; bool pending = false;
   const f32 retryNow = nowMs();
   LightLock_Lock(&fileLock);
   for (u32 id : sceneFiles) if (files[id].wanted && !files[id].data) {
    pending = true;
    if (next == InvalidId && retryNow >= files[id].retryAfter) next = id;
   }
   LightLock_Unlock(&fileLock);
   if (next != InvalidId) {
    if (loadFile(next)) readyFiles.push(next);
    else {
     // A transient LINEAR/SD failure must not remove a required bank for
     // the entire race. Back off, allowing streams and other banks to load.
     LightLock_Lock(&fileLock);
     const unsigned failures = ++files[next].loadFailures;
     files[next].retryAfter = nowMs() + (failures == 1 ? 250.f : failures == 2 ? 1000.f : 5000.f);
     LightLock_Unlock(&fileLock);
     if (failures <= 3) if (FILE* log = std::fopen("sdmc:/3ds/mkw-3ds/audio-load-errors.txt", "a")) {
      std::fprintf(log, "bank=%lu attempt=%u linear_free=%lu resident=%lu\n", (unsigned long)next,
                   failures, (unsigned long)linearSpaceFree(), (unsigned long)waveBytes.load());
      std::fclose(log);
     }
    }
    did = true;
   } else if (!pending && !sceneLoaded.load()) sceneLoaded.store(true);
   if (!did) LightEvent_WaitTimeout(&ioEvent, 20 * 1000000LL);
  }
 }
 static void ioEntry(void* p) { static_cast<Impl*>(p)->ioLoop(); }

 // ---------------- audio thread
 void handle(const SoundCommand& c) {
  Handle* h = c.slot && c.slot < slots.size() ? &slots[c.slot] : nullptr;
  Handle temp{};
  switch (c.type) {
  case SoundCommand::Start: engine.startSound(h ? *h : temp, c.id); break;
  case SoundCommand::Hold: if (h) engine.holdSound(*h, c.id); break;
  case SoundCommand::Stop: if (h) engine.stop(*h, c.frames); break;
  case SoundCommand::Pause: if (h) engine.pause(*h, c.value != 0, c.frames); break;
  case SoundCommand::Volume: if (h) engine.setVolume(*h, c.value, c.frames); break;
  case SoundCommand::Pitch: if (h) engine.setPitch(*h, c.value); break;
  case SoundCommand::Pan: if (h) engine.setPan(*h, c.value); break;
  case SoundCommand::SeqVariable: if (h) engine.setSeqVariable(*h, int(c.id), s16(c.value)); break;
  case SoundCommand::GlobalVariable: Engine::setGlobalVariable(int(c.id), s16(c.value)); break;
  case SoundCommand::TrackVolume: if (h) engine.setTrackVolume(*h, c.id, c.value, c.frames); break;
  case SoundCommand::StreamTrackVolume: if (h) engine.setStreamTrackVolume(*h, c.id, c.value, c.frames); break;
  case SoundCommand::PlayerVolume: engine.setPlayerVolume(c.id, c.value); break;
  case SoundCommand::StopPlayer: engine.stopPlayer(c.id, c.frames); break;
  case SoundCommand::PausePlayer: engine.pausePlayer(c.id, c.value != 0, c.frames); break;
  case SoundCommand::MasterVolume: engine.setMasterVolume(c.value); break;
  case SoundCommand::Ambient: if (h) { Ambient a; a.volume = c.value; a.pan = c.extra[0]; a.pitch = c.extra[1]; a.priority = int(c.extra[2]); engine.setAmbient(*h, a); } break;
  case SoundCommand::StopAll: for (u32 p = 0; p < archive.playerCount(); ++p) engine.stopPlayer(p, c.frames); break;
  case SoundCommand::Layer: gameFrame(true); break;
  case SoundCommand::Prepare: { StartInfo si; si.startOffsetSamples = u32(c.frames); engine.prepareSound(h ? *h : temp, c.id, &si); break; }
  case SoundCommand::StartPrepared: if (h) engine.startPrepared(*h); break;
  case SoundCommand::MoveSlot:
   if(h&&c.id<slots.size()&&c.id!=c.slot){engine.stop(slots[c.id],0);slots[c.id]=*h;*h=Handle{};}
   break;
  }
 }
 void gameFrame(bool fromGame) {
  if (layer && fromGame) layer->gameFrame(engine);
  engine.updateGameFrame(fromGame);
  lastGameFrameMs = audioClock;
 }
 void audioLoop() {
  lastDspFrame = ndspGetFrameCount();
  while (!quit.load()) {
   LightEvent_WaitTimeout(&frameEvent, 50 * 1000000LL);
   const f32 start = nowMs();
   if (layerChanged.exchange(false)) { if (layer) layer->reset(engine); layer = pendingLayer.load(); if (layer) layer->reset(engine); }
   // Channel reservation for MenuMovieAudio.
   if (reserveRequest.exchange(-1) >= 0) {
    const int v = engine.reserveHardwareVoice();
    if (v >= 0) hw->reserveChannel(v);
    reservedChannel.store(v);
   }
   if (int r = releaseRequest.exchange(-1); r >= 0) { hw->unreserve(r); engine.releaseHardwareVoice(r); }
   // Residency changes.
   u32 id;
   for (Drop d; dropFiles.pop(d);) delayedDrops.push_back({d.file, audioClock + d.due});
   for (size_t i = 0; i < delayedDrops.size();) {
    if (delayedDrops[i].due > audioClock) { ++i; continue; }
    id = delayedDrops[i].file; delayedDrops.erase(delayedDrops.begin() + i);
    LightLock_Lock(&fileLock); const bool wantedAgain = files[id].wanted; LightLock_Unlock(&fileLock);
    if (wantedAgain) continue;
    engine.removeFile(id);
    cache.dropFile(id);
    LightLock_Lock(&fileLock);
    if (files[id].data && !files[id].wanted) {
     if (files[id].data->waves) { retired.push_back({files[id].data->waves, ndspGetFrameCount()}); waveBytes.fetch_sub(files[id].data->waveBytes); }
     files[id].data.reset(); files[id].ready = false;
    }
    LightLock_Unlock(&fileLock);
   }
   for (CacheResult r; cacheDone.pop(r);) cache.done(r.key,r.okay);
   cache.resize(wantArena.load());
   while (readyFiles.pop(id)) {
    LightLock_Lock(&fileLock);
    if (files[id].data && files[id].wanted) { files[id].ready = true; engine.addFile(files[id].data.get()); }
    else if (files[id].data) { cache.dropFile(id); if (files[id].data->waves) { retired.push_back({files[id].data->waves, ndspGetFrameCount()}); waveBytes.fetch_sub(files[id].data->waveBytes); } files[id].data.reset(); }
    LightLock_Unlock(&fileLock);
   }
   // Commands, game frames included, in order.
   SoundCommand c;
   int processed = 0;
   while (processed < QueueSize && commands.pop(c)) { handle(c); runSeq = c.seq; ++processed; }
   // Advance sound frames (3 ms) by the DSP frames that elapsed.
   const u32 frame = ndspGetFrameCount();
   u32 elapsed = frame - lastDspFrame; lastDspFrame = frame;
   if (elapsed > 40) elapsed = 40; // after a long stall do not burst through seconds of sequence
   acc += elapsed * DspFrameMs; audioClock += elapsed * DspFrameMs;
   // A stalled game (synchronous race load) still needs fades to progress:
   // after 50 ms without a game frame, advance BasicSound at 60 Hz.
   if (audioClock - lastGameFrameMs > 50.0f) while (audioClock - lastGameFrameMs > 16.667f) { const f32 keep = lastGameFrameMs + 16.667f; gameFrame(false); lastGameFrameMs = keep; }
   while (acc >= f32(SoundFrameMs)) { engine.updateSoundFrame(); acc -= SoundFrameMs; }
   hw->commit();
   // Slot states for the main thread: bits first, then the sequence they cover.
   u32 bits = 0;
   for (int i = 1; i < SoundSystem::TrackedSlots; ++i) if (engine.attached(slots[i])) bits |= 1u << i;
   attachedBits.store(bits, std::memory_order_relaxed);
   handledSeq.store(runSeq, std::memory_order_release);
   // Free LINEAR that the DSP may still have read during the last frame.
   while (!retired.empty() && ndspGetFrameCount() - retired.front().frame >= 2) { linearFree(retired.front().linear); retired.pop_front(); }
   // Stream feeds whose sounds ended.
   pruneFeeds();
   const f32 ms = nowMs() - start; audioMs = audioMs * 0.98f + ms * 0.02f; audioPeak = std::max(audioPeak * 0.999f, ms);
   statFrames.fetch_add(1);
   if ((statFrames.load() & 15) == 0) {
    const auto& es = engine.stats();
    Stats st; st.voices = es.voicesActive; st.sounds = es.soundsActive; st.dropped = es.voicesDropped; st.missing = es.missingData; st.failed = es.startsFailed;
    st.underruns = hw->underruns; st.audioMs = audioMs; st.audioPeakMs = audioPeak; st.frames = statFrames.load();
    st.cacheBytes = cache.arenaBytes; st.cacheUsed = cache.used(); st.cacheLoads = cache.loads; st.cacheEvictions = cache.evictions;
    st.lastMissingSound = es.lastMissingSound; st.lastMissingFile = es.lastMissingFile; st.lastMissingWave = es.lastMissingWave;
    LightLock_Lock(&statLock); published = st; LightLock_Unlock(&statLock);
   }
   LightEvent_Signal(&ioEvent);
  }
 }
 static void audioEntry(void* p) { static_cast<Impl*>(p)->audioLoop(); }
 std::vector<std::pair<IoFeed*, u32>> retiredFeeds;
 std::atomic<IoFeed*> pumping{nullptr};
 static void addFeed(void* ctx, IoFeed* f) { auto* s = static_cast<Impl*>(ctx); LightLock_Lock(&s->streamLock); s->feeds.push_back(f); LightLock_Unlock(&s->streamLock); LightEvent_Signal(&s->ioEvent); }
 static void retireFeed(void* ctx, IoFeed* f) {
  auto* s = static_cast<Impl*>(ctx);
  LightLock_Lock(&s->streamLock); s->feeds.erase(std::remove(s->feeds.begin(), s->feeds.end(), f), s->feeds.end()); LightLock_Unlock(&s->streamLock);
  s->retiredFeeds.push_back({f, ndspGetFrameCount()});
 }
 void pruneFeeds() {
  for (auto it = retiredFeeds.begin(); it != retiredFeeds.end();) {
   if (ndspGetFrameCount() - it->second >= 2 && pumping.load() != it->first) { delete it->first; it = retiredFeeds.erase(it); } else ++it;
  }
 }
 static std::unique_ptr<StreamFeed> openStream(void* ctx, const char* path, u32 startSample) {
  auto* self = static_cast<Impl*>(ctx);
  char full[256]; std::snprintf(full, sizeof full, "%saudio/%s", self->root.c_str(), path);
  auto* feed = new BrstmFeed();
  feed->path = full; feed->startAt = startSample;
  LightLock_Lock(&self->streamLock); self->feeds.push_back(feed); LightLock_Unlock(&self->streamLock);
  LightEvent_Signal(&self->ioEvent);
  struct Owner final : StreamFeed {
   Impl* sys; BrstmFeed* f;
   Owner(Impl* s, BrstmFeed* x) : sys(s), f(x) {}
   ~Owner() override {
    LightLock_Lock(&sys->streamLock); sys->feeds.erase(std::remove(sys->feeds.begin(), sys->feeds.end(), f), sys->feeds.end()); LightLock_Unlock(&sys->streamLock);
    sys->retiredFeeds.push_back({f, ndspGetFrameCount()});
   }
   u8 format() const override { return f->format(); } u32 rate() const override { return f->rate(); }
   int channels() const override { return f->channels(); } int tracks() const override { return f->tracks(); }
   StreamTrack track(int i) const override { return f->track(i); } const AdpcmParam& adpcm(int c) const override { return f->adpcm(c); }
   bool next(int c, StreamBlock& b) override { return f->next(c, b); } void consumed(int c, const u8* d) override { f->consumed(c, d); }
   bool ended() const override { return f->ended(); }
   bool drained(int c) const override { return f->drained(c); }
   int state() const override { return f->state(); }
  };
  return std::make_unique<Owner>(self, feed);
 }
};

SoundSystem& SoundSystem::instance() { static SoundSystem s; return s; }

bool SoundSystem::start() {
 if (impl_) return available();
 MenuMovieAudio::initialize();
 if (!MenuMovieAudio::dspReady()) return false;
 auto* s = new Impl();
 // Asset root: the process chdir()s to it at boot (main.cpp).
 char cwd[256] = {};
 if (getcwd(cwd, sizeof cwd - 1)) { s->root = cwd; if (!s->root.empty() && s->root.back() != '/') s->root += '/'; }
 std::vector<u8> arc;
 if (FILE* f = std::fopen("audio/sound.arc", "rb")) {
  std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  if (n > 0) { arc.resize(size_t(n)); if (std::fread(arc.data(), 1, arc.size(), f) != arc.size()) arc.clear(); }
  std::fclose(f);
 }
 s->brsar = std::fopen("audio/revo_kart.brsar", "rb");
 if (arc.empty() || !s->archive.load(std::move(arc)) || !s->brsar) {

#ifndef MKW_RELEASE
  std::fprintf(stderr, "sound: archive unavailable; running silent\n");
#endif

  if (s->brsar) std::fclose(s->brsar);
  delete s; return false;
 }
 std::setvbuf(s->brsar, nullptr, _IOFBF, 32 * 1024);
 s->files.resize(s->archive.fileCount());
 s->hw = std::make_unique<NdspHardware>();
 s->hw->addFeed = &Impl::addFeed; s->hw->retireFeed = &Impl::retireFeed; s->hw->feedCtx = s;
 s->engine.setup(&s->archive, s->hw.get());
 s->engine.setStreamOpener(&Impl::openStream, s);
 s->engine.setResidency(&s->cache);
 ndspSetOutputMode(NDSP_OUTPUT_STEREO);
 ndspSetClippingMode(NDSP_CLIP_SOFT);
 // Threads: New 3DS core 2; Old 3DS core 1 with an explicit app time limit
 // (the system core is time sliced; docs/AUDIO_SOURCE.md); else core 0
 // above the main thread (short bursts once per DSP frame).
 s32 mainPriority = 0x30; svcGetThreadPriority(&mainPriority, CUR_THREAD_HANDLE);
 bool n3ds = false; APT_CheckNew3DS(&n3ds);
 int core = n3ds ? 2 : 1;
 if (!n3ds) { u32 limit = 0; APT_GetAppCpuTimeLimit(&limit); if (limit < 30) APT_SetAppCpuTimeLimit(30); }
 s->audioThread = threadCreate(&Impl::audioEntry, s, 32 * 1024, 0x18, core, false);
 if (s->audioThread) s->ioThread = threadCreate(&Impl::ioEntry, s, 32 * 1024, 0x19, core, false);
 if (!s->audioThread || !s->ioThread) {
  s->quit.store(true);
  if (s->audioThread) { LightEvent_Signal(&s->frameEvent); threadJoin(s->audioThread, U64_MAX); threadFree(s->audioThread); s->audioThread = nullptr; }
  s->quit.store(false); core = 0;
  s->audioThread = threadCreate(&Impl::audioEntry, s, 32 * 1024, std::max<s32>(0x18, mainPriority - 1), 0, false);
  s->ioThread = threadCreate(&Impl::ioEntry, s, 32 * 1024, std::min<s32>(0x3F, mainPriority + 1), 0, false);
 }
 if (!s->audioThread || !s->ioThread) {
  // Either fallback thread may have started on its own. Join it before
  // destroying the state it reads; thread creation can fail under the
  // smaller physical console's memory/handle limits.
  s->quit.store(true);
  LightEvent_Signal(&s->frameEvent); LightEvent_Signal(&s->ioEvent);
  if (s->audioThread) { threadJoin(s->audioThread, U64_MAX); threadFree(s->audioThread); s->audioThread = nullptr; }
  if (s->ioThread) { threadJoin(s->ioThread, U64_MAX); threadFree(s->ioThread); s->ioThread = nullptr; }
  if (s->brsar) std::fclose(s->brsar);
  delete s; return false;
 }
 ndspSetCallback(&Impl::dspCallback, s);
 impl_ = s;
 running_.store(true, std::memory_order_release);

#ifndef MKW_RELEASE
 std::printf("sound: running on core %d (n3ds=%d), %lu sounds\n", core, int(n3ds), (unsigned long)s->archive.soundCount());
#endif

 return true;
}

void SoundSystem::stop() {
 carriedMusic_=InvalidId;
 if (!impl_) return;
 running_.store(false);
 auto* s = impl_;
 ndspSetCallback(nullptr, nullptr);
 s->quit.store(true);
 LightEvent_Signal(&s->frameEvent); LightEvent_Signal(&s->ioEvent);
 if (s->audioThread) { threadJoin(s->audioThread, U64_MAX); threadFree(s->audioThread); }
 if (s->ioThread) { threadJoin(s->ioThread, U64_MAX); threadFree(s->ioThread); }
 // Retire game-facing handles against the engine that created them,
 // before a replacement engine can reuse its slot/generation numbers.
 if(s->layer){s->layer->reset(s->engine);s->layer=nullptr;}
 // Teardown order: stop and reset every channel, drop references, then free.
 s->engine.shutdown();
 s->hw.reset();
 for (auto& r : s->retired) linearFree(r.linear);
 for (auto& f : s->retiredFeeds) delete f.first;
 for (auto& f : s->files) if (f.data && f.data->waves) linearFree(f.data->waves);
 if (s->cache.arena) linearFree(s->cache.arena);
 if (s->brsar) std::fclose(s->brsar);
 delete s; impl_ = nullptr;
}


void SoundSystem::carryMusic(u32 soundId,std::uint16_t fromSlot){
 if(!impl_||soundId==InvalidId||!fromSlot)return;
 carriedMusic_=soundId;
 SoundCommand c;c.type=SoundCommand::MoveSlot;c.slot=fromSlot;c.id=30;post(c);
}
void SoundSystem::finishCarriedMusic(int frames){
 if(carriedMusic_==InvalidId)return;
 stopSlot(30,frames);carriedMusic_=InvalidId;
}
void SoundSystem::loadScene(const u32* groups, int count, int keepMs, const u32* skip, int skipCount) {
 Scene sc; sc.groups = groups; sc.groupCount = count; sc.keepMs = keepMs; sc.skipFiles = skip; sc.skipCount = skipCount;
 loadScene(sc);
}
void SoundSystem::loadScene(const Scene& sc) {
 if (!impl_) return;
 auto& s = *impl_;
 ScenePlan plan = planSoundScene(s.archive, sc,carriedMusic_);
 LightLock_Lock(&s.fileLock);
 for (u32 f : s.sceneFiles) if (std::find(plan.files.begin(), plan.files.end(), f) == plan.files.end()) { s.files[f].wanted = false; s.dropFiles.push({f, f32(sc.keepMs)}); }
 for (size_t i = 0; i < plan.files.size(); ++i) {
  auto& f = s.files[plan.files[i]];
  if (!f.wanted) { f.loadFailures = 0; f.retryAfter = 0; }
  f.wanted = true;
  // A file already resident keeps its waves; the cache serves any others.
  if (!f.data) f.mask = std::move(plan.masks[i]);
 }
 s.sceneFiles = plan.files;
 s.sceneLoaded.store(false);
 LightLock_Unlock(&s.fileLock);
 s.wantArena.store(sc.cacheBytes);
 LightEvent_Signal(&s.ioEvent);
}
u32 SoundSystem::streamBytes(u32 soundId) const {
 // BrstmFeed: Slots blocks per channel; retail streams use 14336-sample (8 KB) ADPCM blocks.
 const SoundEntry* e = impl_ ? impl_->archive.sound(soundId) : nullptr;
 return e && e->type == TypeStrm ? u32(std::max<u16>(e->strmChannels, 1)) * 4u * 8192u : 0;
}
u32 SoundSystem::residentBytes(const Scene& sc) const {
 if (!impl_) return 0;
 const Archive& a = impl_->archive;
 const ScenePlan plan = planSoundScene(a, sc,carriedMusic_);
 u32 total = 0;
 for (size_t i = 0; i < plan.files.size(); ++i) {
  const FileEntry* f = a.file(plan.files[i]);
  for (u32 w = 0; w < f->waveCount; ++w) {
   const WaveEntry* e = a.wave(plan.files[i], w);
   if (e->size > Impl::StreamThreshold) total += std::min(e->size, 2 * Impl::HeadBytes);
   else if (plan.masks[i].empty() || plan.masks[i][w]) total += (e->size + 31) & ~31u;
  }
 }
 return total;
}
bool SoundSystem::sceneReady() const {
 if (!impl_) return true;
 auto& s = *impl_;
 if (!s.sceneLoaded.load()) return false;
 LightLock_Lock(&s.fileLock);
 bool ready = true;
 for (u32 f : s.sceneFiles) if (s.files[f].wanted && !s.files[f].ready) { ready = false; break; }
 LightLock_Unlock(&s.fileLock);
 return ready;
}
u32 SoundSystem::residentWaveBytes() const { return impl_ ? impl_->waveBytes.load() : 0; }
void SoundSystem::post(const SoundCommand& command) {
 if (!impl_) return;
 SoundCommand c = command;
 c.seq = ++impl_->postSeq;
 if (c.slot < TrackedSlots && (c.type == SoundCommand::Start || c.type == SoundCommand::Hold || c.type == SoundCommand::Prepare)) impl_->slotPosted[c.slot] = c.seq;
 if (!impl_->commands.push(c) && c.slot < TrackedSlots && impl_->slotPosted[c.slot] == c.seq) impl_->slotPosted[c.slot] = 0;
}
bool SoundSystem::slotAttached(std::uint16_t slot) const {
 if (!impl_ || slot == 0 || slot >= TrackedSlots) return false;
 const u32 handled = impl_->handledSeq.load(std::memory_order_acquire);
 if (s32(impl_->slotPosted[slot] - handled) > 0) return true;   // not run yet
 return (impl_->attachedBits.load(std::memory_order_relaxed) >> slot) & 1;
}
void SoundSystem::setLayer(SoundLayer* layer) { if (!impl_) return; impl_->pendingLayer.store(layer); impl_->layerChanged.store(true); }
int SoundSystem::reserveChannel() {
 // Non-blocking. -2: the audio thread has not answered yet (poll again);
 // -1: none free right now; >= 0: the NDSP channel to use until released.
 if (!impl_) return 0;
 int current = impl_->reservedChannel.load();
 if (current == -1) { impl_->reservedChannel.store(-2); impl_->reserveRequest.store(1); current = -2; }
 return current;
}
void SoundSystem::releaseChannel(int channel) { if (impl_ && channel >= 0) { impl_->releaseRequest.store(channel); impl_->reservedChannel.store(-1); } }
SoundSystem::Stats SoundSystem::stats() const {
 Stats out; if (!impl_) return out;
 LightLock_Lock(&impl_->statLock); out = impl_->published; LightLock_Unlock(&impl_->statLock);
 return out;
}
}
