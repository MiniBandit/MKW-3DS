#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace mkw {
// Evaluate the tyre in its own circular support. Rotate the tread/spokes,
// never the circular silhouette through a nearest-neighbour square crop.
inline void loadingWheelPixel(float x,float y,float size,float angle,uint8_t out[4]){
 float rgb=0,alpha=0;
 for(unsigned sy=0;sy<4;++sy)for(unsigned sx=0;sx<4;++sx){
  const float dx=(x+(sx+.5f)/4-size*.5f)*32/size;
  const float dy=(y+(sy+.5f)/4-size*.5f)*32/size;
  const float r=std::sqrt(dx*dx+dy*dy);if(r>14.5f)continue;
  const float a=std::atan2(dy,dx)-angle;
  float v=0;
  if(r>9.2f)v=(std::fmod(std::fmod(a,.44879895f)+.44879895f,.44879895f)<.11f)?85:32;
  else if(r>7.4f)v=185;
  else if(r<2.7f||std::cos(a*5)>.72f)v=215;
  else v=55;
  rgb+=v;alpha+=1;
 }
 out[0]=out[1]=out[2]=alpha?uint8_t(std::lround(rgb/alpha)):0;
 out[3]=uint8_t(std::lround(alpha*255/16));
}
inline uint8_t loadingWheelBlend(uint8_t foreground,uint8_t alpha,uint8_t background){
 return uint8_t((unsigned(foreground)*alpha+unsigned(background)*(255-alpha)+127)/255);
}
}
