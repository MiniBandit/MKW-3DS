#pragma once
#include <algorithm>
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
// The race camera CtrlRaceItemBalloon places its icon from: RaceCamera eye (+0x64),
// target (+0x70) and up (+0x7c), the vertical FOV, and look-behind (flag 0x20).
struct HudCamera {Vec3 eye{},target{0,0,-1},up{0,1,0};float fov=60;bool lookBack=false;};
class RaceHud {
public:
    RaceHud();~RaceHud();
    bool load();void reset();
    // Source presentation recurrences advance at 60 Hz, independent of rendering.
    // started: the race frame counter runs (countdown onward); HUD counters slide in.
    // camera: this step's race camera; without it the incoming-item icon stays closed.
    void stepWorld(const RaceItems&,Vec3 cameraBackward,bool started=true,const HudCamera* camera=nullptr);
    // The icon's SE_ITM_ALARM_VCL request from the last stepWorld (ItemWarningMgr
    // fn_1_1E76F4, PAL 0x806F7AA8): its layout x (+0x184) and rate; false: none.
    bool chaseAlarm(float& x,float& rate) const;
    // QA: the HudItems picture of an icon that opened in the last stepWorld (-1:
    // none), and the icons opened this race by picture.
    int chaseOpened() const;
    const std::array<uint16_t,21>& chaseShown() const;
    // The item and item-box shadow volumes. Retail resolves them in the opaque pass at
    // priority 0x35, after the course and before every kart and item: call before the karts.
    void drawShadows(Renderer&,const RaceItems&,Vec3 viewer);
    void drawWorld(Renderer&,const RaceItems&,Vec3 viewer,unsigned tick);
    // finishFrame: frames since the player finished (FINISH animation), or -1.
    // exitSlide: the race-end CtrlRaceBase hide slide 0..1, or -1.
    void drawOverlay(Renderer&,const RaceItems&,unsigned stage,unsigned timer,unsigned rank,unsigned lap,bool finished,unsigned tick,int finishFrame=-1,float exitSlide=-1.f);
    // The Bob-omb blast, age frames in, with the retail bombCore materials and
    // its own pose per age: course objects (bomb cars, fireballs, cannonballs) share it.
    void drawBombBlast(Renderer&,const C3D_Mtx& transform,unsigned age) const;
    bool speedometer=true; // SELECT toggles it during a race
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
