#pragma once
#include <cstdint>
#include <mkw/math.hpp>
#include <limits>
namespace mkw {
// Native AIControlBattle item interaction table USA808c5388/808c54ac.
// Values0/1 approach,2 retreat; item kind20 is None.
inline unsigned battleAttackChoice(unsigned own,unsigned opponent){
    constexpr uint8_t Index[]={1,3,6,13,8,9,12,5,0,11,10,15,14,0,16,0,2,4,7,0,0};
    constexpr uint8_t Interaction[17][17]={
        {1,2,2,2,2,1,2,2,2,2,2,2,2,2,1,2,2},
        {1,1,2,2,2,1,1,1,1,2,2,2,2,1,1,2,2},
        {1,1,1,1,2,1,1,1,1,1,2,2,2,1,1,2,2},
        {1,1,2,1,2,1,1,1,1,1,1,2,1,1,1,2,2},
        {1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,2,2},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,1,2,2,2,2,2,2,1,1,2,2},
        {1,2,2,2,2,1,1,1,2,2,2,2,2,1,1,2,2},
        {1,1,1,2,2,1,1,1,1,2,2,2,1,1,1,2,2},
        {1,1,1,2,2,1,1,1,1,1,2,2,1,1,1,2,2},
        {1,1,1,1,2,1,1,1,1,1,1,2,1,1,1,2,2},
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {1,1,1,2,2,1,1,1,1,2,2,2,1,1,1,2,2},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1},
        {1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,2,2},
    };
    return own<=20&&opponent<=20?Interaction[Index[own]][Index[opponent]]:0;
}
// USA80747910/80747a20: choose a successor by full3D distance to the
// stored opponent, honoring Path+21 incoming exclusion; strict ties retain order.
template<class Route> unsigned battleAttackBranch(const Route& route,unsigned from,unsigned previous,Vec3 target,bool nearest,bool excludePrevious=true){
    const auto& point=route.point(from);float best=nearest?std::numeric_limits<float>::max():0.f;unsigned result=0;
    for(unsigned k=0;k<point.nextCount;++k){if(excludePrevious&&previous!=from&&point.next[k]==previous)continue;
        const float distance=length(route.point(point.next[k]).position-target);
        if(nearest?distance<best:distance>best){best=distance;result=k;}}
    return result;
}
}
