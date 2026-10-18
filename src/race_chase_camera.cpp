#include <mkw/race_chase_camera.hpp>
#include <mkw/race_camera.hpp>
#include <game/field/CourseColMgr.hh>
#include <game/field/KColData.hh>
#include <game/kart/KartAction.hh>
#include <game/kart/KartCollide.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartObject.hh>
#include <game/kart/KartParam.hh>
#include <game/kart/KartState.hh>
#include <game/system/RaceConfig.hh>
#include <game/system/RaceManager.hh>
#include <game/system/ResourceManager.hh>
#include <egg/math/Matrix.hh>
#include <egg/util/Stream.hh>
#include <algorithm>
#include <limits>

namespace mkw {
namespace K=Kinoko;
using K::EGG::Vector3f;
using St=K::Kart::eStatus;
namespace {
Vec3 vec(const Vector3f& v){return {v.x,v.y,v.z};}
Vector3f egg(Vec3 v){return {v.x,v.y,v.z};}
constexpr K::Field::KCLTypeMask CameraMask=0x62F8BDFF;      // KCL_TYPE_CAMERA_COLLIDABLE
constexpr K::Field::KCLTypeMask ForwardProbeMask=0x4210B000; // 0x805A57CC forward probe
constexpr K::Field::KCLTypeMask WeakWallMask=0x2000000;     // COL_TYPE_WEAK_WALL (soft-wall pass)
constexpr float GbaShyGuyBeachFloor=3050;                   // 0x80892354

// The no-bounce (soft wall) record of the soft-wall pass (CourseModel::setSoftWallColInfo).
struct SoftWall {K::EGG::BoundBox3f bbox;Vector3f tangentOff;float dist=0;Vector3f fnrm;};

// Retail CourseModel::checkSphereFullPush (PAL 0x8078F784, Kinoko
// CollisionDirector::checkSphereFullPush) on the course KCL, as a read-only
// query. What it touches and how the simulation is protected:
//  - KColData's lookup cursor (pos, prevPos, movement, radius, typeMask and
//    prismIter: all lookupSphere sets and the checkSphere iterator advances;
//    the prism tests only read) is saved first and restored after, so the next
//    simulation lookup starts from the state the simulation left. The prism
//    cache of the cached lookups (narrowScopeLocal) is not touched.
//  - Nothing goes through CollisionDirector (its entry array, count and closest
//    entry stay as they are) or CourseColMgr (m_kclScale, the no-bounce record
//    and m_localMtx stay as they are: the camera's scale is 1 and the record is
//    null outside a kart query, so the arithmetic is unchanged).
//  - Drivable objects (ObjDrivableManager, which retail also queries here) are
//    skipped: their queries update ObjectKCL matrices and BoxColManager search
//    state that the simulation reads.
bool courseSphere(float radius,const Vector3f& pos,const Vector3f& prev,K::Field::KCLTypeMask mask,
                  K::Field::CollisionInfo& info,K::Field::KCLTypeMask& maskOut,SoftWall* soft=nullptr){
 info.reset();maskOut=0;
 // The no-bounce record starts at -FLT_MIN (0x8078F858, 0x808A4BE8), so a hit with no
 // soft-wall triangle fails the caller's > -FLT_MIN test (0x808923F4) and its normal is
 // never read. Retail leaves that normal as it was; zeroed here, it is never read stale.
 if(soft){soft->bbox.setZero();soft->dist=-std::numeric_limits<float>::min();soft->fnrm.setZero();}
 auto* mgr=K::Field::CourseColMgr::Instance();
 if(!mask||!mgr||!mgr->data())return false;
 // CourseColMgr's data pointer is mutable; only the saved cursor changes.
 auto* data=const_cast<K::Field::KColData*>(mgr->data());
 const auto saved=data->lookupState();
 data->lookupSphere(radius,pos,prev,mask);
 bool hit=false;float dist=0;Vector3f fnrm;uint16_t attribute=0;
 while(data->checkSphereCollision(&dist,&fnrm,&attribute)){
  if(soft&&(attribute&KCL_SOFT_WALL_MASK)){
   const Vector3f offset=fnrm*dist;
   soft->bbox.min=soft->bbox.min.minimize(offset);soft->bbox.max=soft->bbox.max.maximize(offset);
   if(soft->dist<dist){soft->dist=dist;soft->fnrm=fnrm;}
  }else{
   const uint32_t bit=KCL_ATTRIBUTE_TYPE_BIT(attribute);
   maskOut|=bit;
   if(bit&KCL_TYPE_SOLID_SURFACE)info.update(dist,fnrm*dist,fnrm,bit);
  }
  hit=true;
 }
 data->restoreLookupState(saved);
 if(hit){info.tangentOff=info.bbox.min+info.bbox.max;if(soft)soft->tangentOff=soft->bbox.min+soft->bbox.max;}
 return hit;
}
// fn_1_93B2C (PAL 0x805A3EE0): the unsigned angle between two vectors, radians.
float angleBetween(const Vector3f& a,const Vector3f& b){
 const float d=a.z*b.z+(a.x*b.x+a.y*b.y);
 const Vector3f c=a.cross(b);
 const float r=K::EGG::Mathf::atan2(K::EGG::Mathf::sqrt(c.z*c.z+c.x*c.x+c.y*c.y),d);
 return r<=0?-r:r;
}
}

bool RaceChaseCamera::spherePush(float radius,Vec3 pos,Vec3 prev,Vec3& push,bool& wall){
 K::Field::CollisionInfo info;K::Field::KCLTypeMask mask=0;
 if(!courseSphere(radius,egg(pos),egg(prev),CameraMask,info,mask))return false;
 // findClosestCollisionEntry(walls) finds an entry exactly when the mask has a wall type.
 push=vec(info.tangentOff);wall=(mask&KCL_TYPE_WALL)!=0;
 return true;
}

// RaceCamera::Init (PAL 0x805A2034): getCameraEntries (0x805927D4) picks the
// 16:9 1P entry of the weight class, three classes on in a battle.
void RaceChaseCamera::setup(const K::Kart::KartObject& kart,bool battle){
 const auto& p=kart.param()->camera();
 param_={p.fov,p.dist,p.posY,p.targetPosY};
 if(battle){
  size_t size=0;
  const auto* file=static_cast<const uint8_t*>(K::System::ResourceManager::Instance()->getFile("kartCameraParam.bin",&size,K::System::ArchiveId::Core));
  const int weight=static_cast<int>(kart.param()->stats().weightClass);
  const size_t at=(size_t(std::max(weight,0))+3)*4*16+16;
  if(file&&weight>=0&&at+16<=size){
   K::EGG::RamStream stream(file+at,16);
   const K::Kart::KartParam::KartCameraParam b(stream);
   param_={b.fov,b.dist,b.posY,b.targetPosY};
  }
 }
 course_=int(K::System::RaceConfig::Instance()->raceScenario().course);
 explosion_=cannonLock_=false;explosionRate_=1;explosionFrames_=0;
 boostAdd_=0;boostFactor_=0;halfPipe_={};halfPipeEase_=0;lookBack_=false;
 lookBackEye_=lookBackTarget_=Vector3f::zero;hopPosY_=0;
 // The step before the race camera exists: no event is pending.
 const auto& st=kart.status();
 const auto* move=kart.move();
 const auto* action=kart.action();
 prev_={};
 prev_.actionStarts=action->startCount();
 prev_.largeFlip=action->currentAction()==K::Kart::Action::UNK_7||action->currentAction()==K::Kart::Action::UNK_8;
 prev_.inCannon=st.onBit(St::InCannon);prev_.ramp=st.onBit(St::RampBoost);prev_.overZipper=st.onBit(St::OverZipper);
 for(unsigned t=0;t<3;++t)prev_.boost[t]=move->boostActive(K::Kart::KartBoost::Type(t));
 prev_.dir=move->dir();prev_.up=move->up();prev_.bodyUp=kart.bodyUp();
 prev_.speed=move->speed();prev_.vel1DirY=move->vel1Dir().y;
}

// fn_1_91888 (PAL 0x805A1C3C); the eye, sweep start, target, airtime height,
// pitch rate, explosion height, hit byte and half-pipe lean are kept.
void RaceChaseCamera::resetState(State& s){
 State r;
 r.pos=s.pos;r.prevPos=s.prevPos;r.targetPos=s.targetPos;
 r.bigAirHeight=s.bigAirHeight;r.pitchRate=s.pitchRate;r.explosionY=s.explosionY;r.hit=s.hit;
 r.halfPipePush=s.halfPipePush;r.halfPipePushTarget=s.halfPipePushTarget;
 r.lastOffset=Vector3f::ey;
 s=r;
}

// RaceCamera::OnRespawn (PAL 0x805A49BC), chase part: drift yaw and flags
// cleared, both states reset, the forward one recomputed at rate 1.
void RaceChaseCamera::init(const K::Kart::KartObject& kart){
 driftYaw_=0;explosion_=cannonLock_=false;
 for(auto& s:state_){resetState(s);s.dist=param_.dist;}
 calcForward(1.0f,kart);
 calcCamera(1.0f,1.0f,1.0f,state_[0],false,kart,kart.pos(),kart.pos());
}

// Kart::Link calls made during the simulation step, as the step left the kart.
void RaceChaseCamera::events(const K::Kart::KartObject& kart,float shownEyeY){
 const auto& st=kart.status();
 const auto* move=kart.move();
 const auto* action=kart.action();
 using T=K::Kart::KartBoost::Type;
 const bool inCannon=st.onBit(St::InCannon);
 // enterCannon (0x8058498C): boost add cleared (0x80590E58), explosion ended
 // (0x80590E8C), and on Rainbow Road the camera locked behind (0x80590EA4).
 // Leaving the cannon clears the lock (0x80590ED8 from updateCannon / 0x805852C8).
 if(inCannon&&!prev_.inCannon){
  boostAdd_=0;explosion_=false;
  if(course_==int(K::Course::Rainbow_Road))cannonLock_=true;
 }else if(!inCannon&&prev_.inCannon)cannonLock_=false;
 // startLargeFlipAction (0x805690A0) -> 0x805A4DEC: both states hold the shown
 // eye height, flag 1, rate 1. Actions 7 and 8 are the large flips.
 const bool largeFlip=action->currentAction()==K::Kart::Action::UNK_7||action->currentAction()==K::Kart::Action::UNK_8;
 if(action->startCount()!=prev_.actionStarts&&(action->lastStarted()==K::Kart::Action::UNK_7||action->lastStarted()==K::Kart::Action::UNK_8)){
  for(auto& s:state_)s.explosionY=shownEyeY;
  explosion_=true;explosionRate_=1;explosionFrames_=0;
 }else if(explosion_&&prev_.largeFlip&&!st.onBit(St::LargeFlipHit)&&(largeFlip||action->currentAction()==K::Kart::Action::None)){
  // calcLargeFlipAction (0x805692B4) calls 0x80590E8C once the kart is down
  // after 40 frames: the frame its LargeFlipHit status is cleared.
  explosion_=false;
 }
 // Boost FOV: every boost frame adds 12 x the factor of the last boost started
 // (KartMove +0x124: MT/start/respawn 0.5, mushroom/panel 1.0, zipper/trick
 // 0.85), kept at the largest; it is cleared when no boost runs or the ramp
 // boost ends (calcBoost 0x80582694, calcRampBoost 0x80582804).
 constexpr float Factors[3]={.5f,1.f,.85f}; // 0x808B5C90, 0x808B5C98, 0x808B5CA0
 bool boost[3];
 for(unsigned t=0;t<3;++t){boost[t]=move->boostActive(T(t));if(boost[t]&&!prev_.boost[t])boostFactor_=Factors[t];}
 if(st.onBit(St::Boost))boostAdd_=std::max(boostAdd_,12.f*boostFactor_);else boostAdd_=0;
 const bool ramp=st.onBit(St::RampBoost);
 if(ramp)boostAdd_=std::max(boostAdd_,12.f*boostFactor_);
 else if(prev_.ramp)boostAdd_=0;
 // KartHalfPipe::calc (0x80574340) camera fields, from the kart as this step's
 // half-pipe pass read it (the end of the previous step).
 const bool over=st.onBit(St::OverZipper);
 if(over&&!prev_.overZipper){
  V3 upXZ=prev_.up;upXZ.y=0;upXZ.normalise();
  auto& h=halfPipe_;
  h.dir=prev_.dir.perpInPlane(upXZ,true);
  const float c=std::clamp(h.dir.dot(Vector3f::ey),-1.f,1.f);
  h.elev=std::clamp(90.f-57.29578f*K::EGG::Mathf::acos(c),0.f,180.f);
  h.vspeed=prev_.vel1DirY*prev_.speed;
  h.axis=prev_.bodyUp.perpInPlane(h.dir,true);
  h.takeoffAxis=h.axis;
  h.sign=h.dir.cross(h.axis).dot(Vector3f::ey)<=0?-1.f:1.f;
 }else if(prev_.overZipper&&!prev_.stop){
  halfPipe_.dir=halfPipe_.dir.perpInPlane(prev_.up,true);
  halfPipe_.axis=halfPipe_.axis.perpInPlane(halfPipe_.dir,true);
 }
 prev_.inCannon=inCannon;prev_.actionStarts=action->startCount();prev_.largeFlip=largeFlip;
 for(unsigned t=0;t<3;++t)prev_.boost[t]=boost[t];
 prev_.ramp=ramp;prev_.overZipper=over;
 prev_.stop=st.onBit(St::InAction)||st.onBit(St::Bullet)||kart.collide()->surfaceFlags().onBit(K::Kart::KartCollide::eSurfaceFlags::StopHalfPipeState);
 prev_.dir=move->dir();prev_.up=move->up();prev_.bodyUp=kart.bodyUp();
 prev_.speed=move->speed();prev_.vel1DirY=move->vel1Dir().y;
}

// RaceCamera::Update (PAL 0x805A21D0).
void RaceChaseCamera::update(const K::Kart::KartObject& kart,RaceCameraDirector& director,RaceCameraInput& in,bool lookBackHeld){
 constexpr float HOP_POS_INTERP_RATE=0.8f;
 constexpr float FORWARD_INTERP_RATE=0.35f;
 constexpr float HORIZ_POS_INTERP_RATE=0.05f;
 constexpr float VERT_POS_INTERP_RATE=0.03f;
 constexpr float FAST_VERT_POS_INTERP_RATE=0.1f;
 const auto& st=kart.status();
 events(kart,director.view().eye.y);
 // 0x805A66D8: only once the race runs and not around a respawn.
 lookBack_=lookBackHeld&&K::System::RaceManager::Instance()->isStageReached(K::System::RaceManager::Stage::Race)&&
           st.offBit(St::BeforeRespawn)&&st.offBit(St::InRespawn)&&st.offBit(St::AfterRespawn);
 Vector3f targetPos=kart.pos();
 hopPosY_+=HOP_POS_INTERP_RATE*(kart.move()->hopPosY()-hopPosY_);
 targetPos.y-=hopPosY_;
 calcForward(FORWARD_INTERP_RATE,kart);
 // fn_1_92948 (0x805A2CFC) in the director, before this update's orbit.
 const bool over=st.onBit(St::OverZipper);
 in.lookBack=lookBack_;in.chaseEye=vec(state_[0].pos);in.chasePivot=vec(targetPos);
 in.explosion=explosion_;in.halfPipe=over;in.falling=kart.velocity().y<0;in.halfPipeEase=halfPipeEase_;
 in.boostFov=boostAdd_;in.course=course_;in.spherePush=&RaceChaseCamera::spherePush;
 in.cameraFov=param_.fov;in.targetPosY=param_.targetPosY;
 const bool live=director.beginStep(in);
 calcDriftOffset(kart);
 // fn_1_930B4 (0x805A3468): the explosion's vertical rate decays to 0.07.
 if(explosion_&&++explosionFrames_>0){explosionRate_-=0.05f;if(explosionRate_<0.07f)explosionRate_=0.07f;}
 const Vector3f camTarget=egg(director.target());
 for(bool backward:{false,true})
  calcCamera(HORIZ_POS_INTERP_RATE,VERT_POS_INTERP_RATE,FAST_VERT_POS_INTERP_RATE,state_[backward],backward,kart,targetPos,camTarget);
 // The shown forward eye is taken before this update's collision.
 in.chaseEye=vec(state_[0].pos);
 in.holdFollow=state_[0].wallPitchTarget>0.5f||state_[0].hit;
 if(live)director.holdEye(in);
 const Vector3f forward=kart.move()->smoothedForward();
 calcCollision(state_[0],false,kart,forward);calcWallAvoid(state_[0],false,kart);
 calcCollision(state_[1],true,kart,-forward);calcWallAvoid(state_[1],true,kart);
 in.chaseBackEye=vec(state_[1].pos);in.lookBackTarget=vec(lookBackTarget_);
 // 0x805A40D0: the up vector's target in the chase view.
 Vector3f up=Vector3f::ey;float rate=0.1f;
 if(!lookBack_){
  if(over&&halfPipe_.elev>20.f&&halfPipe_.vspeed>30.f){
   if(kart.velocity().y<=0){rate=0.01f;up=halfPipe_.axis;}else up=halfPipe_.dir;
  }
 }else if(!over){
  if(st.onBit(St::HalfPipeRamp)){up=kart.move()->smoothedUp();rate*=0.02f;}
 }else{up=halfPipe_.axis;rate*=0.02f;}
 in.upTarget=vec(up);in.upRate=rate;
 if(live)director.endStep(in);
}

void RaceChaseCamera::calcForward(float t,const K::Kart::KartObject& kart){
 forward_=forward_+(kart.move()->smoothedForward()-forward_)*t;
 right_=Vector3f::ey.perpInPlane(forward_,true);
}

// KartCamera::calcDriftOffset (PAL 0x805A3070).
void RaceChaseCamera::calcDriftOffset(const K::Kart::KartObject& kart){
 constexpr float MAX_DRIFT_YAW_AUTOMATIC=10.0f;
 constexpr float MAX_DRIFT_YAW_MANUAL=15.0f;
 constexpr float YAW_STEP_AUTOMATIC=0.5f;
 constexpr float YAW_STEP_MANUAL=1.0f;
 const auto& status=kart.status();
 if(status.onBit(St::DriftManual,St::Hop)){
  float yaw=static_cast<float>(kart.hopStickX())*-0.5f;
  if(kart.vehicleType()==K::Kart::KartParam::Stats::DriftType::Inside_Drift_Bike)yaw*=-1.0f;
  driftYaw_+=yaw;
  float yawMax=status.onBit(St::AutoDrift)?MAX_DRIFT_YAW_AUTOMATIC:MAX_DRIFT_YAW_MANUAL;
  float pitch=90.0f-K::RAD2DEG*K::EGG::Mathf::acos(forward_.dot(Vector3f::ey));
  if(pitch<0.0f)yawMax+=0.1f*K::EGG::Mathf::abs(pitch);
  driftYaw_=std::clamp(driftYaw_,-yawMax,yawMax);
 }else if(driftYaw_!=0.0f){
  float yawStep=status.onBit(St::AutoDrift)?YAW_STEP_AUTOMATIC:YAW_STEP_MANUAL;
  if(driftYaw_>0.0f)driftYaw_=std::max(0.0f,driftYaw_-yawStep);
  else driftYaw_=std::min(0.0f,driftYaw_+yawStep);
 }
}

// fn_1_930FC (PAL 0x805A34B0). Kinoko's KartCamera::calcCamera is the plain
// race path of this; the wall swing (+0x78/+0x7c), the wall yaw nudge (+0x5c),
// the cannon, half-pipe, explosion and look-back branches are retail's.
void RaceChaseCamera::calcCamera(float horizInterpRate,float vertInterpRate,float fastVertInterpRate,State& s,bool backward,
                                 const K::Kart::KartObject& kart,const Vector3f& targetPos,const Vector3f& camTarget){
 constexpr float BIG_AIR_PITCH_CAP=89.0f*K::DEG2RAD;
 const auto& status=kart.status();
 const auto* move=kart.move();
 const bool over=status.onBit(St::OverZipper);
 Vector3f forwardDir=backward?-forward_:forward_;
 Vector3f pitchAxis=right_.cross(forwardDir);
 pitchAxis.normalise();
 float driftYaw=K::DEG2RAD*driftYaw_;
 if(status.onBit(St::AutoDrift))driftYaw*=-1.0f;
 float bigAirPitch=std::min(BIG_AIR_PITCH_CAP,s.bigAirFallPitch+s.wallPitch);
 K::EGG::Matrix34f bigAirPitchMat;
 K::EGG::Matrix34f yawRotMat;
 yawRotMat.setAxisRotation(s.wallYaw+(driftYaw+s.wallYawDrift),right_);
 bigAirPitchMat.setAxisRotation(bigAirPitch,pitchAxis);
 Vector3f orbitDir=yawRotMat.ps_multVector(bigAirPitchMat.ps_multVector(forwardDir));
 const float pitch=90.0f-K::RAD2DEG*K::EGG::Mathf::acos(std::clamp(orbitDir.dot(Vector3f::ey),-1.0f,1.0f));
 if(pitch>0.0f){
  K::EGG::Matrix34f downPitchMat;
  float fVar16=status.onBit(St::TouchingGround)?0.1f:0.6f;
  s.pitchRate=s.pitchRate+0.2f*(fVar16-s.pitchRate);
  downPitchMat.setAxisRotation(K::DEG2RAD*pitch*s.pitchRate,pitchAxis);
  orbitDir=downPitchMat.ps_multVector(orbitDir);
 }
 orbitDir*=-1.0f;
 bool storeOffset=false;
 if(!backward||!over){
  storeOffset=true;
  // 0x805A47A4: in a half-pipe the orbit turns about the jump's axis.
  if(!over)halfPipeEase_=0;
  else{orbitDir=-halfPipe_.dir;calcHalfPipeOrbit(orbitDir,kart);}
  // The eye leans out along the axis by the axis' turn since take-off.
  bool pushed=false;
  if(over){
   const float turn=angleBetween(halfPipe_.axis,halfPipe_.takeoffAxis)*57.29578f;
   if(turn>0&&halfPipe_.takeoffAxis.dot(halfPipe_.dir)>0){
    s.halfPipePushTarget=std::min(100.f,0.7f*turn);
    s.halfPipePush+=0.1f*(s.halfPipePushTarget-s.halfPipePush);
    s.pos+=halfPipe_.axis*-s.halfPipePush;
    pushed=true;
   }
  }
  if(!pushed)s.halfPipePush=0;
  calcAirtimeHeight(s,kart);
  // In a cannon the height is 200 (0x80892390); a zipper boost lowers it to 0.6.
  const float posY=status.onBit(St::InCannon)?200.f:param_.posY;
  float height=(posY-kart.cameraDistY())+s.bigAirHeight;
  height*=status.onBit(St::ZipperBoost)?0.6f:1.0f;
  s.pos+=targetPos-s.targetPos;
  Vector3f posOffset=s.dist*orbitDir;
  posOffset.y+=height;
  if(backward){
   // Looking back down a slope lifts the eye and drops the target (+0xb8 / +0xc4).
   Vector3f eyeTo=Vector3f::zero,targetTo=Vector3f::zero;
   if(pitch<-25.f){
    const Vector3f up=kart.mainRot().rotateVector(Vector3f::ey);
    const float d=25.f+pitch;
    eyeTo=up*(3.f*d);targetTo=up*(-3.f*d);
   }
   lookBackEye_+=(eyeTo-lookBackEye_)*0.3f;
   lookBackTarget_+=(targetTo-lookBackTarget_)*0.3f;
   posOffset+=lookBackEye_;
  }
  const Vector3f desired=targetPos+posOffset;
  if(!cannonLock_){
   Vector3f horizDelta=desired-s.pos;
   const float vertDelta=horizDelta.y;
   horizDelta.y=0.0f;
   // Charging a stand-still mini-turbo doubles the horizontal rate.
   s.pos+=(horizInterpRate*(status.onBit(St::ChargingSSMT)?2.0f:1.0f))*horizDelta;
   if((status.onBit(St::TouchingGround)&&kart.speed()>30.0f&&pitch>15.0f)||status.onBit(St::ZipperBoost)||over)
    vertInterpRate=fastVertInterpRate;
   s.pos.y+=vertDelta*vertInterpRate;
   // An explosion holds the eye low and lets it follow at the decaying rate.
   if(explosion_){s.explosionY+=explosionRate_*(s.pos.y-s.explosionY);s.pos.y=s.explosionY;}
  }else{
   // Rainbow Road cannon: the eye eases behind the camera target.
   const Vector3f want=camTarget+(backward?s.dist:-s.dist)*move->smoothedForward();
   s.pos+=(want-s.pos)*0.2f;
  }
 }else{
  // The backward state in a half-pipe looks down from above the kart.
  s.bigAirHeight=0;
  s.pos+=targetPos-s.targetPos;
  const Vector3f axis=move->smoothedUp().cross(move->smoothedForward());
  K::EGG::Matrix34f m;m.setAxisRotation(-0.5f,axis);
  const Vector3f dir=m.ps_multVector(move->smoothedForward());
  s.lastOffset+=(s.dist*dir-s.lastOffset)*0.05f;
  s.pos+=s.lastOffset;
 }
 Vector3f targetDelta=s.pos-targetPos;
 if(targetDelta.ps_length()!=0.0f){
  s.pos=targetPos+s.dist*targetDelta.ps_normalize();
  if(storeOffset)s.lastOffset=s.pos-targetPos;
 }
 s.targetPos=targetPos;
}

// fn_1_943F0 (PAL 0x805A47A4).
void RaceChaseCamera::calcHalfPipeOrbit(Vector3f& dir,const K::Kart::KartObject& kart){
 const auto& h=halfPipe_;
 const float a=std::clamp((h.elev-20.f)/60.f,0.f,1.f);
 const float b=std::clamp((h.vspeed-30.f)/35.f,0.f,1.f);
 const float t=kart.velocity().y<0?a*b:0.f;
 halfPipeEase_+=0.02f*(t-halfPipeEase_);
 K::EGG::Matrix34f m;
 m.setAxisRotation(h.sign*K::DEG2RAD*((1.f+halfPipeEase_)*h.elev),h.axis);
 dir=m.ps_multVector(dir);
}

// fn_1_94288 (PAL 0x805A463C). AirtimeOver20 also covers retail's OverZipper
// clause: its bit 34 is only set past 20 frames of airtime (0x805958AC).
void RaceChaseCamera::calcAirtimeHeight(State& s,const K::Kart::KartObject& kart){
 constexpr float BIG_AIR_HEIGHT_CAP=300.0f;
 float targetPitch=0.0f;
 float interpRate=0.2f;
 bool bigAir=false;
 const auto& status=kart.status();
 if(status.offBit(St::InAction)&&status.offBit(St::InCannon)&&status.onBit(St::AirtimeOver20)){
  if(status.offBit(St::Bullet)){s.bigAirHeight=std::min(BIG_AIR_HEIGHT_CAP,s.bigAirHeight+10.0f);bigAir=true;}
  const Vector3f& vel=kart.velocity();
  if(vel.y<0.0f){targetPitch=std::min(0.3f,0.005f*-vel.y);interpRate=0.03f;}
 }
 s.bigAirFallPitch+=interpRate*(targetPitch-s.bigAirFallPitch);
 if(!bigAir)s.bigAirHeight=0.0f;
}

// fn_1_959BC (PAL 0x805A5D70): the eye's sphere (80 forward, 140 backward)
// swept from the last eye, against the course KCL.
void RaceChaseCamera::calcCollision(State& s,bool backward,const K::Kart::KartObject& kart,const Vector3f& forward){
 const auto& status=kart.status();
 const float radius=backward?140.f:80.f; // 0x808923F8 / 0x808923E0
 K::Field::CollisionInfo info;K::Field::KCLTypeMask mask=0;
 Vector3f push=Vector3f::zero,wall=Vector3f::zero;
 bool upright=false,floorHit=false,overhang=false;
 s.hit=false;
 if(--s.wallTimer<0)s.wallTimer=0;
 s.ssmtWall=false;
 const bool over=status.onBit(St::OverZipper);
 if(status.onBit(St::InCannon)||status.onBit(St::AfterCannon)||!courseSphere(radius,s.pos,s.prevPos,CameraMask,info,mask)){
  s.prevPos=s.pos;
 }else{
  push=info.tangentOff;
  s.prevPos=s.pos+push;
  if(!(mask&0x400)&&!(mask&0x10000)){
   if(over){
    // Half-pipe walls blend the push in, faster once the wall turns.
    if(mask&KCL_TYPE_WALL){
     const Vector3f& v=s.halfPipeWall;
     if(v.z*v.z+v.x*v.x+v.y*v.y>1.1920929e-07f){
      const Vector3f& n=info.wallNrm;
      const Vector3f c=n.cross(v);
      const Vector3f ac=halfPipe_.axis.cross(c);
      if(ac.z*halfPipe_.dir.z+ac.x*halfPipe_.dir.x+ac.y*halfPipe_.dir.y>0){
       const float f=std::clamp(1.f-(n.z*v.z+n.x*v.x+n.y*v.y),0.f,1.f);
       if(f>0.01f)s.halfPipeBlendOn=true;
       if(s.halfPipeBlendOn){s.halfPipeBlend+=0.03f;if(s.halfPipeBlend>0.8f)s.halfPipeBlend=0.8f;}
      }
     }
     s.halfPipeWall=info.wallNrm;
    }
    s.pos+=(s.prevPos-s.pos)*s.halfPipeBlend;
    goto tail;
   }
   // Chain Chomp Roulette: no wall push for the first 0x159 race frames.
   const bool hold=course_==int(K::Course::Chain_Chomp_Roulette)&&K::System::RaceManager::Instance()->timer()<0x159;
   if((mask&KCL_TYPE_WALL)&&!hold){
    wall=info.wallNrm;
    s.hit=true;
    Vector3f dir=push;
    if(dir.normalise()>0.f){
     upright=wall.y>=-0.3f;
     overhang=!upright;
     if(dir.z*forward.z+dir.x*forward.x+dir.y*forward.y<0.3f){
      if(kart.speed()<=50.f&&status.offBit(St::DriftManual)){
       if(status.onBit(St::ChargingSSMT)&&K::EGG::Mathf::abs(kart.state()->stickX())>0.f){
        // fn_1_9EBB8 / fn_1_9ED3C: is the kart behind the wall's plane?
        const Vector3f p=s.prevPos+wall*-radius;
        const float d=-((wall.x*p.x+wall.y*p.y)+wall.z*p.z);
        const Vector3f& q=kart.pos();
        if(d+(wall.z*q.z+(wall.x*q.x+wall.y*q.y))<=0.f)s.ssmtWall=true;
       }
      }else s.wallTimer=5;
     }
    }
   }
   if(mask&KCL_TYPE_FLOOR)floorHit=true;
  }
 }
tail:
 if(!over){s.halfPipeWall=Vector3f::zero;s.halfPipeBlend=0.1f;s.halfPipeBlendOn=false;}
 if(course_==int(K::Course::GBA_Shy_Guy_Beach)&&s.pos.y<GbaShyGuyBeachFloor){s.prevPos.y=GbaShyGuyBeachFloor;s.pos.y=GbaShyGuyBeachFloor;}
 const bool both=(floorHit||overhang)&&!upright;
 if(floorHit||overhang||(s.pitchTimer<1&&(both||(!s.ssmtHold&&upright&&s.wallTimer==0))))s.pos+=push;
 if(s.pitchTimer>0)s.prevPos=kart.pos();
 if(upright&&s.wallTimer<1){
  // Running along a wall turns the view a little along it.
  const Vector3f c=right_.cross(wall);
  float side=c.z*forward_.z+c.x*forward_.x+c.y*forward_.y<0.f?-1.f:1.f;
  const Vector3f& last=kart.move()->lastDir();
  const float f=K::EGG::Mathf::abs(wall.z*last.z+wall.x*last.x+wall.y*last.y);
  if(backward)side*=-1.f;
  s.wallYawDrift=(s.wallYawDrift+f*(0.06f*side))*(0.6f+0.3f*f);
 }else s.wallYawDrift*=0.97f;
}

// fn_1_94A80 (PAL 0x805A4E34): probes from the kart toward the eye; a wall in
// between swings the view away from it (+0x80), a floor behind pitches it (+0x84).
void RaceChaseCamera::calcWallAvoid(State& s,bool backward,const K::Kart::KartObject& kart){
 if(course_==int(K::Course::SNES_Battle_Course_4))return;
 const auto& status=kart.status();
 const bool enabled=!(status.onBit(St::OverZipper)||status.onBit(St::InCannon))&&status.offBit(St::DriftManual);
 const Vector3f fwd=backward?-forward_:forward_;
 const Vector3f& kartPos=kart.pos();
 const auto [proj,rej]=(s.pos-kartPos).projAndRej(right_);
 const float d=-K::EGG::Mathf::sqrt(rej.z*rej.z+rej.x*rej.x+rej.y*rej.y);
 Vector3f probe=d*fwd;probe=probe+kartPos+proj;
 if(--s.fwdTimer<0)s.fwdTimer=0;
 if(--s.revTimer<0)s.revTimer=0;
 if(--s.pitchTimer<0)s.pitchTimer=0;
 if(s.fwdTimer<1||(s.revTimer<1&&!s.ssmtHold))s.wallYaw+=0.05f*(0.f-s.wallYaw);
 else s.wallYaw+=0.1f*(s.wallYawTarget-s.wallYaw);
 if(s.pitchTimer<1)s.wallPitch+=0.1f*(0.f-s.wallPitch);
 else s.wallPitch+=0.1f*(s.wallPitchTarget-s.wallPitch);
 Vector3f dir=probe-kartPos;
 Vector3f back=-dir;
 dir.normalise();
 float dist=0;Vector3f at=Vector3f::zero,atFwd=Vector3f::zero,atRev=Vector3f::zero;
 bool hitFwd=false,floorHit=false;
 if(enabled){
  const int tries=s.fwdTimer>0?3:1;
  for(int i=0;i<tries;++i)
   if(probeStep(dist,at,s.probeFwd,kart,i,s.fwdTimer,probe,dir,back,false)){
    atFwd=at;
    if(wallCheck(s,kart,nullptr,at,dir,probe,backward,false)){s.probeFwd=dist;hitFwd=true;s.fwdTimer=3;break;}
   }
  dir=-dir;back=-back;
  const int revTries=s.revTimer>0?3:1;
  for(int i=0;i<revTries;++i)
   if(probeStep(dist,at,s.probeRev,kart,i,s.revTimer,probe,dir,back,true)){
    atRev=at;
    if(wallCheck(s,kart,&floorHit,at,dir,probe,backward,true)){s.probeRev=dist;s.revTimer=3;break;}
   }
 }
 const bool ssmt=status.onBit(St::ChargingSSMT);
 if(s.ssmtHold&&!ssmt)s.ssmtHold=false;
 if(ssmt&&s.ssmtWall&&s.fwdTimer>=1)s.ssmtHold=true;
 else if(!s.ssmtHold&&!(s.fwdTimer>0&&s.revTimer>0))s.wallYawTarget=0;
 bool pitchUp=false;
 if(status.offBit(St::OverZipper)&&kart.state()->airtime()>3&&floorHit&&hitFwd&&K::EGG::Mathf::abs(kart.speed())<20.f){
  const Vector3f a=atFwd-s.pos,b=atRev-s.pos;
  if(K::EGG::Mathf::abs(dir.z*b.z+dir.x*b.x+dir.y*b.y)<K::EGG::Mathf::abs(dir.z*a.z+dir.x*a.x+dir.y*a.y))pitchUp=true;
 }
 if(pitchUp)s.pitchTimer=15;
 else if(s.pitchTimer<1)s.wallPitchTarget=0;
}

// fn_1_95194 (PAL 0x805A5548): the next probe point, stepping 78 (0x808923D8)
// a frame from the kart (or, reversed, from the eye) and wrapping at the far end.
bool RaceChaseCamera::probeStep(float& outDist,Vector3f& outPos,float& slot,const K::Kart::KartObject& kart,int i,int timer,
                                const Vector3f& probe,const Vector3f& dir,const Vector3f& back,bool rev){
 if(timer<1){slot+=78.f;outDist=slot;}
 else{
  outDist=slot;
  if(i!=0)outDist=i==1?78.f:-78.f;
  if(outDist<0.f)return false;
 }
 const Vector3f& kartPos=kart.pos();
 const Vector3f v=rev?(probe+outDist*dir)-kartPos:(kartPos+outDist*dir)-probe;
 if(v.z*back.z+v.x*back.x+v.y*back.y<0.f){
  if(timer>0)return false;
  slot=0;outDist=0;
 }
 outPos=rev?probe+outDist*dir:kartPos+outDist*dir;
 return true;
}

// fn_1_95418 (PAL 0x805A57CC): a radius-80 sphere at the probe point.
bool RaceChaseCamera::wallCheck(State& s,const K::Kart::KartObject& kart,bool* floorHit,const Vector3f& at,const Vector3f& dir,
                                const Vector3f& probe,bool backward,bool rev){
 constexpr float HALF_PI=1.5707963705062866f; // 0x808923F0
 K::Field::CollisionInfo info;K::Field::KCLTypeMask mask=0;
 bool hit=false;
 // The swing away from a wall normal `n` met by `dir` (`d` = n.dir), signed by its side.
 auto swing=[&](const Vector3f& n,float d){
  const Vector3f c=n.cross(dir);
  float a=K::EGG::Mathf::atan2(K::EGG::Mathf::sqrt(c.z*c.z+c.x*c.x+c.y*c.y),d);
  if(a<=0.f)a=-a;
  s.wallYawTarget=a-HALF_PI;
  const Vector3f side=dir.cross(n);
  if(side.z*right_.z+side.x*right_.x+side.y*right_.y<0.f)s.wallYawTarget=-(a-HALF_PI);
 };
 if(courseSphere(80.f,at,rev?probe:kart.pos(),rev?CameraMask:ForwardProbeMask,info,mask)){
  if(mask&KCL_TYPE_WALL){
   const Vector3f& n=info.wallNrm;
   const float d=n.z*dir.z+n.x*dir.x+n.y*dir.y;
   if(d<0.f&&n.y>-1.45f){ // 0x808923E8
    if(!rev)swing(n,d);
    hit=true;
   }
  }
  if(rev&&(mask&KCL_TYPE_FLOOR)){
   const Vector3f d=kart.pos()-at;
   if(K::EGG::Mathf::sqrt(d.z*d.z+d.x*d.x+d.y*d.y)>100.f){
    const Vector3f f=backward?forward_:-forward_;
    const Vector3f c=f.cross(Vector3f::ey);
    float a=K::EGG::Mathf::atan2(K::EGG::Mathf::sqrt(c.z*c.z+c.x*c.x+c.y*c.y),f.y);
    if(a<=0.f)a=-a;
    s.wallPitchTarget=a;
    if(floorHit)*floorHit=true;
   }
  }
 }
 if(!hit&&!rev){
  // The soft-wall pass: weak walls only, their no-bounce normal.
  SoftWall soft;
  if(courseSphere(80.f,at,probe,WeakWallMask,info,mask,&soft)&&soft.dist>-1.1754944e-38f){ // 0x808923F4
   const Vector3f& n=soft.fnrm;
   swing(n,n.z*dir.z+(n.x*dir.x+n.y*dir.y));hit=true;
  }
 }
 return hit;
}
}
