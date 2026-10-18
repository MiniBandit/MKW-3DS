#pragma once
// Helpers shared by the race item sources (race_items.cpp, item_world.cpp, item_native.cpp).
#include "mkw/race_items.hpp"
#include "mkw/held_items.hpp"
#include <cmath>

namespace mkw::items {
inline constexpr float Pi=3.14159265358979323846f;
inline void tick(uint16_t& timer){if(timer)--timer;}
inline bool shell(ItemKind kind){return kind==ItemKind::GreenShell||kind==ItemKind::RedShell||kind==ItemKind::BlueShell;}
// Bob-ombs trail behind the kart like Bananas until the button is released.
inline bool singleHeld(ItemKind kind){return kind==ItemKind::GreenShell||kind==ItemKind::RedShell||kind==ItemKind::Banana||kind==ItemKind::FakeBox||kind==ItemKind::Bobomb;}
inline bool trailing(ItemKind kind){return singleHeld(kind)||kind==ItemKind::TripleBanana;}
// Objects whose OnKill is a blast (Bob-omb 0x8079cf60, Blue Shell 0x807a376c).
inline bool blastItem(ItemKind kind){return kind==ItemKind::Bobomb||kind==ItemKind::BlueShell;}
inline bool trapItem(ItemKind kind){return kind==ItemKind::Banana||kind==ItemKind::FakeBox||kind==ItemKind::Bobomb;}
// ItemObj property +0x59 (fn_1_291E80 0x807A2234): a hit by this object makes its owner cheer.
inline bool hitVoiceItem(ItemKind kind){return shell(kind)||trapItem(kind);}
// ItemObj property +0x58 (initItemObjProperties 0x80790AFC): set for object types 0-4, 7
// and 9, cleared for every type in Time Trials (getIsTt 0x80790E1C). With it an object has
// a floor tracker (ItemObj_init 0x8079E224 also gives types 2/7/9 one) and survives KCL
// contact in the scatter state (0x807A7350); it also picks the drop constants (0x807BC350,
// 0x807A6F74).
inline bool groundItem(ItemKind kind,bool timeTrial){
    if(timeTrial)return false;
    switch(kind){
    case ItemKind::GreenShell:case ItemKind::RedShell:case ItemKind::Banana:case ItemKind::Mushroom:
    case ItemKind::Star:case ItemKind::FakeBox:case ItemKind::Bobomb:return true;
    default:return false;
    }
}
inline bool triple(ItemKind kind){return kind==ItemKind::TripleMushroom||kind==ItemKind::TripleBanana||kind==ItemKind::TripleGreenShell||kind==ItemKind::TripleRedShell;}
/// The object a triple (or single) inventory item throws.
inline ItemKind baseKind(ItemKind kind){
    return kind==ItemKind::TripleBanana?ItemKind::Banana:kind==ItemKind::TripleGreenShell?ItemKind::GreenShell:
        kind==ItemKind::TripleRedShell?ItemKind::RedShell:kind==ItemKind::TripleMushroom?ItemKind::Mushroom:kind;
}
inline Vec3 itemMotion(const ItemRacer& r){return r.haveVelocity?r.velocity:normalized(r.forward,{0,0,1})*r.speed;}
// The held items' frame (Item::PlayerObj +0x58 X, +0x64 Y, +0x70 Z, +0x7c anchor), rebuilt each
// step an item is held and once on deployment (fn_1_281AA8 0x80791E5C, from fn_1_281F68
// 0x8079231C and ItemHolderPlayerb4_use 0x80791AFC). `up` and `lift` persist (+0x64, +0xcc).
//  * Y eases .2 toward the body's up in an action or trick (fn_1_2AD3D8 0x807BD78C: status 32,
//    47, 54), else toward KartMove +0x38 (smoothedUp), so bike lean and body pitch never tilt
//    the ring or the trail. fn_1_280F2C (0x807912E0): Y normalised, X = Y x front, Z = X x Y.
//  * Anchor: the kart position + Z * itemUnk17c * scale.z, lifted in a wheelie by the last
//    wheel's rise since race frame 60 (`wheel` = its kart-frame y, z and height there, from
//    fn_1_285210 0x807955C4): .2 toward it, else x.85 (0x808A4CEC).
struct HeldFrame { Vec3 right{1,0,0},up{0,1,0},forward{0,0,1},anchor{}; };
inline HeldFrame heldFrame(Vec3& up,float& lift,const ItemRacer& r,Vec3 wheel){
    up=normalized(up+((r.inAction||r.trickRotating?r.up:r.smoothedUp)-up)*.2f,{0,1,0});
    const Vec3 x=normalized(cross(up,r.forward),{1,0,0}),z=cross(x,up);
    lift=r.wheelie?lift+.2f*(wheel.x*r.up.y+wheel.y*r.forward.y-wheel.z-lift):lift*.85f;
    return {x,up,z,r.position+z*(r.itemGeometry[3]*r.scale.z)+Vec3{0,lift,0}};
}
inline TrailingItemPose trailingPose(const ItemRacer& r,const HeldFrame& f,float terrainRadius=40){
    return {f.anchor,f.forward,f.up,r.scale,
        r.itemGeometry[0]*(r.bike?.8f:.9f)+40,r.itemGeometry[2]*r.scale.y+terrainRadius,
        r.speed,r.speedRatio,r.hop,dot(r.haveVelocity?r.velocity:r.forward*r.speed,r.forward)>0,!r.grounded};
}
// RMCU object IDs: green=0, red=1, banana=2, mushroom=3, star=4,
// blue=5, lightning=6, fake=7, cloud=8, bomb=9. Inventory IDs differ.
inline unsigned retailObjectId(ItemKind kind){
    switch(kind){
    case ItemKind::GreenShell:case ItemKind::TripleGreenShell:return 0;
    case ItemKind::RedShell:case ItemKind::TripleRedShell:return 1;
    case ItemKind::Banana:case ItemKind::TripleBanana:return 2;
    case ItemKind::Mushroom:case ItemKind::TripleMushroom:case ItemKind::GoldenMushroom:return 3;
    case ItemKind::Star:return 4;
    case ItemKind::BlueShell:return 5;
    case ItemKind::Lightning:return 6;
    case ItemKind::FakeBox:return 7;
    case ItemKind::ThunderCloud:return 8;
    case ItemKind::Bobomb:return 9;
    default:return 14;
    }
}
}
