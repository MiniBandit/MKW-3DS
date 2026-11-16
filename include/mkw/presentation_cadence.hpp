#pragma once
#include <cstdint>

namespace mkw {
// New 3DS presents every tick in races, their intros and the award ceremony,
// as the Wii: it runs 60 FPS except 3-4 player and online races (USA 80516b10,
// set only from the race scene at 8054e250, which the ceremony, game mode 11,
// also goes through). introSixty false (no-intro-60.txt) brings back two ticks
// per presentation for intros and the ceremony. stereoSixty: New 3DS stereo
// presents every tick too (its lighter 3D: race_visuals' stereo cuts).
constexpr uint32_t racePresentationTicks(bool new3ds,bool stereo,bool intro,bool stereoSixty=false,bool introSixty=true){
    return (intro&&!introSixty)||!new3ds||(stereo&&!stereoSixty)?2u:1u;
}
// One presentation per slot of source ticks: 1 tick (60 FPS, New 3DS in 2D) or
// 2 ticks (30 FPS: Old 3DS, which cannot present every tick and would otherwise
// judder between one- and two-tick frames, and New 3DS in stereo). Missed slots
// are coalesced; simulation and input polling remain independent.
class PresentationCadence {
public:
    void setTicksPerSlot(uint32_t ticks){ticks_=ticks?ticks:1;started_=false;}
    uint32_t ticksPerSlot() const {return ticks_;}
    bool due(uint32_t sourceTick) {
        const uint32_t slot=sourceTick/ticks_;
        if(started_&&slot==slot_)return false;
        started_=true;slot_=slot;return true;
    }
    void reset(){started_=false;}
private:
    uint32_t ticks_=1;
    uint32_t slot_=0;
    bool started_=false;
};
}
