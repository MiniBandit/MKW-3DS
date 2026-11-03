#pragma once
#include <array>
#include <cstdint>
namespace mkw {
// Retail RFLiSetFaceParts (RMCE01 800c3d40). Coordinates describe the
// source mask at the requested resolution; mirrored pairs share a texture.
struct MiiFacePart {float x,y,w,h,angle;std::uint32_t mirror;};
struct MiiFaceFeatures {
 unsigned eyeType=0,eyeScale=0,eyeRotation=0,eyeX=0,eyeY=0;
 unsigned browType=0,browScale=0,browRotation=0,browX=0,browY=0;
 unsigned mouthScale=0,mouthY=0,mustacheScale=0,mustacheY=0;
 unsigned moleScale=0,moleX=0,moleY=0;
};
inline bool miiFaceLayout(const MiiFaceFeatures& f,unsigned resolution,std::array<MiiFacePart,8>& out){
 if(f.eyeType>=50||f.browType>=24||!resolution||resolution>1024)return false;
 const unsigned small[]={f.eyeScale,f.eyeX,f.browScale,f.browX,f.mouthScale,f.mustacheScale,f.moleScale};
 for(auto v:small)if(v>15)return false;
 const unsigned large[]={f.eyeRotation,f.eyeY,f.browRotation,f.browY,f.mouthY,f.mustacheY,f.moleX,f.moleY};
 for(auto v:large)if(v>31)return false;
 constexpr std::uint8_t eyeRot[]={29,28,28,28,29,28,28,28,29,28,28,28,28,29,29,28,28,28,29,29,28,29,28,29,29,28,29,28,28,29,28,28,28,29,29,29,28,28,29,29,29,28,28,29,29,29,29,29,28,28};
 constexpr std::uint8_t browRot[]={26,26,27,25,26,25,26,25,28,25,26,24,27,27,26,26,25,25,26,26,27,26,25,27};
 const float k=float(resolution)*0.015625f;
 auto size=[](double base,unsigned n){return float(base*(1.0+double(0.4f*float(n))));};
 const float ex=0.8896146416664124f*float(f.eyeX),ey=18.45152473449707f+1.076094388961792f*float(f.eyeY);
 const float ew=size(5.34375,f.eyeScale),eh=size(4.5,f.eyeScale),ea=11.25f*float((f.eyeRotation+eyeRot[f.eyeType])&31);
 const float bx=0.8896146416664124f*float(f.browX),by=16.549806594848633f+1.076094388961792f*float(f.browY);
 const float bw=size(5.0625,f.browScale),bh=size(4.5,f.browScale),ba=11.25f*float((f.browRotation+browRot[f.browType])&31);
 const float my=29.25885009765625f+1.076094388961792f*float(f.mouthY);
 const float sy=31.763553619384766f+1.076094388961792f*float(f.mustacheY);
 out={{{(32-ex)*k,ey*k,ew*k,eh*k,ea,2}, {(32+ex)*k,ey*k,ew*k,eh*k,360-ea,1},
       {(32-bx)*k,by*k,bw*k,bh*k,ba,2}, {(32+bx)*k,by*k,bw*k,bh*k,360-ba,1},
       {32*k,my*k,size(6.1875,f.mouthScale)*k,size(4.5,f.mouthScale)*k,0,0},
       {32*k,sy*k,size(4.5,f.mustacheScale)*k,size(9,f.mustacheScale)*k,0,2},
       {32*k,sy*k,size(4.5,f.mustacheScale)*k,size(9,f.mustacheScale)*k,0,1},
       {(17.766164779663086f+1.7792292833328247f*float(f.moleX))*k,
        (17.95985984802246f+1.076094388961792f*float(f.moleY))*k,size(1,f.moleScale)*k,size(1,f.moleScale)*k,0,0}}};
 return true;
}
}
