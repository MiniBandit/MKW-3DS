#pragma once
// Port of the NW4R snd runtime that Mario Kart Wii drives (doldecomp/ogws
// src/nw4r/snd, checked against the retail DOL; docs/AUDIO_SOURCE.md).
// Platform free: the 3DS NDSP backend (snd_ndsp.cpp) and the host test spy
// implement Hardware. Everything here runs on the single audio thread; the
// game thread talks to it only through SoundSystem's command queue.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mkw::snd {
using u8 = std::uint8_t; using s8 = std::int8_t; using u16 = std::uint16_t; using s16 = std::int16_t;
using u32 = std::uint32_t; using s32 = std::int32_t; using f32 = float;

inline constexpr u32 InvalidId = 0xFFFFFFFFu;
inline constexpr int SoundFrameMs = 3;          // AX frame the sound thread runs at
inline constexpr int VoiceMax = 24;             // NDSP channels
inline constexpr int ChannelMax = 2;            // NW4R CHANNEL_MAX (mono/stereo waves)
inline constexpr int PriorityMax = 127;         // BasicSound::PRIORITY_MAX
inline constexpr int VoicePriorityMax = 255;    // Voice::PRIORITY_MAX (streams)
inline constexpr int VoicePriorityRelease = 1;  // Channel::PRIORITY_RELEASE

enum SoundType : u8 { TypeInvalid = 0, TypeSeq = 1, TypeStrm = 2, TypeWave = 3 };
enum PanMode : u8 { PanDual = 0, PanBalance = 1 };
enum PanCurve : u8 { CurveSqrt, CurveSqrt0Db, CurveSqrt0DbClamp, CurveSinCos, CurveSinCos0Db, CurveSinCos0DbClamp,
                     CurveLinear, CurveLinear0Db, CurveLinear0DbClamp };

// ---------------------------------------------------------------- archive
// Cooked from revo_kart.brsar by tools/cook_audio.py (audio/sound.arc).
struct SoundEntry {
 u8 type = 0, volume = 0, playerPriority = 0, remoteFilter = 0;
 u8 panMode = 0, panCurve = 0, actorPlayerId = 0, channelPriority = 64;
 u8 releasePriorityFix = 0, decayCurve = 1, decayRatio = 128, dopplerFactor = 0;
 u16 playerId = 0, bankId = 0xFFFF;
 u32 fileId = InvalidId;
 u32 data = 0;        // seq dataOffset, wave subNo, strm startPosition
 u32 allocTrack = 0;  // seq/wave allocTrack, strm allocTrackFlag
 u32 flags3d = 0;
 u32 userParam[2]{};
 u16 strmChannels = 0, pad = 0;
};
static_assert(sizeof(SoundEntry) == 44);

struct FileEntry {
 u32 mainOffset = 0, mainSize = 0;   // RSEQ/RBNK/RWSD bytes in the BRSAR
 u32 waveOffset = 0, waveSize = 0;   // its RWAR in the BRSAR (0 = none)
 u32 waveCount = 0;
 u32 streamPath = InvalidId;         // index into the archive string pool (BRSTM)
};
struct GroupEntry { u32 firstItem = 0, itemCount = 0; };
// A loop that starts inside a DSP-ADPCM frame (tools/cook_audio.py loop_bridge):
// NDSP plays `frame` (samples [loopStart, bodyStart)) then the retail body from
// bodyStart with the decoder state restored to (bodyPs, bodyYn1, bodyYn2).
struct LoopBridge { u32 fileId = 0; u16 wave = 0; u8 channel = 0, samples = 0; u32 bodyStart = 0; u16 bodyPs = 0; s16 bodyYn1 = 0, bodyYn2 = 0; u8 frame[8]{}; u16 pad = 0; };
static_assert(sizeof(LoopBridge) == 28);
struct PlayerEntry { u8 playableSoundCount = 1; };
// One RWAR entry: its RWAV's offset from the RWAR start and its size.
struct WaveEntry { u32 offset = 0, size = 0; };
// Every wave a sound can ever play (tools/audio_reach.py): indices into the
// RWAR of `file` (the bank's file for a sequence, the RWSD's for a wave sound).
struct WaveReach { u32 file = InvalidId; const u16* waves = nullptr; u32 count = 0; };

