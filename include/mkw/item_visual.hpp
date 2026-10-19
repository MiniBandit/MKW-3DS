#pragma once
#include "mkw/race_items.hpp"
#include <cmath>
namespace mkw {
// The held shell controller's 2000/65536 turn per source tick. Use a separate
// presentation phase so spinning never changes guidance, collisions or aim.
inline float shellVisualSpin(unsigned tick){return float(uint16_t(tick*2000u))*(6.283185307179586f/65536.f);}
inline bool spinningShell(ItemKind kind){return kind==ItemKind::GreenShell||kind==ItemKind::RedShell||kind==ItemKind::BlueShell;}
// ItemObjProperties +0x6c, copied by USA 80787af0. Retail object IDs differ
// from inventory IDs: mushroom=3, star=4, blue=5, fake=7, bomb=9.
// Obj::UpdateShadow (80797a5c) multiplies X/Z only; Y stays the model scale.
inline constexpr float itemShadowScale(ItemKind kind){
    switch(kind){
    case ItemKind::GreenShell:case ItemKind::RedShell:case ItemKind::Banana:
    case ItemKind::TripleGreenShell:case ItemKind::TripleRedShell:case ItemKind::TripleBanana:
    case ItemKind::Bobomb:return 1.f;
    case ItemKind::FakeBox:return 1.2f;
    case ItemKind::Mushroom:case ItemKind::TripleMushroom:case ItemKind::Star:
    case ItemKind::GoldenMushroom:case ItemKind::MegaMushroom:return .8f;
    case ItemKind::BlueShell:return .9f;
    default:return 0.f;
    }
}
inline HeldItemVisual releasedItemVisual(const ItemActor& actor){
    const auto up=actor.explosion?Vec3{0,1,0}:normalized(actor.up,{0,1,0});
    Vec3 facing{std::sin(actor.yaw),0,std::cos(actor.yaw)};
    auto right=normalized(cross(up,facing),{1,0,0});facing=normalized(cross(right,up),{0,0,1});
    // Damage-ejected shells keep their spawn orientation. Obj::UpdateModel-
    // PositionNoClip (USA 807975e8) changes translation without advancing the
    // stored model rotation; ordinary launched shells still spin.
    const float angle=actor.explosion?0:spinningShell(actor.kind)?(actor.scattered?0:shellVisualSpin(actor.age)):
        actor.kind==ItemKind::Star?float(uint16_t(unsigned(actor.age)*1000u))*(6.283185307179586f/65536.f):0;
    const auto z=facing*std::cos(angle)+right*std::sin(angle);
    return {cross(up,z),up,z,actor.drawScale()};
}
}
