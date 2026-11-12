#pragma once
#include <array>
#include <cstdint>
namespace mkw {
// RMCE01 RFLiSetTev4Eye (800c3a90), RFLiSetTev4Mouth (800c3840).
// Inputs/outputs are linear GX channel values in [0,1], before final framebuffer
// quantization. Do not premultiply alpha: the mask capture sets its own blending.
using MiiPixel = std::array<float,4>;
inline bool miiEyeColor(const MiiPixel& tex,unsigned type,unsigned color,MiiPixel& out){
 constexpr std::uint8_t iris[][3]={{0,0,0},{124,128,128},{112,80,64},{112,110,64},{88,104,184},{72,128,104}};
 if(type>=50||color>=6)return false;
 const std::uint8_t base[3]={std::uint8_t(type==9?255:0),std::uint8_t(type==9?130:type==20?255:0),std::uint8_t(type==20?255:0)};
 MiiPixel result{};
 for(unsigned c=0;c<3;++c){
  // R channel: outline; B: iris; G: white. Every TEV stage clamps.
  float v=(float(base[c])/255.f)*tex[0];
  v=v<0?0:v>1?1:v;
  v+=(float(iris[color][c])/255.f)*tex[2];v=v<0?0:v>1?1:v;
  v+=tex[1];result[c]=v<0?0:v>1?1:v;
 }
 result[3]=tex[3];out=result;return true;
}
inline bool miiMouthColor(const MiiPixel& tex,unsigned color,MiiPixel& out){
 constexpr std::uint8_t light[][3]={{190,78,38},{216,48,40},{207,68,71}};
 constexpr std::uint8_t dark[][3]={{113,42,4},{120,21,16},{126,37,40}};
 if(color>=3)return false;
 MiiPixel result{};
 for(unsigned c=0;c<3;++c){
  float v=(float(light[color][c])/255.f)*tex[0];v=v<0?0:v>1?1:v;
  v+=(float(dark[color][c])/255.f)*tex[1];v=v<0?0:v>1?1:v;
  v+=tex[2];result[c]=v<0?0:v>1?1:v;
 }
 result[3]=tex[3];out=result;return true;
}
}
