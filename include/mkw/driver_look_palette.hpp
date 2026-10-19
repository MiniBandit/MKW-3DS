#pragma once
#include <mkw/math.hpp>
#include <cmath>
#include <cstdint>

namespace mkw {
// USA807c92b4 / 807c96b8: positive local-X rotations of root then spin.
// Rotate the complete subtree around that joint; descendants inherit the
// parent's transform before their own callback is applied.
inline void applyDriverBodyLookPalette(float* palette,unsigned joint,const uint32_t* slots,unsigned count,float degrees,float pitchDegrees=0,float rollDegrees=0){
 if(degrees==0&&pitchDegrees==0&&rollDegrees==0)return;
 const float* h=palette+joint*21;const Vec3 pivot{h[3],h[7],h[11]};
 constexpr float rad=3.14159265358979323846f/180.f;
 const float rc=std::cos(rollDegrees*rad),rs=std::sin(rollDegrees*rad);
 const float pc=std::cos(pitchDegrees*rad),ps=std::sin(pitchDegrees*rad);
 // Retail root/spin callbacks: driver-world Z roll, then world X pitch,
 // then item-look around the resulting local X (807ce6f4).
 auto tilt=[&](Vec3 v){
  v={rc*v.x-rs*v.y,rs*v.x+rc*v.y,v.z};
  return Vec3{v.x,pc*v.y-ps*v.z,ps*v.y+pc*v.z};
 };
 const Vec3 axis=normalized(tilt(Vec3{h[0],h[4],h[8]}),{0,1,0});
 const float angle=degrees*(3.14159265358979323846f/180.f),c=std::cos(angle),s=std::sin(angle);
 auto rotate=[&](Vec3 v){v=tilt(v);return v*c+cross(axis,v)*s+axis*(dot(axis,v)*(1-c));};
 for(unsigned n=0;n<count;++n){float* m=palette+slots[n]*21;
  for(unsigned col=0;col<3;++col){
   const Vec3 v=rotate({m[col],m[col+4],m[col+8]});m[col]=v.x;m[col+4]=v.y;m[col+8]=v.z;
   const Vec3 normal=rotate({m[12+col],m[15+col],m[18+col]});m[12+col]=normal.x;m[15+col]=normal.y;m[18+col]=normal.z;
  }
  const Vec3 p=pivot+rotate(Vec3{m[3],m[7],m[11]}-pivot);m[3]=p.x;m[7]=p.y;m[11]=p.z;
 }
}
// USA807c9250..807c9268 stores the driver's world X axis at callback +3d0.
// USA807c9af4..807c9b70 pitches around that axis, then applies negative yaw
// around the original head Y axis; preserve the joint's translation. Our
// palettes precede the driver world transform, so its X axis here is (1,0,0).
// Applying that same rigid delta to every descendant retains hair/hat offsets.
inline void applyDriverLookPalette(float* palette,unsigned head,const uint32_t* slots,
        unsigned count,float yawDegrees,float pitchDegrees,float rollDegrees=0){
    if(yawDegrees==0&&pitchDegrees==0&&rollDegrees==0)return;
    const float* h=palette+head*21;
    const Vec3 pivot{h[3],h[7],h[11]};
    const Vec3 x{1,0,0};
    const Vec3 y=normalized(Vec3{h[1],h[5],h[9]},{0,1,0});
    constexpr float radians=3.14159265358979323846f/180.f;
    const float rc=std::cos(rollDegrees*radians),rs=std::sin(rollDegrees*radians);
    const float pc=std::cos(pitchDegrees*radians),ps=std::sin(pitchDegrees*radians);
    const float yc=std::cos(-yawDegrees*radians),ys=std::sin(-yawDegrees*radians);
    auto rotate=[&](Vec3 v){
        // Head Y above is captured before both vehicle rotations (807c9aac).
        v={rc*v.x-rs*v.y,rs*v.x+rc*v.y,v.z};
        v=v*pc+cross(x,v)*ps+x*(dot(x,v)*(1-pc));
        return v*yc+cross(y,v)*ys+y*(dot(y,v)*(1-yc));
    };
    for(unsigned n=0;n<count;++n){
        float* m=palette+slots[n]*21;
        for(unsigned col=0;col<3;++col){
            const Vec3 v=rotate({m[col],m[col+4],m[col+8]});
            m[col]=v.x;m[col+4]=v.y;m[col+8]=v.z;
            const Vec3 normal=rotate({m[12+col],m[15+col],m[18+col]});
            m[12+col]=normal.x;m[15+col]=normal.y;m[18+col]=normal.z;
        }
        const Vec3 p=pivot+rotate(Vec3{m[3],m[7],m[11]}-pivot);
        m[3]=p.x;m[7]=p.y;m[11]=p.z;
    }
}
}
