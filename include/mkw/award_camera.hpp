#pragma once
#include <mkw/math.hpp>
#include <algorithm>
#include <array>
#include <cstdint>

namespace mkw {
struct AwardCameraKey {float frame,value,tangent;};
struct AwardCameraTrack {const AwardCameraKey* keys;uint16_t count;bool hermite;};
// Original SCN0 channel order, including fields unused by a given camera type.
enum AwardCameraChannel {AwardPosX,AwardPosY,AwardPosZ,AwardAspect,AwardNear,AwardFar,
 AwardRotX,AwardRotY,AwardRotZ,AwardAimX,AwardAimY,AwardAimZ,AwardTwist,AwardFovy,AwardOrthoHeight,AwardCameraChannels};
struct AwardCameraClip {
 const char* name;float duration;bool loop;uint8_t cameraType,projectionType;
 std::array<AwardCameraTrack,AwardCameraChannels> tracks;
};
inline float sampleAwardTrack(const AwardCameraTrack& track,float frame){
 if(!track.count)return 0;
 const auto* keys=track.keys;
 if(track.count==1||frame<=keys[0].frame)return keys[0].value;
 unsigned upper=1;
 while(upper<track.count&&frame>=keys[upper].frame)++upper;
 if(upper==track.count)return keys[upper-1].value;
 const auto& a=keys[upper-1];const auto& b=keys[upper];
 const float length=b.frame-a.frame,t=(frame-a.frame)/length;
 if(!track.hermite){
  // NW4R sampled linear channels use periodic angle interpolation.
  const float angle=std::fmod(b.value-a.value,180.f);
  return a.value+(std::fmod(2*angle,180.f)-angle)*t;
 }
 const float t2=t*t,t3=t2*t;
 return (2*t3-3*t2+1)*a.value+(t3-2*t2+t)*a.tangent*length+
        (-2*t3+3*t2)*b.value+(t3-t2)*b.tangent*length;
}
struct AwardCameraSample {
 Vec3 position{},rotation{},aim{};float twist=0,fovy=45,aspect=5.f/3,near=1,far=100000,orthoHeight=0;
 uint8_t cameraType=0,projectionType=0;
};
inline AwardCameraSample sampleAwardCamera(const AwardCameraClip& clip,float frame){
 frame=std::isfinite(frame)?std::max(0.f,frame):0;
 if(clip.loop&&clip.duration>0&&frame>clip.duration){
  frame=std::fmod(frame,clip.duration);if(frame==0)frame=clip.duration;
 }else frame=std::min(frame,clip.duration);
 auto s=[&](unsigned i){return sampleAwardTrack(clip.tracks[i],frame);};
 AwardCameraSample result;
 result.position={s(AwardPosX),s(AwardPosY),s(AwardPosZ)};
 result.rotation={s(AwardRotX),s(AwardRotY),s(AwardRotZ)};
 result.aim={s(AwardAimX),s(AwardAimY),s(AwardAimZ)};
 result.twist=s(AwardTwist);result.fovy=s(AwardFovy);result.aspect=s(AwardAspect);
 result.near=s(AwardNear);result.far=s(AwardFar);result.orthoHeight=s(AwardOrthoHeight);
 result.cameraType=clip.cameraType;result.projectionType=clip.projectionType;
 return result;
}
struct AwardCameraMatrices {std::array<float,12> view{};std::array<float,16> projection{};};
inline AwardCameraMatrices awardCameraMatrices(const AwardCameraSample& c,float aspect=5.f/3){
 constexpr float radians=0.01745329252f;
 Vec3 right,up,back;
 if(c.cameraType==0){
  // Camera::UpdateCameraMtx, USA main.dol 8006ad84..8006aef0.
  // NW4R's camera rotation is not the usual model SRT inverse order.
  const float sx=std::sin(c.rotation.x*radians),cx=std::cos(c.rotation.x*radians);
  const float sy=std::sin(c.rotation.y*radians),cy=std::cos(c.rotation.y*radians);
  const float sz=std::sin(c.rotation.z*radians),cz=std::cos(c.rotation.z*radians);
  right={sx*sy*sz+cy*cz,cx*sz,sx*cy*sz-sy*cz};
  up={sx*sy*cz-cy*sz,cx*cz,sx*cy*cz+sy*sz};
  back={cx*sy,-sx,cx*cy};
 }else{
  back=normalized(c.position-c.aim,{0,0,1});
  // The original vertical aim special case ignores twist.
  if(back.x==0&&back.z==0){right={1,0,0};up={0,0,back.y<=0?1.f:-1.f};}
  else{
   const auto r=normalized(Vec3{back.z,0,-back.x});const auto u=cross(back,r);
   const float s=std::sin(c.twist*radians),k=std::cos(c.twist*radians);
   right=r*k+u*s;up=u*k-r*s;
  }
 }
 AwardCameraMatrices result;
 result.view={right.x,right.y,right.z,-dot(right,c.position),
              up.x,up.y,up.z,-dot(up,c.position),
              back.x,back.y,back.z,-dot(back,c.position)};
 auto& p=result.projection;
 const float range=c.far-c.near;
 if(c.projectionType==0){
  // C_MTXPerspective: source depth is -1 at near and 0 at far.
  const float cot=1.f/std::tan(c.fovy*.5f*radians);
  p[0]=cot/aspect;p[5]=cot;p[10]=-c.near/range;p[11]=-c.far*c.near/range;p[14]=-1;
 }else{
  p[0]=2.f/(c.orthoHeight*aspect);p[5]=2.f/c.orthoHeight;
  p[10]=-1.f/range;p[11]=-c.far/range;p[15]=1;
 }
 return result;
}
class Renderer;
void beginAwardCamera(Renderer& renderer,const AwardCameraClip& clip,float frame);
}
