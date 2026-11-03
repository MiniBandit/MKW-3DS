#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace mkw {
struct MenuStripeSlice {float y,h,v0,v1;};
// The original bg.brlyt line0 is a vertically repeating full-frame stripe
// image (57 periods). Split at its content edge, never sample T3X padding.
inline std::array<MenuStripeSlice,2> menuStripeSlices(float y,float h,float v0,float v1,double seconds){
 const float shift=float(std::fmod(seconds*4.0,240.0)/240.0);
 const float start=std::fmod(v0+shift,1.f),span=v1-v0;
 if(span<=0)return {};
 const float first=std::min(span,1.f-start),height=h*first/span;
 return {{{y,height,start,start+first},{y+height,h-height,0.f,span-first}}};
}
}
