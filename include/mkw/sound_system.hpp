#pragma once
// The game-facing sound system: owns the NW4R engine port (snd_engine.hpp),
// the NDSP backend, the audio thread and the stream/loader I/O thread.
// Every public call is non-blocking and safe from the main thread; work is
// queued to the audio thread. Without DSP firmware (ndspInit failure) it
// stays silent and every call is a no-op.
#include <mkw/snd_engine.hpp>
#include <atomic>
#include <cstdint>

namespace mkw {
namespace snd { class Engine; }

// Main thread -> audio thread command. Slots name sounds the game keeps
// (NW4R SoundHandle equivalents); slot 0 means "fire and forget".
struct SoundCommand {
 enum Type : std::uint8_t {
  Start, Hold, Stop, Pause, Volume, Pitch, Pan, SeqVariable, GlobalVariable, TrackVolume, StreamTrackVolume,
  PlayerVolume, StopPlayer, PausePlayer, MasterVolume, Ambient, StopAll, Layer, Prepare, StartPrepared, MoveSlot
 };
 Type type = Start;
 std::uint8_t pad = 0;
 std::uint16_t slot = 0;
 std::uint32_t seq = 0;    // set by SoundSystem::post
 std::uint32_t id = 0;     // sound id / player id / variable index / track mask
 std::int32_t frames = 0;  // fade frames
 float value = 0;          // volume, pitch, pan, variable value
 float extra[3]{};         // Ambient: pan, pitch, priority(as float)
};

class SoundLayer;

class SoundSystem {
public:
 static SoundSystem& instance();
 // Loads audio/sound.arc and opens audio/revo_kart.brsar under the asset
 // root (romfs:/ or sdmc:/3ds/mkw-3ds). Requires MenuMovieAudio::initialize.
 bool start();
 void stop();
 // Preserve only the already-playing results music across a loading handoff.
 void carryMusic(std::uint32_t soundId,std::uint16_t fromSlot);
 void finishCarriedMusic(int fadeFrames=0);
 bool carryingMusic() const {return carriedMusic_!=0xffffffffu&&available();}
 bool available() const { return running_.load(std::memory_order_acquire); }

 // Scene residency (SoundArchivePlayer::LoadGroup): replaces the previous
 // set; files shared with it stay resident. Loading happens on the I/O
 // thread; sceneReady() turns true once every file is playable. Files no
 // longer wanted are released after `keepMs` (a fading sound keeps its data);
 // `skipFiles` lists files of these groups the port can never play.
 void loadScene(const std::uint32_t* groups, int count, int keepMs = 0, const std::uint32_t* skipFiles = nullptr, int skipCount = 0);
 // A scene under a LINEAR budget. Every file of `groups` is known to the
 // engine. The waves of `residentGroups` (every group if residentCount < 0)
 // that the scene's sounds can reach (Archive::reach) stay resident, except
 // those reachable only from `onDemandSounds`; `residentSounds` keep their
 // waves resident whatever their group; any other wave a sound needs
 // loads into a fixed wave cache of `cacheBytes` LINEAR when the sound starts
 // (the sound waits for it, see snd::WaveResidency). Waves over
 // StreamThreshold always stream from the BRSAR through a resident head.
 struct Scene {
  const std::uint32_t* groups = nullptr; int groupCount = 0;
  const std::uint32_t* residentGroups = nullptr; int residentCount = -1;
  const std::uint32_t* onDemandSounds = nullptr; int onDemandCount = 0;
  const std::uint32_t* residentSounds = nullptr; int residentSoundCount = 0;
  const std::uint32_t* skipFiles = nullptr; int skipCount = 0;
  std::uint32_t cacheBytes = 0; int keepMs = 0;
 };
 void loadScene(const Scene& scene);
 // LINEAR the scene's resident waves and stream heads take (budgeting; main thread).
 std::uint32_t residentBytes(const Scene& scene) const;
 // LINEAR a stream sound's block ring takes while it plays (0 for other sounds).
 std::uint32_t streamBytes(std::uint32_t soundId) const;
 bool sceneReady() const;
 // LINEAR bytes the current scene's waves occupy (QA / budget reports).
 std::uint32_t residentWaveBytes() const;

 void post(const SoundCommand& command);
 // Convenience wrappers.
 void play(std::uint32_t id, std::uint16_t slot = 0) { SoundCommand c; c.type = SoundCommand::Start; c.id = id; c.slot = slot; post(c); }
 void hold(std::uint32_t id, std::uint16_t slot) { SoundCommand c; c.type = SoundCommand::Hold; c.id = id; c.slot = slot; post(c); }
 void stopSlot(std::uint16_t slot, int frames) { SoundCommand c; c.type = SoundCommand::Stop; c.slot = slot; c.frames = frames; post(c); }
 // Slots below 32: whether the slot's sound (started, prepared or held) is
 // still attached, counting commands the audio thread has not run yet.
 static constexpr int TrackedSlots = 32;
 bool slotAttached(std::uint16_t slot) const;

 // Audio-thread layer (race/menu sound logic ported from MKW's Audio::*),
 // fed per game frame; nullptr removes it. Ownership stays with the caller.
 void setLayer(SoundLayer* layer);

 // NDSP channel for MenuMovieAudio while it plays (taken out of the engine's pool).
 int reserveChannel();
 void releaseChannel(int channel);

 struct Stats { std::uint32_t voices = 0, sounds = 0, dropped = 0, underruns = 0, missing = 0, failed = 0; float audioMs = 0, audioPeakMs = 0; std::uint32_t frames = 0;
  std::uint32_t cacheBytes = 0, cacheUsed = 0, cacheLoads = 0, cacheEvictions = 0;
  std::uint32_t lastMissingSound = 0xFFFFFFFFu, lastMissingFile = 0xFFFFFFFFu, lastMissingWave = 0xFFFFFFFFu; };
 Stats stats() const;
 struct Impl;
private:
 SoundSystem() = default;
 Impl* impl_ = nullptr;
 std::uint32_t carriedMusic_=0xffffffffu;
 std::atomic<bool> running_{false};
};

// Logic that must run in lockstep with game frames on the audio thread
// (MKW's Audio::RaceMgr / KartActor equivalents). The main thread hands it a
// snapshot each game frame through its own lock-free buffer.
class SoundLayer {
public:
 virtual ~SoundLayer() = default;
 // Called on the audio thread once per consumed game frame, before the
 // engine's game-frame update. Slots map to the engine through `handles`.
 virtual void gameFrame(snd::Engine& engine) = 0;
 virtual void reset(snd::Engine& engine) { (void)engine; }
};
}
