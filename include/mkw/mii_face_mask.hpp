#pragma once
#include <mkw/mii_face_layout.hpp>
#include <mkw/mii_face_color.hpp>
#include <algorithm>
#include <cmath>
#include <vector>
namespace mkw {
struct MiiMaskImage {unsigned width=0,height=0;const std::uint8_t* rgba=nullptr;};
struct MiiMaskInput {
 MiiFaceFeatures face;
 unsigned eyeColor=0,browColor=0,mouthColor=0,beardColor=0;
 // One view for each layout part. Empty images are transparent (absent mole,
 // mustache, etc.); shared left/right parts may reference the same image.
 std::array<MiiMaskImage,8> textures{};
};
// CPU mask composition for native Mii renderer integration. Retail Capture 800c4260
// draws color and coverage in separate passes. Float rasterization/bilinear
// filtering here is not an emulation of GX EFB subpixel or RGB5A3 quantization.
inline bool composeMiiFaceMask(const MiiMaskInput& input,unsigned resolution,
 std::vector<std::uint8_t>& output){
 std::array<MiiFacePart,8> parts;
 if(!miiFaceLayout(input.face,resolution,parts)||input.eyeColor>=6||input.browColor>=8||input.mouthColor>=3||input.beardColor>=8)return false;
 for(auto& t:input.textures)if((t.width||t.height||t.rgba)&&(!t.rgba||!t.width||!t.height||t.width>1024||t.height>1024))return false;
 constexpr unsigned order[]={5,6,4,2,3,0,1,7};
 constexpr std::uint8_t hair[][3]={{30,26,24},{56,32,21},{85,38,23},{112,64,36},{114,114,120},{73,54,26},{122,89,40},{193,159,100}};
 std::vector<MiiPixel> pixels(std::size_t(resolution)*resolution);
 auto sample=[](const MiiMaskImage& t,float u,float v){
  float x=u*float(t.width)-.5f,y=v*float(t.height)-.5f;
  int ix=int(std::floor(x)),iy=int(std::floor(y));float fx=x-ix,fy=y-iy;MiiPixel p{};
  for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i){
   unsigned sx=unsigned(std::clamp(ix+int(i),0,int(t.width)-1)),sy=unsigned(std::clamp(iy+int(j),0,int(t.height)-1));
   float w=(i?fx:1-fx)*(j?fy:1-fy);
   for(unsigned c=0;c<4;++c)p[c]+=w*(float(t.rgba[(sy*t.width+sx)*4+c])/255.f);
  }return p;
 };
 for(unsigned pass=0;pass<2;++pass){
  if(pass)for(auto& p:pixels)p[3]=0; // CopyTex clear with color update disabled.
  for(unsigned part:order){
   const auto& t=input.textures[part];if(!t.rgba)continue;
   const auto& p=parts[part];const float angle=p.angle*.01745329238474369f;
   const float co=std::cos(angle),si=std::sin(angle);
   const float left=p.mirror==2?-1.f:p.mirror==1?0.f:-.5f;
   const float right=left+1.f;
   // Retail DrawQuad = translation * XY-scale * rotation * part-scale.
   float minX=float(resolution),minY=minX,maxX=0,maxY=0;
   for(float x:{left,right})for(float y:{-.5f,.5f}){
    float sx=p.x+.8896146416664124f*(co*p.w*x-si*p.h*y);
    float sy=p.y+.927667498588562f*(si*p.w*x+co*p.h*y);
    minX=std::min(minX,sx);maxX=std::max(maxX,sx);minY=std::min(minY,sy);maxY=std::max(maxY,sy);
   }
   const int x0=std::clamp(int(std::floor(minX)),0,int(resolution)),x1=std::clamp(int(std::ceil(maxX)),0,int(resolution));
   const int y0=std::clamp(int(std::floor(minY)),0,int(resolution)),y1=std::clamp(int(std::ceil(maxY)),0,int(resolution));
   for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
    float dx=(float(x)+.5f-p.x)/.8896146416664124f,dy=(float(y)+.5f-p.y)/.927667498588562f;
    float lx=(co*dx+si*dy)/p.w,ly=(-si*dx+co*dy)/p.h;
    if(lx<left||lx>=right||ly<-.5f||ly>=.5f)continue;
    float u=p.mirror==1?1.f-lx:lx-left;
    MiiPixel src=sample(t,u,ly+.5f);if(src[3]<=0)continue;
    if(part<2){MiiPixel shaded;miiEyeColor(src,input.face.eyeType,input.eyeColor,shaded);src=shaded;}
    else if(part==4){MiiPixel shaded;miiMouthColor(src,input.mouthColor,shaded);src=shaded;}
    else {
     const auto* color=hair[part<4?input.browColor:input.beardColor];
     for(unsigned c=0;c<3;++c)src[c]=float(part==7?(c==0?18:15):color[c])/255.f;
    }
    auto& dst=pixels[std::size_t(y)*resolution+x];float a=dst[3];
    // Pass 0: INV_DST_ALPHA, DST_ALPHA; pass 1: INV_DST_ALPHA, ONE,
    // color writes disabled. The second pass restores union coverage.
    if(!pass)for(unsigned c=0;c<3;++c)dst[c]=std::clamp(src[c]*(1.f-a)+dst[c]*a,0.f,1.f);
    dst[3]=std::clamp(src[3]*(1.f-a)+a*(pass?1.f:a),0.f,1.f);
   }
  }
 }
 std::vector<std::uint8_t> result(pixels.size()*4);
 for(std::size_t i=0;i<pixels.size();++i)for(unsigned c=0;c<4;++c)result[4*i+c]=std::uint8_t(std::clamp(pixels[i][c]*255.f+.5f,0.f,255.f));
 output.swap(result);return true;
}
}
