#pragma once
#include <cstdint>

namespace mkw {
// Intro draws both screens from the authored 60 Hz camera; budget two ticks
// per presentation without slowing animation or simulation.
constexpr uint32_t racePresentationTicks(bool new3ds,bool stereo,bool intro){
    return intro||!new3ds||stereo?2u:1u;
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
