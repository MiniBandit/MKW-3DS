#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace mkw {
struct MiiShapePlacement {
 bool transform=false,mirrorX=false;
 float scale=1;
 std::array<float,3> translation{};
};
// Shape order is RFLiInitCharModelRes / 8024b508: nose, forehead, face,
// hair, cap, beard, nose line, mask, glasses. Face resource anchors are
// nose, beard, then hair attachment points, each already in model units.
inline bool miiShapePlacements(const std::array<float,9>& anchors,
 unsigned noseScale,unsigned noseY,unsigned glassesScale,unsigned glassesY,
 bool hairFlip,std::array<MiiShapePlacement,9>& out){
 if(noseScale>15||noseY>31||glassesScale>15||glassesY>31)return false;
 for(float v:anchors)if(!std::isfinite(v)||std::fabs(v)>127.f)return false;
 std::array<MiiShapePlacement,9> p{};
 for(unsigned part:{1u,3u,4u}){p[part].transform=true;p[part].mirrorX=hairFlip;
  for(unsigned i=0;i<3;++i)p[part].translation[i]=anchors[6+i];}
 p[5].transform=true;
 for(unsigned i=0;i<3;++i)p[5].translation[i]=anchors[3+i];
 p[0].transform=true;p[0].scale=.4f+.175f*float(noseScale);
 p[0].translation={anchors[0],anchors[1]-1.5f*float(int(noseY)-8),anchors[2]};
 p[6]=p[0];
 p[8].transform=true;p[8].scale=.4f+.15f*float(glassesScale);
 p[8].translation={anchors[0],(5.f+anchors[1])-1.5f*float(int(glassesY)-11),2.f+anchors[2]};
 out=p;return true;
}
// The original loader quantizes scale and translation before applying them
// to S16 positions. Applying an ordinary float matrix gives different vertices.
inline std::array<std::int16_t,3> miiPlacePosition(
 const std::array<std::int16_t,3>& src,const MiiShapePlacement& p){
 std::array<std::int16_t,3> result{};
 auto wrap=[](std::int32_t v){auto u=std::uint32_t(v)&65535u;return std::int16_t(u<32768u?std::int32_t(u):std::int32_t(u)-65536);};
 const auto scale=std::int32_t(p.scale*256.f);
 for(unsigned i=0;i<3;++i){
  std::int32_t v=src[i];if(i==0&&p.mirrorX)v=-v;
  if(p.transform){
   v*=scale;
   // PPC srawi rounds negative products down, not toward zero.
   v=(v>=0?v/256:-((-v+255)/256))+std::int32_t(p.translation[i]*256.f);
  }
  result[i]=wrap(v);
 }
 return result;
}
inline std::array<std::int16_t,3> miiPlaceNormal(
 std::array<std::int16_t,3> n,const MiiShapePlacement& p){
 // Uniform scale/translation are baked only into positions; native normals
 // remain S16/14. Mirroring negates X (including S16 wrapping).
 if(p.mirrorX)n[0]=n[0]==-32768?-32768:std::int16_t(-n[0]);
 return n;
}
}
