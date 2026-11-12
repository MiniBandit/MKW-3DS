#pragma once
#include <mkw/course_bloom.hpp>
#include <algorithm>
namespace mkw {
// Original EGG threshold constant, byte-truncated like PostEffect::ColorScale.
inline uint32_t bloomThresholdColor(const CourseBloom& b){
 const float scale=(16.f+219.f*b.threshold)/255.f;
 uint32_t out=0;
 for(unsigned shift:{24u,16u,8u,0u})out|=uint32_t(std::clamp(float((b.thresholdColor>>shift)&255)*scale,0.f,255.f))<<shift;
 if(b.flags&16)out&=0xffffff00u;
 return out;
}
inline float bloomTapIntensity(const CourseBloom& b,unsigned band,unsigned stage=0){
 const float value=b.intensity[band]*(stage?.5f:1.f);
 return float(unsigned(std::clamp(value,0.f,1.f)*255.f))/255.f;
}
// Restore authored PBLM tint and intensity; the previous LCD attenuation
// removed too much of the haze requested in the latest hardware feedback.
inline uint32_t bloomDisplayTint(const CourseBloom& b){
 constexpr float gain=1.f,warmReduction=0.f;
 float rgb[3]={float(b.tint>>24),float((b.tint>>16)&255),float((b.tint>>8)&255)};
 if(rgb[0]>rgb[2]&&rgb[1]>rgb[2])
  for(unsigned i=0;i<2;++i)rgb[i]-=(rgb[i]-rgb[2])*warmReduction;
 uint32_t out=b.tint&255;
 for(unsigned i=0;i<3;++i)out|=uint32_t(rgb[i]*gain)<<(24-i*8);
 return out;
}
// Fill the complete power-of-two textures: hardware edge clamping must act
// at the rendered boundary, not at the edge of unused texture allocation.
inline constexpr unsigned BloomNearWidth=128,BloomNearHeight=64;
inline constexpr unsigned BloomFarWidth=64,BloomFarHeight=32;
inline float bloomTapRadius(float strength,unsigned band,unsigned sourceExtent,unsigned originalExtent){
 // Restore a broader source-style halo on the small LCD, without adding
 // blur passes, changing light sets or lifting the unblurred scene itself.
 return 1.3f*strength*(band?2.f:1.f)*float(sourceExtent)*4.f/float(originalExtent);
}
// PostEffectBlur's uniformly spaced ring, with angle zero at the first tap.
inline constexpr float BloomRing[16][2]={
 {0,1},{.3826834324f,.9238795325f},{.7071067812f,.7071067812f},{.9238795325f,.3826834324f},
 {1,0},{.9238795325f,-.3826834324f},{.7071067812f,-.7071067812f},{.3826834324f,-.9238795325f},
 {0,-1},{-.3826834324f,-.9238795325f},{-.7071067812f,-.7071067812f},{-.9238795325f,-.3826834324f},
 {-1,0},{-.9238795325f,.3826834324f},{-.7071067812f,.7071067812f},{-.3826834324f,.9238795325f}};
inline constexpr unsigned BloomFullQuads=4+1+4+1+4+8+2;
inline constexpr unsigned BloomReducedQuads=4+1+4+1+4+8+2;
}
