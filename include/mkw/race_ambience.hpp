#pragma once
// Course sound zones and ambience (docs/AUDIO_SOURCE.md "Course ambience"):
// Audio::SoundTriggerMgr (Init 8071077c, ApplyTrigger 80711178) and
// RaceAmbienceMgr::PlayIntroAmbience (806f6ec8) for the local racer, as
// requests: ambience ops for RaceAmbienceMgr on the audio thread, and music
// ops (StreamsMgr stems, SinglePlayerValuesMgr volume, Toad's Factory's
// restart) for RaceSound. EchoMgr levels are not kept: NDSP has no reverb.
// Platform free (tests/race_ambience_test.cpp).
#include <cstdint>
#include <vector>

namespace mkw {
// EGG::AudioTrack (SetTarget 80213dbc, Calc 80213e3c): a value moving linearly
// to a target over frames. A target equal to the current value is ignored,
// even while another move runs.
struct AudioTrack {
 float value = 0, target = 0, step = 0; int frames = 0;
 void set(float v, int over) {
  if (value == v) return;
  target = v; frames = over;
  if (over == 0) value = v; else step = (value - v) / float(over);
 }
 bool calc() {
  if (!frames) return false;
  if (--frames == 0) value = target; else value -= step;
  return true;
 }
 void clamp(float lo, float hi) { if (value > hi) value = hi; else if (value < lo) value = lo; }
};

struct AmbienceOp {
 enum Kind : std::uint8_t {
  Play,         // RaceAmbienceMgr::PlayAmbience (806f7e40): start the loop, or bring its volume back
  Volume,       // SetAmbienceVolume (806f7f94): the loop's volume to `value` over `frames`
  Global,       // PlayGlobalSound (806f7c38) on the one global handle
  GlobalStop,   // the global handle stops over `frames`
  GlobalVolume, // its volume to `value` over `frames`
  GlobalFadeIn, // it fades in over `frames`
  OneShot,      // fn_1_1ED62C (806f8a78): the zone one-shot handle; `var` >= 0 also sets seq variable 0
  GlobalStopIf, // PAL fn_1_1EC9C0 (0x806FCD74): the global handle stops over `frames` if it holds `id`
 };
 Kind kind = Play;
 std::uint16_t id = 0, frames = 0;
 float value = 0;
 std::int16_t var = -1;
};
struct MusicOp {
 enum Kind : std::uint8_t {
  Stems,          // StreamsMgr::ChangeStream (806f5bc4): table row `row` over `frames`
  Volume,         // SinglePlayerValuesMgr::SetVolume (806f4ebc) value 0 to `value` over `frames`
  FactoryRestart, // Toad's Factory zone 5: the prepared stream restarts (22 frames), lap 1 prepares another
 };
 Kind kind = Stems;
 std::uint8_t row = 0;
 std::uint16_t frames = 0;
 float value = 0;
};

class SoundTriggers {
public:
 // RaceAmbienceMgr::Init / SoundTriggerMgr ctor: a new race on `course`.
 void reset(unsigned course, bool mirror);
 // SoundTriggerMgr::Init (the race section starts): each course's first zone.
 void init();
 // ApplyTrigger for the local racer: `zone` is the KCL sound trigger variant
 // (or a respawn point's). Ignored once the racer finished, and outside race
 // states 1 and 3-6. `maxLap` is the racer's Raceinfo maxLap.
 void apply(int zone, bool finished, int raceState, unsigned maxLap);
 // PlayIntroAmbience: the next course-preview shot (the first call comes when
 // the race groups have loaded).
 void intro();
 int zone() const { return zone_; }
 unsigned introCount() const { return introCount_; }
 // Requests since the last take.
 std::vector<AmbienceOp> ambience;
 std::vector<MusicOp> music;
private:
 void applyCourse(int zone, int previous, unsigned maxLap);
 unsigned course_ = 0;
 bool mirror_ = false;
 int zone_ = -1;            // SoundTriggerMgr +0x12: the racer's last zone (-1 none)
 unsigned introCount_ = 0;  // RaceAmbienceMgr +0x14
};
}
