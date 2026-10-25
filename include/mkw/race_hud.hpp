#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <mkw/math.hpp>
#include <mkw/race_hud_layout.hpp>
namespace mkw {
class Renderer;
class RaceItems;
struct CountdownCue {int digit=0;bool go=false;float scale=1,alpha=1;unsigned frame=0;};
// The original 240-tick countdown. RacePage_afterCalc (USA 0x80835A28) sets
// count n's message (3, 2, 1, GO) and starts its animation at timer
// n*60 + 51, so the zoom-in lands on frame 9 with the beep and lamp at
// (n+1)*60; GO lands as physics unlocks at 240.
inline constexpr unsigned CountdownCueStart=51;
inline CountdownCue countdownCue(unsigned stage,unsigned timer){
    CountdownCue cue;
    constexpr unsigned GoStart=CountdownCueStart+3*60;
    if(stage==1&&timer>=CountdownCueStart&&timer<GoStart){
        cue.digit=3-int((timer-CountdownCueStart)/60);
        cue.frame=(timer-CountdownCueStart)%60;
    }else if((stage==1||stage==2)&&timer>=GoStart&&timer<GoStart+HudCountFrames.size()){
        cue.go=true;cue.frame=timer-GoStart;
    }
    if(cue.digit||cue.go){cue.scale=HudCountFrames[cue.frame][0].scale;cue.alpha=HudCountFrames[cue.frame][0].alpha;}
    return cue;
}
class RaceHud {
public:
    RaceHud();~RaceHud();
    bool load();void reset();
    // Source presentation recurrences advance at 60 Hz, independent of rendering.
    // started: the race frame counter runs (countdown onward); HUD counters slide in.
    void stepWorld(const RaceItems&,Vec3 cameraBackward,bool started=true);
    void drawWorld(Renderer&,const RaceItems&,Vec3 viewer,unsigned tick);
    // finishFrame: frames since the player finished (FINISH animation), or -1.
    // exitSlide: the race-end CtrlRaceBase hide slide 0..1, or -1.
    void drawOverlay(Renderer&,const RaceItems&,unsigned stage,unsigned timer,unsigned rank,unsigned lap,bool finished,unsigned tick,int finishFrame=-1,float exitSlide=-1.f);
    bool speedometer=true; // SELECT toggles it during a race
    // Incoming threats (not in the original; Mario Kart 7 style): shells homing on
    // the player and powered-up racers closing in, nearest first. Shown at the top
    // centre, placed toward the side they come from, flashing red when close.
    struct Warning {uint8_t kind=20;float side=0;float distance=0;}; // kind: ItemKind (20 none)
    std::array<Warning,4> warnings{};unsigned warningCount=0;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
