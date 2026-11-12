#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace mkw {
struct MiiIconCamera {
 std::array<float,3> eye{0,34.5f,600},target{0,34.5f,0},up{0,1,0};
 float fovDegrees=0,aspect=1,nearPlane=500,farPlane=700;
};
// RFLiMakeIcon RMCE01 800c4fe8..800c50e4. Portrait crops widen the
// vertical FOV so horizontal framing stays fixed; landscape keeps vertical FOV.
inline bool miiIconCamera(unsigned width,unsigned height,MiiIconCamera& out){
 if(!width||!height||width>1024||height>1024)return false;
 MiiIconCamera c;c.aspect=float(width)/float(height);
 const float half=width<height?43.20000076293945f/c.aspect:43.20000076293945f;
 const float angle=float(std::atan2(double(half),500.));
 c.fovDegrees=2.f*(57.295780181884766f*angle);out=c;return true;
}
// Original MakeIcon light at (1600,1500,6000), white, diffuse CLAMP,
// attenuation NONE, RGB ambient 160, white material and unlit opaque alpha.
// View transform preserves this world-space dot product. This is vertex
// lighting, evaluated once for a static icon, not a replacement for race lights.
inline std::uint8_t miiIconVertexLight(const std::array<float,3>& p,const std::array<float,3>& n){
 const float x=1600.f-p[0],y=1500.f-p[1],z=6000.f-p[2];
 const float d=std::sqrt(x*x+y*y+z*z);
 const float diffuse=d>0?std::max(0.f,(x*n[0]+y*n[1]+z*n[2])/d):0;
 return std::uint8_t(std::clamp(160.f+255.f*diffuse+.5f,0.f,255.f));
}
}