class Archive {
public:
 // Parses audio/sound.arc already read into memory (little endian, see cook_audio.py).
 bool load(std::vector<u8> bytes);
 bool loaded() const { return !sounds_.empty(); }
 u32 soundCount() const { return u32(sounds_.size()); }
 const SoundEntry* sound(u32 id) const { return id < sounds_.size() ? &sounds_[id] : nullptr; }
 const FileEntry* file(u32 id) const { return id < files_.size() ? &files_[id] : nullptr; }
 u32 fileCount() const { return u32(files_.size()); }
 u32 bankFile(u32 bank) const { return bank < banks_.size() ? banks_[bank] : InvalidId; }
 const PlayerEntry* player(u32 id) const { return id < players_.size() ? &players_[id] : nullptr; }
 u32 playerCount() const { return u32(players_.size()); }
 u32 groupCount() const { return u32(groups_.size()); }
 // File ids of a group's items (retail GroupInfo item table).
 void groupFiles(u32 group, std::vector<u32>& out) const;
 const char* string(u32 index) const;
 const LoopBridge* bridge(u32 fileId, u32 wave, int channel) const;
 const WaveEntry* wave(u32 fileId, u32 index) const;
 WaveReach reach(u32 soundId) const;
 // Retail SoundArchivePlayerInfo counts (seq/strm/wave sound and track pools).
 struct PlayerInfo { u16 seqSounds = 0, seqTracks = 0, strmSounds = 0, strmTracks = 0, strmChannels = 0, waveSounds = 0, waveTracks = 0; } info;
private:
 std::vector<SoundEntry> sounds_;
 std::vector<FileEntry> files_;
 std::vector<u32> banks_, groupItems_;
 std::vector<GroupEntry> groups_;
 std::vector<PlayerEntry> players_;
 std::vector<char> strings_;
 std::vector<LoopBridge> bridges_;
 std::vector<WaveEntry> waves_;
 std::vector<u32> fileWaves_;                 // first waves_ index per file
 struct ReachEntry { u32 first = 0, count = 0; };
 std::vector<ReachEntry> reach_;
 std::vector<u16> reachPool_;
};

// ---------------------------------------------------------------- waves
struct AdpcmParam { s16 coef[16]{}; u16 ps = 0; s16 yn1 = 0, yn2 = 0; };
struct AdpcmLoop { u16 ps = 0; s16 yn1 = 0, yn2 = 0; };
// One resident wave (WaveFileReader::ReadWaveParam, sample units).
struct WaveData {
 u8 format = 2, loop = 0, channels = 1;   // 0 PCM8, 1 PCM16, 2 DSP-ADPCM
 u32 rate = 32000, loopStart = 0, loopEnd = 0;
 struct Channel { const u8* data = nullptr; u32 bytes = 0; AdpcmParam adpcm; AdpcmLoop loopParam; const LoopBridge* bridge = nullptr;
  u32 fileOffset = 0; } channel[ChannelMax];
 // Long waves stream from the BRSAR: `data` holds only the first headSamples
 // (DSP visible), the rest is read from fileOffset by the I/O thread.
 bool streamed = false; u32 headSamples = 0;
};

// --------------------------------------------------------------- hardware
// One hardware voice plays one mono channel of a wave (NW4R AxVoice).
// Streams (BRSTM, NW4R StrmPlayer) feed ADPCM blocks per file channel instead.
struct StreamBlock { const u8* data = nullptr; u32 samples = 0; bool context = false; AdpcmLoop adpcm; };
struct StreamTrack { u8 channels = 0, channel[2]{}, volume = 127, pan = 64; };
class StreamFeed {
public:
 virtual ~StreamFeed() = default;
 virtual u8 format() const = 0;            // WaveFile format (2 = DSP-ADPCM)
 virtual u32 rate() const = 0;
 virtual int channels() const = 0;
 virtual int tracks() const = 0;
 virtual StreamTrack track(int index) const = 0;
 virtual const AdpcmParam& adpcm(int channel) const = 0;
 // Next block for this file channel, or false if none is ready (underrun).
 virtual bool next(int channel, StreamBlock& block) = 0;
 virtual void consumed(int channel, const u8* data) = 0;
 virtual bool ended() const = 0;           // non-looping stream fully queued
 // This channel has been handed every block (a starved channel is an underrun only if not).
 virtual bool drained(int channel) const { (void)channel; return ended(); }
 virtual int state() const = 0;            // 0 loading header, 1 ready, -1 failed
};
class Hardware {
public:
 virtual ~Hardware() = default;
 virtual int voiceCount() const { return VoiceMax; }
 virtual void start(int v, const WaveData& wave, int channel, u32 offsetSamples) = 0;
 virtual void startStream(int v, StreamFeed* feed, int channel) = 0;
 virtual void stop(int v) = 0;
 virtual void pause(int v, bool paused) = 0;
 // rate in Hz, gains linear (AX mix 0x8000 == 1.0), lpf 0 = bypass.
 virtual void set(int v, f32 rate, f32 left, f32 right, f32 lpfHz) = 0;
 virtual bool finished(int v) = 0;
 // Called once at the end of every audio-thread pass, after all set() calls.
 virtual void commit() {}
};

