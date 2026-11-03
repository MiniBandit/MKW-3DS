#include <mkw/race_chase_camera.hpp>
#include <game/kart/KartObject.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartParam.hh>
#include <egg/math/Matrix.hh>
#include <algorithm>

namespace mkw {
namespace K=Kinoko;
using K::EGG::Vector3f;

// KartCamera::initPos (Kinoko render/KartCamera.cc, PAL 0x805A49BC).
void RaceChaseCamera::init(const K::Kart::KartObject& kart){
 const auto& params=kart.param()->camera();
 camPosY_=params.posY;
 driftYaw_=0;
 dist_=0;bigAirFallPitch_=0;
 dist_=params.dist;
 calcForward(1.0f,kart);
 calcCamera(1.0f,1.0f,1.0f,kart,kart.pos());
}

// KartCamera::calc without the backward state and the collision query.
void RaceChaseCamera::calc(const K::Kart::KartObject& kart){
 constexpr float HOP_POS_INTERP_RATE=0.8f;
 constexpr float FORWARD_INTERP_RATE=0.35f;
 constexpr float HORIZ_POS_INTERP_RATE=0.05f;
 constexpr float VERT_POS_INTERP_RATE=0.03f;
 constexpr float FAST_VERT_POS_INTERP_RATE=0.1f;
 Vector3f targetPos=kart.pos();
 hopPosY_+=HOP_POS_INTERP_RATE*(kart.move()->hopPosY()-hopPosY_);
 targetPos.y-=hopPosY_;
 const bool bullet=kart.status().onBit(K::Kart::eStatus::Bullet);
 // Killer already steers its body smoothly. Keep the camera behind that
 // turn instead of adding the ordinary kart's second orbit lag.
 calcForward(bullet?1.f:FORWARD_INTERP_RATE,kart);
 if(bullet)driftYaw_=0;else calcDriftOffset(kart);
 calcCamera(bullet?1.f:HORIZ_POS_INTERP_RATE,VERT_POS_INTERP_RATE,FAST_VERT_POS_INTERP_RATE,kart,targetPos);
}

void RaceChaseCamera::calcForward(float t,const K::Kart::KartObject& kart){
 forward_=forward_+(kart.move()->smoothedForward()-forward_)*t;
 right_=Vector3f::ey.perpInPlane(forward_,true);
}

void RaceChaseCamera::calcDriftOffset(const K::Kart::KartObject& kart){
 constexpr float MAX_DRIFT_YAW_AUTOMATIC=10.0f;
 constexpr float MAX_DRIFT_YAW_MANUAL=15.0f;
 constexpr float YAW_STEP_AUTOMATIC=0.5f;
 constexpr float YAW_STEP_MANUAL=1.0f;
 const auto& status=kart.status();
 if(status.onBit(K::Kart::eStatus::DriftManual,K::Kart::eStatus::Hop)){
  float yaw=static_cast<float>(kart.hopStickX())*-0.5f;
  if(kart.vehicleType()==K::Kart::KartParam::Stats::DriftType::Inside_Drift_Bike)yaw*=-1.0f;
  driftYaw_+=yaw;
  float yawMax=status.onBit(K::Kart::eStatus::AutoDrift)?MAX_DRIFT_YAW_AUTOMATIC:MAX_DRIFT_YAW_MANUAL;
  float pitch=90.0f-K::RAD2DEG*K::EGG::Mathf::acos(forward_.dot(Vector3f::ey));
  if(pitch<0.0f)yawMax+=0.1f*K::EGG::Mathf::abs(pitch);
  driftYaw_=std::clamp(driftYaw_,-yawMax,yawMax);
 }else if(driftYaw_!=0.0f){
  float yawStep=status.onBit(K::Kart::eStatus::AutoDrift)?YAW_STEP_AUTOMATIC:YAW_STEP_MANUAL;
  if(driftYaw_>0.0f)driftYaw_=std::max(0.0f,driftYaw_-yawStep);
  else driftYaw_=std::min(0.0f,driftYaw_+yawStep);
 }
}

void RaceChaseCamera::calcCamera(float horizInterpRate,float vertInterpRate,float fastVertInterpRate,
                                 const K::Kart::KartObject& kart,const Vector3f& targetPos){
 constexpr float BIG_AIR_PITCH_CAP=89.0f*K::DEG2RAD;
 Vector3f forwardDir=forward_;
 Vector3f pitchAxis=right_.cross(forwardDir);
 pitchAxis.normalise();
 float driftYaw=K::DEG2RAD*driftYaw_;
 const auto& status=kart.status();
 if(status.onBit(K::Kart::eStatus::AutoDrift))driftYaw*=-1.0f;
 float bigAirPitch=std::min(BIG_AIR_PITCH_CAP,bigAirFallPitch_);
 K::EGG::Matrix34f bigAirPitchMat;
 K::EGG::Matrix34f yawRotMat;
 yawRotMat.setAxisRotation(driftYaw,right_);
 bigAirPitchMat.setAxisRotation(bigAirPitch,pitchAxis);
 Vector3f orbitDir=yawRotMat.ps_multVector(bigAirPitchMat.ps_multVector(forwardDir));
 float pitch=90.0f-K::RAD2DEG*K::EGG::Mathf::acos(std::clamp(orbitDir.dot(Vector3f::ey),-1.0f,1.0f));
 if(pitch>0.0f){
  K::EGG::Matrix34f downPitchMat;
  float fVar16=status.onBit(K::Kart::eStatus::TouchingGround)?0.1f:0.6f;
  pitchRate_=pitchRate_+0.2f*(fVar16-pitchRate_);
  downPitchMat.setAxisRotation(K::DEG2RAD*pitch*pitchRate_,pitchAxis);
  orbitDir=downPitchMat.ps_multVector(orbitDir);
 }
 orbitDir*=-1.0f;
 calcAirtimeHeight(kart);
 pos_+=targetPos-targetPos_;
 Vector3f posOffset=dist_*orbitDir;
 posOffset.y+=camPosY_-kart.cameraDistY()+bigAirHeight_;
 Vector3f horizDelta=targetPos+posOffset-pos_;
 float vertDelta=horizDelta.y;
 horizDelta.y=0.0f;
 pos_+=horizInterpRate*horizDelta;
 if(status.onBit(K::Kart::eStatus::TouchingGround)&&kart.speed()>30.0f&&pitch>15.0f)vertInterpRate=fastVertInterpRate;
 pos_.y+=vertDelta*vertInterpRate;
 Vector3f targetDelta=pos_-targetPos;
 if(targetDelta.ps_length()!=0.0f)pos_=targetPos+dist_*targetDelta.ps_normalize();
 targetPos_=targetPos;
}

void RaceChaseCamera::calcAirtimeHeight(const K::Kart::KartObject& kart){
 constexpr float BIG_AIR_HEIGHT_CAP=300.0f;
 float targetPitch=0.0f;
 float interpRate=0.2f;
 const auto& status=kart.status();
 if(status.onBit(K::Kart::eStatus::AirtimeOver20)){
  bigAirHeight_=std::min(BIG_AIR_HEIGHT_CAP,bigAirHeight_+10.0f);
  const Vector3f& vel=kart.velocity();
  if(vel.y<0.0f)targetPitch=std::min(0.3f,0.005f*-vel.y);
  interpRate=0.03f;
 }else bigAirHeight_=0.0f;
 bigAirFallPitch_+=interpRate*(targetPitch-bigAirFallPitch_);
}
}
