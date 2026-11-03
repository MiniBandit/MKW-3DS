#pragma once
#include <mkw/math.hpp>
#include <algorithm>
#include <limits>

namespace mkw {
// AISpeedBattle::update (USA 8073438c) and AISpeedBase's corner response
// (80733b24). ENPT setting1 is a speed subtraction in battle, rather than
// the mushroom/wheelie route tag used by AIPathPointRace.
inline float battleCpuSpeed(float baseSpeed,int pointPenalty,bool inked,
                            Vec3 toTarget,Vec3 bodyUp,Vec3 bodyFront){
    float speed=std::max(baseSpeed-float(pointPenalty),0.f);
    if(inked)speed*=.8f;
    constexpr float Epsilon=std::numeric_limits<float>::epsilon();
    const float upSquared=dot(bodyUp,bodyUp);
    if(upSquared<=Epsilon)return speed;
    const Vec3 flat=toTarget-bodyUp*(dot(toTarget,bodyUp)/upSquared);
    if(dot(flat,flat)<=Epsilon)return speed;
    const float degrees=std::fabs(std::atan2(length(cross(flat,bodyFront)),dot(flat,bodyFront)))*57.295780181884765625f;
    const float turn=std::clamp((degrees-20.f)/70.f,0.f,1.f);
    return speed*(1.f+turn*(.1f-1.f));
}
}
