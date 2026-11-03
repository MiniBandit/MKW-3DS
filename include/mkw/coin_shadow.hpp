#pragma once
#include <mkw/math.hpp>
namespace mkw {
// CoinUnit8086e2a0/e440 and808d79e0: the authored80-triangle
// coin-shadow is a closed volume, transformed at the coin's visual origin.
// It must use the scene's shadow-volume pass, not an ordinary black model
// at bodyY-150 or a replacement floor-aligned quad.
struct CoinShadow {
    static constexpr float Darkness=.5f;
    static Vec3 scale(float modelScale){return {modelScale,4.f*modelScale,modelScale};}
};
}
