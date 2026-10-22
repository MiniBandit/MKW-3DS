#pragma once
#include "mkw/race_items.hpp"
#include <cmath>
namespace mkw {
// The held shell controller's 2000/65536 turn per source tick. Use a separate
// presentation phase so spinning never changes guidance, collisions or aim.
inline float shellVisualSpin(unsigned tick){return float(uint16_t(tick*2000u))*(6.283185307179586f/65536.f);}
// Released, only the Green and Red spin (fn_1_2A5CEC 0x807B60A0, from 0x807AEC64 / 0x807AA87C);
// no Blue state calls it.
inline bool spinningShell(ItemKind kind){return kind==ItemKind::GreenShell||kind==ItemKind::RedShell;}
inline HeldItemVisual releasedItemVisual(const ItemActor& actor){
    const auto up=actor.explosion?Vec3{0,1,0}:normalized(actor.up,{0,1,0});
    Vec3 facing{std::sin(actor.yaw),0,std::cos(actor.yaw)};
    auto right=normalized(cross(up,facing),{1,0,0});facing=normalized(cross(right,up),{0,0,1});
    const float angle=actor.explosion?0:spinningShell(actor.kind)?shellVisualSpin(actor.age):
        actor.kind==ItemKind::Star?float(uint16_t(unsigned(actor.age)*1000u))*(6.283185307179586f/65536.f):0;
    const auto z=facing*std::cos(angle)+right*std::sin(angle);
    return {cross(up,z),up,z,actor.drawScale()};
}
}
