#pragma once
// Retail race audio driven from the main loop (docs/AUDIO_SOURCE.md "Race"):
// Audio::RaceRSARPlayer (scene groups, pause, stop), RaceMgr (race states:
// course preview, start fanfare, course music, final lap, goal music),
// SinglePlayer (two BGM handles with prepare and delayed start),
// PlayersVolumeMgr, ItemAlterationMgr (Star and Mega themes, the shrunk
// pitch wobble), KartActor::UpdateLapSounds and the RaceHUD countdown.
// Runs at the 60 Hz simulation rate on the main thread and only posts
// SoundSystem commands. Addresses are USA StaticR.
#include <mkw/race_ambience.hpp>
#include <mkw/race_audio.hpp>
#include <array>
#include <cstdint>

namespace mkw {
class RaceSound {
public:
 bool prepareScene();
 bool resourcesReady() const;
 struct Racer { std::uint8_t character = 0, vehicle = 0; bool local = false; std::uint8_t miiVoice = 0; };
 struct Setup {
  unsigned course = 0;        // retail course id (Kinoko::Course)
  unsigned mode = 1;          // RaceConfig game mode: 0 GP, 1 VS, 2 TT, 3 battle
  unsigned lapCount = 3;
  unsigned engineClass = 2;   // 0 50cc, 1 100cc, 2 150cc
  bool mirror = false;
  bool coinBattle = false;
  bool preview = false;       // the course preview plays first (retail intro section)
  unsigned racers = 0;
  std::uint32_t graphicsReserve = 2048u * 1024;
  std::array<Racer, 12> racer{};
 };
 struct Frame {
  unsigned stage = 0;         // Kinoko RaceManager stage (0 intro .. 4 all finished)
  unsigned timer = 0;         // RaceManager timer: countdown frames, then race frames
  int currentLap = 0;         // local player's RaceManager currentLap
  uint8_t battleResultKind=3; //0 MVP,1 winning teammate,2 loss,3 draw.
  uint8_t battlePointCues=0; // TotalPoint red/blue up/down bits, original231..234.
  bool resultPointsChanged = false; // CtrlRaceResult / Team displayed integer changed
  int resultSlideSound = -1; // original result control entry/exit cue
  bool finished = false;      // local player finished the race
  unsigned rank = 1;          // local player's finishing position once finished
  bool star = false, mega = false, shocked = false;   // local kart status bits 63, 79, 71
  int introShot = -1;         // the course preview's shot, -1 outside it
  unsigned hudRank = 0;       // the race HUD's position (raceGrid rank), 0 while no HUD shows
 };
 // The race's graphics are resident: enter the first race state. The sound
 // groups are planned once LINEAR settles (course objects make their skin
 // copies over the first frames), then loaded during the course preview.
 void begin(const Setup& setup);
 // Every simulation tick, after the race step (RaceRSARPlayer::Calc order);
 // `actors` (karts, camera) goes to the audio thread's RaceAudioLayer; its
 // sound cues are cleared once the audio thread has the frame.
 void step(const Frame& frame, RaceAudioFrame* actors = nullptr);
 void previewEnded();          // the course preview finished or was skipped
 void pause(bool paused);      // RaceRSARPlayer::Pause / Resume
 void carryResultMusic(); // Next/Next Race: retire racing actors, keep result stream
 void quit();                  // RaceRSARPlayer::Stop: the race is left
 bool active() const { return active_; }
 // QA: called with a line on every race state change and countdown/lap sound.
 void setTrace(void (*trace)(const char* line, void* context), void* context) { trace_ = trace; traceContext_ = context; }

 // Residency chosen at begin() (runtime.log / QA).
 struct Budget { std::uint32_t linearFree = 0, budget = 0, resident = 0, cache = 0; unsigned demoted = 0; };
 const Budget& budget() const { return budget_; }
 RaceAudioLayer& layer() { return layer_; }
 const RaceAudioLayer& layer() const { return layer_; }

 // EGG::AudioTrack: moves linearly to a target over frames.
 struct Track {
  float value = 1, target = 1, step = 0; int frames = 0;
  void set(float to, int over) { target = to; frames = over; if (over <= 0) { value = to; frames = 0; } else step = (to - value) / float(over); }
  bool moving() const { return frames > 0; }
  void calc() { if (!frames) return; if (--frames == 0) value = target; else value += step; }
 };
 // Audio::SinglePlayer: two BGM handles, one active and one prepared.
 struct Bgm {
  std::uint16_t slot[2]{3, 4};
  std::uint32_t sound[2]{0xFFFFFFFFu, 0xFFFFFFFFu};
  int active = -1, prepared = -1, delay = -1;
  bool attached(int i) const;
  std::uint32_t activeSound() const { return active >= 0 && attached(active) ? sound[active] : 0xFFFFFFFFu; }
  void play(std::uint32_t id, int frames);
  void prepare(std::uint32_t id, std::uint32_t startSample = 0);
  void playPrepared(int frames);
  void stop(int frames);
  void calc();
  void setupActive();          // SetupActiveHandle: course stream track volumes
 };
private:
 void setRaceState(int state);
 void applyMusic(const MusicOp& op);
 void calcStems();
 void rankHud(unsigned rank);
 void checkRaceState(const Frame& f);
 void updateAlteration(const Frame& f);
 void postVolumes(bool force);
 void planScene();
 bool planPending_ = false; std::uint32_t lastFree_ = 0; unsigned stableTicks_ = 0, waitTicks_ = 0;
 Setup setup_{};
 Budget budget_{};
 Bgm bgm_;
 bool active_ = false, paused_ = false;
 int state_ = 0;               // RaceMgr+0x40
 int countdown_ = 0;           // RaceHUD countdown index (0..4)
 int maxLap_ = 1;              // Raceinfo maxLap of the local player
 bool finished_ = false;       // latched until begin(): post-finish audio is music only
 unsigned rank_ = 1,battleResult_=3;
 int alteration_ = 3;          // ItemAlterationMgr state: 0 Mega, 1 Star, 2 shrunk, 3 none
 bool wasMega_ = false, wasStar_ = false;
 float wobble_ = 0;
 float bgmVolume_ = 1, bgmPitch_ = 1, alterVolume_ = 1, fanfareVolume_ = 1;
 float postedBgmVolume_ = -1, postedBgmPitch_ = -1; int postedSlot_ = -1;
 void trace(const char* what, unsigned value);
 void (*trace_)(const char*, void*) = nullptr; void* traceContext_ = nullptr;
 unsigned tick_ = 0;
 std::uint16_t goalVoice_[3]{};   // the local racer's result voices (planScene)
 RaceAudioLayer layer_;        // KartActor & co. on the audio thread
 Track players_[11];           // PlayersVolumeMgr
 Track race_;                  // RaceMgr+0x54 (players 2-6 at the race start)
 float postedPlayer_[11]{};
 SoundTriggers triggers_;      // SoundTriggerMgr and the intro ambience
 bool introLoaded_ = false; int introShot_ = -1;
 // StreamsMgr (806f52f0 PrepareStreams, 806f5bc4 ChangeStream, 806f54b8 Calc):
 // the active course stream's stem volumes.
 AudioTrack stems_[4]; int stemCount_ = 0; std::uint32_t stemSound_ = 0xFFFFFFFFu; float postedStem_[4]{};
 AudioTrack bgmValue_;         // SinglePlayerValuesMgr value 0 (zone volume)
 // CtrlRaceRankNum (807ea3e0): the shown position and its close/open animation.
 unsigned rankShown_ = 0; int rankAnim_ = 2, rankFrame_ = 0;
};
}
