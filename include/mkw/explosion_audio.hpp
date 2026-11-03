#pragma once
#include <cstdint>
namespace mkw::snd {
// Dad-requested mix adjustment, over the authored sound/sequence parameters.
// Bob-omb, Blue Shell, shared echo, highway bomb car and Bowser fireball.
inline constexpr bool explosionSound(std::uint32_t id){
 return id==0x105||id==0x106||id==0x109||id==0x210||id==0x2ee;
}
inline constexpr float ExplosionGain=1.8f,ExplosionFullDistance=3000.f,ExplosionUnitDistance=4000.f;
}
