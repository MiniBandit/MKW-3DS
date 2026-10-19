#pragma once
#include <cstdint>

namespace mkw {
// Effects::Player::UpdateFromKartCollision (RMCU 0x8069d1d8) on the body's closest wall
// (Kinoko keeps no per-hitbox wall list): whether a wall contact counts as a hit. Walls
// 0xc, 0xf, 0x14 and 0x1e count, except bushes (0xc variant 5, OnBushCollision), player
// wall variant 7 and special wall variants 0, 1 and 3. Race audio plays the wall sound and
// KartEffect restarts its wall timer (+0x7b4) on the same hits.
inline bool countedWallContact(bool contact,std::uint8_t type,std::uint8_t variant){
    if(!contact)return false;
    if(type==0x1e&&variant==1)return false;
    if(type==0xc&&variant==5)return false;
    const bool wall=type==0xc||type==0xf||type==0x14||type==0x1e;
    const bool skip=(type==0x14&&variant==7)||(type==0x1e&&(variant==3||variant==0));
    return wall&&!skip;
}
// Shared Effects::Player wall trigger (USA 8069262c..80692738).
// Call after advancing the cooldown for this physics tick.
inline bool wallImpactReady(bool beforeRespawn,bool contact,float speed,int frames){
    return !beforeRespawn&&contact&&speed>30.f&&frames>30;
}
// Effects::Player::OnBushCollision (0x806a221c): the bush wall, 0xc variant 5.
inline bool bushContact(bool contact,std::uint8_t type,std::uint8_t variant){
    return contact&&type==0xc&&variant==5;
}
}
