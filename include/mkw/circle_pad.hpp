#pragma once
#include <algorithm>
#include <cmath>
namespace mkw {
// libctru Circle Pad uses a calibrated signed range of approximately +/-156.
// Rearward shell throws require at least 80% downward travel: ceil(156*.8).
inline constexpr bool circlePadThrowBack(int y){return y<=-125;}
static_assert(!circlePadThrowBack(-124)&&circlePadThrowBack(-125));
// Both axes on the Wii's 15-step lattice (-7..7). The deadzone is radial, so a
// resting pad that sits a little off center never steers. Travel past it is
// rescaled, full lock comes a little before the rim (worn pads stop short of
// 156), and the round gate is stretched to a square so a diagonal still gives
// full lock on both axes, as the Nunchuk's gate does.
struct CirclePadStick{int x=0,y=0;};
inline CirclePadStick circlePadStick(int dx,int dy){
 constexpr float DeadZone=24,Full=140;
 const float r=std::sqrt(float(dx*dx+dy*dy));
 if(r<=DeadZone)return {};
 const float travel=std::min(1.f,(r-DeadZone)/(Full-DeadZone));
 const float cx=dx/r,cy=dy/r,square=travel/std::max(std::fabs(cx),std::fabs(cy));
 auto step=[](float v){return std::clamp(int(std::lround(v*7.f)),-7,7);};
 return {step(cx*square),step(cy*square)};
}
}
