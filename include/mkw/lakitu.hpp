#pragma once
#include <cstdint>
#include <memory>
#include <mkw/math.hpp>
#include <mkw/renderer.hpp>
namespace Kinoko::Kart { class KartObject; }
namespace mkw {
class Renderer;
struct LakituInput {
    const Kinoko::Kart::KartObject* kart=nullptr;
    unsigned stage=0,timer=0,lap=1,lapCount=3;
    bool introHeld=true,finished=false;
    Vec3 cameraForward{0,0,1};
};
// The start signal's count-down clip, jg_cd (270 frames, played once). Retail
// RacePage_afterCalc (USA 0x80835A28) starts it on every Lakitu when the
// countdown timer equals fn_1_20E5D0(60) = 60 - 50 (0x8071A554; the 50 is the
// halfword at 0x808C4E1C). Each count throws the hand down to the bottom of
// its swing (clip frames 45, 108, 175, 232), then raises the fingers, so the
// throws land with the 3, 2, 1 and GO digits and beeps at 60, 120, 180, 240.
// The UI runs after the Jugem update, so the clip is set (frame 0) by the next
// tick's Jugem start state (fn_1_2133F8 0x8071F37C, flag +0x1EC) and the
// scene's animation update advances it once before it is drawn: frame
// timer - 10 from timer 11 on, which is the 60 - 50 alignment the code intends.
inline constexpr unsigned LakituCountdownStartTimer=60-50;
// jg_cd's frame for a countdown stage/timer, or -1 while Lakitu still waves (jg_ww).
inline int lakituCountdownFrame(unsigned stage,unsigned timer){
    return stage>=1&&timer>LakituCountdownStartTimer?int(timer-LakituCountdownStartTimer):-1;
}
// Retail Lakitu (jugemu): start signal, lap 2..N and final lap boards, the
// wrong-way board, the respawn pickup and the goal flag. Steps at 60 Hz.
class Lakitu {
public:
    Lakitu();~Lakitu();
    bool load();
    void unload();
    void reset();
    void step(const LakituInput&);
    void draw(Renderer&);
    bool shadowVisible()const;
    void drawShadow(Renderer&,const Model* const*,unsigned,Vec3,bool,const Renderer::ShadowReceiver*,unsigned);
    // Audio::ObjectActor on Lakitu::Player (+0x214): the sound held this tick
    // (UpdateRespawn 80720314 SE_JUGEM_RESCUE, UpdateBackwards 80720abc
    // SE_JUGEM_REVERSE; 0 for none), and where Lakitu is.
    uint16_t soundLoop()const;
    Vec3 position()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
