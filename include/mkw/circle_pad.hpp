#pragma once
#include <algorithm>
#include <cmath>
namespace mkw {
// libctru Circle Pad uses a calibrated signed range of approximately +/-156.
// Rearward shell throws require at least 80% downward travel: ceil(156*.8).
inline constexpr bool circlePadThrowBack(int y){return y<=-125;}
static_assert(!circlePadThrowBack(-124)&&circlePadThrowBack(-125));
inline int circlePadSteering(int x){
 // A released pad often rests 10-15 units off centre on the side it was pushed
 // to (PR #76 measured it): read up to 20 as neutral.
 constexpr int DeadZone=20;
 return std::abs(x)<=DeadZone?0:std::clamp(int(std::lround(x*7.f/156.f)),-7,7);
}
// Both stick axes on the Wii's -7..7 lattice. The dead zone is round (a pad
// resting 20 units off centre in any direction is neutral), and the circle
// maps onto the square: the rim gives full lock, diagonals included, from
// RimFull outwards (the pad's physical edge is about 156).
struct StickLattice {int x=0,y=0;};
inline StickLattice circlePadLattice(int x,int y){
 constexpr float DeadZone=20.f,RimFull=145.f;
 const float m=std::sqrt(float(x)*x+float(y)*y);
 if(m<=DeadZone)return {};
 const float reach=std::clamp((m-DeadZone)/(RimFull-DeadZone),0.f,1.f);
 const float edge=std::max(std::abs(float(x)),std::abs(float(y)));
 auto axis=[&](float v){return std::clamp(int(std::lround(v/edge*reach*7.f)),-7,7);};
 return {axis(float(x)),axis(float(y))};
}
}
