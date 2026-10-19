#pragma once
#include <mkw/math.hpp>
#include <cmath>
namespace mkw {
struct DriverLookTargetAngles {float yaw=0,pitch=0,kartYaw=0;bool valid=false;};
// Head X points forward, head Y points down. Model Z is kart forward.
// Compose with the seated world matrix before projecting the world target;
// normalize axes so vehicle/driver scale cannot change the requested angles.
inline DriverLookTargetAngles driverLookTargetAngles(const float* world,const float* head,Vec3 target){
 auto vector=[&](Vec3 v){return Vec3{world[0]*v.x+world[1]*v.y+world[2]*v.z,
  world[4]*v.x+world[5]*v.y+world[6]*v.z,world[8]*v.x+world[9]*v.y+world[10]*v.z};};
 auto unit=[](Vec3 v){const float n=dot(v,v);return n>1.e-12f?v/std::sqrt(n):Vec3{};};
 const Vec3 origin=vector({head[3],head[7],head[11]})+Vec3{world[3],world[7],world[11]};
 const Vec3 delta=target-origin;
 const Vec3 x=unit(vector({head[0],head[4],head[8]}));
 const Vec3 y=unit(vector({head[1],head[5],head[9]}));
 const Vec3 z=unit(vector({head[2],head[6],head[10]}));
 DriverLookTargetAngles a;
 if(dot(delta,delta)<1.e-12f||dot(x,x)<.5f||dot(y,y)<.5f||dot(z,z)<.5f)return a;
 constexpr float deg=180.f/3.14159265358979323846f;
 a.yaw=std::atan2(dot(delta,z),dot(delta,x))*deg;
 a.pitch=std::atan2(dot(delta,y),dot(delta,x))*deg;
 const Vec3 fromKart=target-Vec3{world[3],world[7],world[11]};
 a.kartYaw=std::atan2(dot(fromKart,unit(vector({1,0,0}))),dot(fromKart,unit(vector({0,0,1}))))*deg;
 a.valid=std::isfinite(a.yaw)&&std::isfinite(a.pitch)&&std::isfinite(a.kartYaw);
 return a;
}
}
