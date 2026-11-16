#pragma once
#include <mkw/driver_controller.hpp>
#include <mkw/race_items.hpp>

namespace mkw {
// Original Red update USA 8079fd2c..8079fd68; Blue 807a26c8..807a2774.
// These requests last one frame and are refreshed by the actor, independently
// of the local-player warning sound's 6000-unit range.
inline bool driverItemLookTarget(const ItemActor& actor,unsigned player,Vec3& target) {
    if(!actor.active||actor.explosion||actor.scattered||actor.target!=int(player))return false;
    const auto& state=actor.guidance;
    if(actor.kind==ItemKind::RedShell) {
        if(state.phase<5&&(state.phase!=4||state.ticks<=20))return false;
        target=actor.visualPosition();
        return true;
    }
    if(actor.kind==ItemKind::BlueShell) {
        if(!state.targetLive||state.phase>=6||
            (state.phase<3&&(state.phase!=2||state.ticks<=40)))return false;
        target=actor.visualPosition()+Vec3{0,50,0};
        if(state.phase>=4)target+=actor.velocity;
        return true;
    }
    return false;
}
// Observe the same accepted item events as audio; do not consume the queue.
inline void driverItemSignals(DriverSignals& signals,const RaceItems& items,unsigned player) {
    signals.hasLookTarget=false;
    // Retail updates Red actors before Blue actors; the last request wins.
    // Live slots only (a look target needs an active actor): called for all 12 racers
    // every tick, a full scan read the 96-actor pool (36 KB) 24 times a tick.
    const auto& actors=items.actors();
    for(auto kind:{ItemKind::RedShell,ItemKind::BlueShell})
        items.forEachActorSlot([&](unsigned n){
            const auto& actor=actors[n];
            if(actor.kind==kind&&driverItemLookTarget(actor,player,signals.lookTarget))signals.hasLookTarget=true;
            return true;});
    for(const auto& event:items.sounds()) {
        if(event.player!=player)continue;
        if(event.cue==ItemSoundCue::HitSuccess)signals.celebrate=true;
        else if(event.cue==ItemSoundCue::Release)
            signals.itemThrowDirection=event.flag?1:-1;
    }
}
}
