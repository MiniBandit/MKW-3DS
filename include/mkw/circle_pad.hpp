#pragma once
#include <algorithm>
#include <cmath>
namespace mkw {
// libctru Circle Pad uses a calibrated signed range of approximately +/-156.
// Rearward shell throws require at least 80% downward travel: ceil(156*.8).
inline constexpr bool circlePadThrowBack(int y){return y<=-125;}
static_assert(!circlePadThrowBack(-124)&&circlePadThrowBack(-125));
inline int circlePadSteering(int x){
 constexpr int DeadZone=16;
 return std::abs(x)<=DeadZone?0:std::clamp(int(std::lround(x*7.f/156.f)),-7,7);
}
}
