#pragma once
#include <mkw/math.hpp>
#include <egg/math/Vector.hh>
#include <cstdint>

namespace Kinoko::Kart { class KartObject; }
namespace mkw {
struct RaceCameraInput;
class RaceCameraDirector;
// Presentation copy of the retail race camera's chase states (RaceCamera +0x19c
// forward, +0x268 backward; Kinoko Render::KartCamera has the subset the
// simulation needs) and the parts of RaceCamera::Update (PAL 0x805A21D0) that
// read the kart: calcCamera 0x805A34B0, calcCollision 0x805A5D70, the wall
// swing 0x805A4E34, the boost FOV add, the explosion lag and the half-pipe
// orbit. Kinoko keeps its own camera for the simulation's collision side
// effects; this copy is the one shown. Its course-KCL queries are read-only
// (KColData's lookup cursor is saved and restored around each one) and skip
// drivable objects, so they never change the simulation.
class RaceChaseCamera {
public:
 // kartCameraParam.bin entry (fov, dist, posY, targetPosY).
 struct Param {float fov=60,dist=0,posY=0,targetPosY=0;};
 // RaceCamera::Init (0x805A2034): the camera entry (getCameraEntries 0x805927D4:
 // Balloon Battle reads weight class + 3) and the course. Once per race.
 void setup(const Kinoko::Kart::KartObject& kart,bool battle);
 // RaceCamera::OnRespawn (0x805A49BC), the chase part.
 void init(const Kinoko::Kart::KartObject& kart);
 // One RaceCamera::Update after the simulation step, the director's phases
 // called in retail order. `lookBackHeld`: the look-behind button.
 void update(const Kinoko::Kart::KartObject& kart,RaceCameraDirector& director,RaceCameraInput& in,bool lookBackHeld);
 const Param& param()const{return param_;}
 // Flag 0x20 (0x805A66D8): the look-behind view is shown and Lakitu is hidden.
 bool lookBack()const{return lookBack_;}
 // RaceCamera +0x120: the boost FOV add (Kart::Link 0x80590E40 / 0x80590E58).
 float boostFov()const{return boostAdd_;}
 // The KMP cameras' push (0x805AAD08): course KCL, camera mask, read-only.
 static bool spherePush(float radius,Vec3 pos,Vec3 prev,Vec3& push,bool& wall);
 Vec3 eye(bool backward=false)const{const auto& s=state_[backward];return {s.pos.x,s.pos.y,s.pos.z};}
 Vec3 pivot(bool backward=false)const{const auto& s=state_[backward];return {s.targetPos.x,s.targetPos.y,s.targetPos.z};}
private:
 using V3=Kinoko::EGG::Vector3f;
 // KartCameraState (0xcc bytes; retail offsets).
 struct State {
  V3 pos,prevPos,targetPos;                // +0x00, +0x38, +0xac
  float bigAirHeight=0,pitchRate=0;        // +0x18, +0x1c
  float explosionY=0;                      // +0x20
  bool hit=false;                          // +0x28: a wall push this update
  float halfPipePush=0,halfPipePushTarget=0; // +0x44, +0x48
  float dist=0;                            // +0x4c
  V3 lastOffset;                           // +0x50
  float wallYawDrift=0;                    // +0x5c: yaw nudge along a touched wall
  float bigAirFallPitch=0;                 // +0x60
  int16_t wallTimer=0;                     // +0x64
  bool ssmtWall=false;                     // +0x6c
  float probeFwd=0,probeRev=0;             // +0x70, +0x74
  float wallYaw=0,wallPitch=0,wallYawTarget=0,wallPitchTarget=0; // +0x78..+0x84
  int16_t fwdTimer=0,revTimer=0,pitchTimer=0; // +0x94, +0x96, +0x98
  bool ssmtHold=false;                     // +0xa8
  V3 halfPipeWall;                         // +0xb8
  float halfPipeBlend=.1f;                 // +0xc4
  bool halfPipeBlendOn=false;              // +0xc8
 };
 // Retail KartHalfPipe camera fields Kinoko omits (KartHalfPipe::calc 0x80574340).
 struct HalfPipe {float elev=0,vspeed=0,sign=1;V3 dir,axis,takeoffAxis;}; // +0x14 +0x1c +0x44, +0x20 +0x2c +0x38
 void resetState(State& s);
 void events(const Kinoko::Kart::KartObject& kart,float shownEyeY);
 void calcForward(float t,const Kinoko::Kart::KartObject& kart);
 void calcDriftOffset(const Kinoko::Kart::KartObject& kart);
 void calcCamera(float horiz,float vert,float fastVert,State& s,bool backward,const Kinoko::Kart::KartObject& kart,const V3& targetPos,const V3& camTarget);
 void calcHalfPipeOrbit(V3& dir,const Kinoko::Kart::KartObject& kart);
 void calcAirtimeHeight(State& s,const Kinoko::Kart::KartObject& kart);
 void calcCollision(State& s,bool backward,const Kinoko::Kart::KartObject& kart,const V3& forward);
 void calcWallAvoid(State& s,bool backward,const Kinoko::Kart::KartObject& kart);
 bool probeStep(float& outDist,V3& outPos,float& slot,const Kinoko::Kart::KartObject& kart,int i,int timer,const V3& probe,const V3& dir,const V3& back,bool rev);
 bool wallCheck(State& s,const Kinoko::Kart::KartObject& kart,bool* floorHit,const V3& at,const V3& dir,const V3& probe,bool backward,bool rev);
 Param param_;
 int course_=-1;
 float driftYaw_=0,hopPosY_=0;
 V3 forward_,right_;                      // +0x12c, +0x138
 State state_[2];
 bool lookBack_=false;
 // Flag 1 (0x805A4DEC .. 0x805A4E20) with its rate (+0x190) and frame counter (+0x194).
 bool explosion_=false;float explosionRate_=1;int explosionFrames_=0;
 bool cannonLock_=false;                  // flag 0x40 (Rainbow Road cannon, 0x80590EA4)
 // Flag 0x200 with its frame counter (+0x33c): a car hit (ObjectCarTGE::onCollision -> 0x805A6DCC).
 bool carHit_=false;int16_t carHitTimer_=0;
 float halfPipeEase_=0;                   // +0x118
 V3 lookBackEye_,lookBackTarget_;         // +0xb8, +0xc4
 float boostAdd_=0,boostFactor_=0;        // RaceCamera +0x120, KartMove +0x124
 HalfPipe halfPipe_;
 // The kart at the end of the previous step: what this step's KartHalfPipe::calc read.
 struct Prev {V3 dir,up,bodyUp;float speed=0,vel1DirY=0;bool overZipper=false,stop=false,ramp=false;bool boost[3]{};uint32_t actionStarts=0,carHits=0;bool largeFlip=false,inCannon=false;} prev_;
};
}