// --------------------------------------------------------------- residency
// File data the players read: main data in ordinary heap, waves in
// DSP-visible memory (LINEAR on 3DS). Owned by the Bank of loaded files.
struct LoadedFile {
 u32 fileId = InvalidId;
 std::vector<u8> main;
 u8* waves = nullptr;         // RWAR image (DSP visible); offsets below index into it
 u32 waveBytes = 0;
 std::vector<u32> waveOffset; // per RWAR entry: RWAV offset in `waves`, InvalidId if not resident
 // Waves too long to keep resident: header copy, DSP-visible heads and the
 // BRSAR offset of each channel's samples.
 struct Streamed { u32 index = 0; std::vector<u8> header; u32 fileOffset[ChannelMax]{}; const u8* head[ChannelMax]{}; u32 headSamples = 0; };
 std::vector<Streamed> streamed;
 // Waves the scene did not keep resident, loaded on demand into the wave
 // cache (DSP visible RWAV images; null until loaded). Set by the audio thread.
 std::vector<const u8*> cached;
 int refs = 0;
};

class Engine;
struct EngineObserver;

// On-demand wave residency (SoundSystem's wave cache). Starting a sound pins
// every wave it can reach (Archive::reach); a sound with waves still loading
// waits like a stream being prepared, and its waves stay pinned until it ends.
class WaveResidency {
public:
 virtual ~WaveResidency() = default;
 virtual bool acquire(u32 file, u32 wave) = 0;  // pin; true if playable now, else requested
 virtual bool ready(u32 file, u32 wave) = 0;
 virtual void release(u32 file, u32 wave) = 0;
};
inline constexpr int WaveWaitFrames = 60;       // game frames a sound may wait for its waves

// ------------------------------------------------------------ public API
// Handles are generation-checked slots so game code can keep them across
// sounds (nw4r::snd::SoundHandle semantics: detached when the sound ends).
struct Handle { u16 slot = 0xFFFF; u16 generation = 0; bool valid() const { return slot != 0xFFFF; } };

struct StartInfo {
 int playerPriority = -1;     // -1: archive value
 int playerId = -1;           // -1: archive value
 u32 startOffsetMs = 0;
 u32 startOffsetSamples = 0;  // streams: START_OFFSET_TYPE_SAMPLE (0: the archive's start position)
};

// 3D actor parameters (MKW Audio::Engine3D inputs) are applied by the
// caller through ambient overrides on each sound.
struct Ambient { f32 volume = 1, pitch = 1, pan = 0, surroundPan = 0, fxSend = 0, lpf = 0; int priority = 0; };

struct EngineStats { u32 voicesActive = 0, voicesDropped = 0, soundsActive = 0, startsFailed = 0, missingData = 0, streamUnderruns = 0;
 u32 lastMissingSound = InvalidId, lastMissingFile = InvalidId, lastMissingWave = InvalidId; };  // QA

// The NW4R runtime: SoundArchivePlayer + SoundPlayers + sequencer + channels +
// voices. Not thread safe; one audio thread owns it.
class Engine {
public:
 Engine();
 ~Engine();
 Engine(const Engine&) = delete; Engine& operator=(const Engine&) = delete;
 void setup(const Archive* archive, Hardware* hardware);
 void shutdown();
 const Archive* archive() const;

 // Files resident for playback (SoundArchivePlayer::LoadGroup equivalent).
 void addFile(LoadedFile* file);
 void removeFile(u32 fileId);  // stops every sound using it first
 LoadedFile* fileData(u32 fileId) const;
 // Opens a BRSTM named by the archive ("strm/....brstm") from startSample.
 void setStreamOpener(std::unique_ptr<StreamFeed> (*open)(void* ctx, const char* path, u32 startSample), void* ctx);
 void setResidency(WaveResidency* residency);

