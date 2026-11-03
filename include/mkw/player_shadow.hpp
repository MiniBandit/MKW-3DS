#pragma once
#include <mkw/math.hpp>
#include <algorithm>
#include <mkw/retail_lighting.hpp>
namespace mkw::playerShadow {
inline constexpr unsigned Size=128;
// Requested wheel-to-support gap limit. The original hop apex (~33.26)
// crosses it; smoothstep fades coverage to zero before the 25-unit cutoff.
inline constexpr float contactHeightTolerance(){return 25.f;}
inline float contactOpacity(float wheelRadius,float heightAbovePlane){
 if(wheelRadius<=0.f)return 0.f;
 const float t=std::clamp(1.f-std::max(0.f,heightAbovePlane-wheelRadius)/contactHeightTolerance(),0.f,1.f);
 return t*t*(3.f-2.f*t);
}
inline bool contactVisible(bool /*grounded*/,float wheelRadius,float heightAbovePlane){
 return contactOpacity(wheelRadius,heightAbovePlane)>0.f;
}
// HW65 Grumble Volcano has no GOBJ sun. Its first enabled world light is
// (1,-1,-1)/sqrt(3): elevation atan(1/sqrt(2)), 35.264 degrees.
inline constexpr float VolcanoLengthPerHeight=1.41421356237f;
inline constexpr float MaxLengthPerHeight=VolcanoLengthPerHeight*1.15f;
inline Vec3 limitedDirection(Vec3 d){
 const float horizontal=std::sqrt(d.x*d.x+d.z*d.z);
 if(!(horizontal>1e-5f))return {0,-1,0};
 const float down=std::max(-d.y,horizontal/MaxLengthPerHeight);
 return normalized({d.x,-down,d.z},{0,-1,0});
}
// Bound downhill horizontal reach to 1.5 times the same-height flat projection.
// Preserve azimuth; elevate only where the support slope exceeds that bound.
inline Vec3 directionOnSupport(Vec3 d,Vec3 n){
 d=limitedDirection(d);n=normalized(n,{0,1,0});
 const float h=std::hypot(d.x,d.z);
 if(h<1e-5f)return d;
 const float limit=1.5f*h/(-d.y);
 if(n.y<=1e-4f)return {0,-1,0};
 const float ny=n.y;
 const float down=std::max(-d.y,(n.x*d.x+n.z*d.z)/ny+h/limit);
 return normalized({d.x,-down,d.z},{0,-1,0});
}
// Body roll about its forward axis, relative to world horizontal. Using
// right.y/up.y cancels ordinary pitch; wheel spin and steering are irrelevant.
inline bool hideContactForRoll(Vec3 right,Vec3 up){
 right=normalized(right,{1,0,0});up=normalized(up,{0,1,0});
 return up.y<0.f||std::fabs(right.y)>0.83909963118f*up.y; // tan(40 degrees)
}
// Authoritative GX blend/Z state, already preserved by gx_material.py.
// Light shafts/glow/decal alpha passes must never become solid receivers.
inline bool receives(unsigned flags){
 // Authored alpha-blended sand still writes solid scene depth. Exclude
 // additive glow, then trust the explicit GX depth state for both passes.
 if(flags&(1u<<31))return !(flags&64)&&(flags&(1u<<23))&&(flags&(1u<<24));
 return !(flags&2);
}
// A receiver pass repeats the depth-producing mesh after the scene. Two D24
// units left visible stippling on Luigi's sloped straight. Reserve one small
// coplanar-layer interval for replay precision; keep the real scene depth test.
inline constexpr float ReceiverReplayBias=32.f/16777215.f;
inline float receiverBias(unsigned lighting){
 return (lighting&retail::RaisedRoadLayer?512.f:lighting&retail::CoplanarLayer?32.f:0.f)/16777215.f;
}
// Retail EGG ShadowTexture: light-space orthographic silhouette, projected
// back onto receivers. PICA uses one 128-square target for the local racer.
inline Vec3 direction(const CourseLighting* course){
 if(course)for(const auto& l:course->lights){
  if(!(l.flags&1)||l.space!=0||l.type!=1)continue;
  Vec3 d=normalized({l.direction[0],l.direction[1],l.direction[2]},{});
  if(d.y<-.25f)return d;
 }
 return normalized(Vec3{.45f,-1.f,.35f});
}
inline bool intersects(Vec3 lo,Vec3 hi,Vec3 center,float radius){
 return hi.x>=center.x-radius&&lo.x<=center.x+radius&&
        hi.z>=center.z-radius&&lo.z<=center.z+radius&&
        hi.y>=center.y-1800.f&&lo.y<=center.y+120.f;
}
}