 // SoundStartable: start/hold/prepare by sound id. Returns false like START_ERR_*.
 bool startSound(Handle& handle, u32 id, const StartInfo* info = nullptr, const Ambient* ambient = nullptr);
 bool holdSound(Handle& handle, u32 id, const StartInfo* info = nullptr, const Ambient* ambient = nullptr);
 bool prepareSound(Handle& handle, u32 id, const StartInfo* info = nullptr, const Ambient* ambient = nullptr);
 void startPrepared(Handle h);
 bool attached(Handle h) const;
 u32 soundId(Handle h) const;
 void detach(Handle& h);
 void stop(Handle h, int fadeFrames);
 void pause(Handle h, bool paused, int fadeFrames);
 void fadeIn(Handle h, int frames);
 void setVolume(Handle h, f32 volume, int frames = 0);
 void setPitch(Handle h, f32 pitch);
 void setPan(Handle h, f32 pan);
 void setSurroundPan(Handle h, f32 pan);
 void setLpf(Handle h, f32 lpf);
 void setFxSend(Handle h, int bus, f32 send);
 void setAmbient(Handle h, const Ambient& ambient);
 void setPlayerPriority(Handle h, int priority);   // BasicSound::SetPlayerPriority
 int playerPriority(Handle h) const;
 const Ambient* ambient(Handle h) const;            // the ambient parameters last set
 void setAutoStop(Handle h, int frames);
 // SeqSoundHandle
 void setSeqVariable(Handle h, int index, s16 value);
 static void setGlobalVariable(int index, s16 value);
 void setTrackVolume(Handle h, u32 trackMask, f32 volume, int frames = 0);
 void setTrackPitch(Handle h, u32 trackMask, f32 pitch);
 void setTempoRatio(Handle h, f32 ratio);
 // StrmSoundHandle track volume (per BRSTM track).
 void setStreamTrackVolume(Handle h, u32 trackMask, f32 volume, int frames);
 bool prepared(Handle h) const;
 bool started(Handle h) const; // preparation/queue acceptance alone is not playback
 u32 playingSamples(Handle h) const; // stream position, 0 otherwise
 u32 seqTick(Handle h) const;        // SeqSoundHandle::GetTick: ticks played, 0 for other sounds

 // SoundPlayer controls (by archive player id).
 void setPlayerVolume(u32 player, f32 volume);
 void stopPlayer(u32 player, int fadeFrames);
 void pausePlayer(u32 player, bool paused, int fadeFrames);
 int playingCount(u32 player) const;
 void setMasterVolume(f32 volume);

 // Game frame (60 Hz): BasicSound fades/params, player priority sorting.
 // Maintenance frames during a stalled game advance fades but cannot
 // expire HoldSound: the game has not had a chance to renew its handles.
 void updateGameFrame(bool advanceHold = true);
 // Sound frame (3 ms): sequencer ticks, channel envelopes, voice params.
 void updateSoundFrame();
 const EngineStats& stats() const;
 // A hardware voice taken out of the pool (MenuMovieAudio's THP soundtrack).
 int reserveHardwareVoice();
 void releaseHardwareVoice(int voice);
 struct Impl;
private:
 friend void setObserver(Engine&, EngineObserver*);
 friend u32 soundFrameCount(const Engine&);
 friend int testAllocVoice(Engine&, int, int);
 friend void testReleaseVoice(Engine&, int);
 friend void testFreeVoice(Engine&, int);
 friend int testVoicePriority(const Engine&, int);
 std::unique_ptr<Impl> impl_;
};

// Test/QA observation of the sequencer (tests/snd_engine_test.cpp).
struct NoteEvent { u32 sound = 0, tick = 0, frame = 0; int track = 0, key = 0, velocity = 0, prg = 0; s32 length = 0; bool tie = false; };
struct EngineObserver {
 virtual ~EngineObserver() = default;
 virtual void noteOn(const NoteEvent&) {}
 // When true, NoteOn allocates no channel (matches tools/rseq_ref.py).
 bool suppressChannels = false;
};
void setObserver(Engine& engine, EngineObserver* observer);
u32 soundFrameCount(const Engine& engine);
// Voice manager probes for the priority/stealing test.
int testAllocVoice(Engine& engine, int channels, int priority);
void testReleaseVoice(Engine& engine, int voice);   // Channel::Release (priority 1)
void testFreeVoice(Engine& engine, int voice);
int testVoicePriority(const Engine& engine, int voice); // -1 when the voice is gone

// BRSTM header (StrmFileReader::ReadStrmInfo + track/channel tables).
struct BrstmInfo {
 u8 format = 2, loop = 0, channels = 0; u32 rate = 0, loopStart = 0, loopEnd = 0;
 u32 audioOffset = 0, blocks = 0, blockSize = 0, blockSamples = 0, lastBlockSize = 0, lastBlockSamples = 0, lastBlockPadded = 0;
 u32 adpcOffset = 0, adpcSize = 0;
 int trackCount = 0; StreamTrack track[8]; AdpcmParam adpcm[8]; AdpcmLoop loopParam[8];
};
// `file` holds at least the RSTM header and HEAD block; offsets are into the file.
bool parseBrstm(const u8* file, u32 size, BrstmInfo& out);
// One RWAV (WaveFileReader); channel data pointers point into `rwav`.
bool parseRwav(const u8* rwav, u32 size, WaveData& out, bool checkData);

// Utility maths exposed for tests (snd_Util.cpp).
f32 pitchRatio(int microtones);   // Util::CalcPitchRatio
f32 volumeRatio(f32 db);          // Util::CalcVolumeRatio
f32 panRatio(f32 pan, int curve, bool centerZero, bool zeroClamp); // Util::CalcPanRatio
u16 random();                     // Util::CalcRandom (shared LCG)
void seedRandom(u32 seed);
}
