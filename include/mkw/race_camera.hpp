#pragma once
#include <mkw/math.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mkw {
// Presentation copy of the retail route classes used by race cameras:
// RouteCurved / RouteStraight and their controllers (Kinoko Field::RailSpline,
// RailLine, RailSmoothInterpolator and RailLinearInterpolator, PAL
// 0x806ED57C..0x806F0880). Kinoko's interpolators only bind to its KMP rails;
// the start pan builds a rail from points of its own, rebuilt every frame.
class CameraRail {
public:
 enum class Status {InProgress=0,SegmentEnd=1,ChangingDirection=2};
 // smooth: RouteCurved (cubic spline, `tension` = Rail::m_someScale; KMP
 // routes 0.1, the start pan 0.2). oscillating: an open path (POTI setting 1).
 bool build(const Vec3* points,unsigned count,bool smooth,bool oscillating,float tension);
 // Route::UpdatePoints + RouteCurved::Update: new point positions, same count.
 void movePoints(const Vec3* points,unsigned count);
 // Controller init(t, idx) (PAL 0x806EE924 / 0x806EFEAC); velocity becomes `speed`.
 void init(float t,unsigned index,float speed=0);
 void setVelocity(float velocity);
 Status calc();
 void setStopAtEnd(bool stop){stopAtEnd_=stop;}
 bool valid()const{return count_>=2;}
 Vec3 position()const{return pos_;}
 unsigned current()const{return unsigned(cur_);}
 unsigned next()const{return unsigned(next_);}
 float segmentT()const{return t_;}
 unsigned count()const{return count_;}
private:
 struct Transition {Vec3 p0,p1,p2,p3,dir;float length=0,lengthInv=0;};
 void invalidate();
 Vec3 bezier(float t,const Transition& tr)const;
 float estimate(const Transition& tr);
 float calcT(float t)const;
 bool shouldChangeDirection()const;
 void changeDirection();
 void nextIndices();
 std::vector<Vec3> points_;
 std::vector<Transition> transitions_;
 std::vector<float> percent_;
 unsigned count_=0,segments_=0;
 bool smooth_=false,oscillating_=false,stopAtEnd_=false,parked_=false,forward_=true;
 float tension_=.1f,speed_=0,vel_=0,segmentVel_=0,t_=0;
 int cur_=0,next_=1;
 Vec3 pos_,direction_;
};

// KMP camera data (CAME, AREA cameras, POTI routes) for the race director.
struct RaceCameraCourse {
 struct Camera {
  uint8_t type=0,next=0xff,route=0xff;uint16_t pathSpeed=0,fovSpeed=0,viewSpeed=0;
  Vec3 position,viewStart,viewEnd;float fovStart=60,fovEnd=60,time=0;
 };
 struct Route {std::vector<Vec3> points;std::vector<uint16_t> speeds;bool smooth=false,oscillating=false;};
 std::vector<Camera> cameras;
 std::vector<int> areaCamera;   // AREA index -> CAME index (camera areas, else -1)
 std::vector<Route> routes;
 int goalCamera=-1;             // CourseMap +0x44: first CAME of type 0
 bool load(const void* kmp,std::size_t size);
};

// race_camera.bcp / start_camera.bcp in Common.szs (BCPInfo::Load, PAL 0x805A6E40).
struct StartPanParam {
 unsigned count=0;float tension=.2f;
 Vec3 scale{1,1,1},rotation,translation;
 std::array<Vec3,16> points{};std::array<float,16> speeds{};
 bool load(const void* data,std::size_t size);
 void defaults(); // OpeningPan::Load without a BCP (PAL 0x805A6F44)
};

// One 60 Hz tick of race-camera inputs, sampled after the simulation step.
struct RaceCameraInput {
 // Retail RaceCamera forward state (+0x19c): eye and target position (kart
 // position less the smoothed hop), from the presentation chase camera.
 Vec3 chaseEye,chasePivot;
 Vec3 rearEye,rearPivot;
 bool lookBack=false,bullet=false,inCannon=false;
 Vec3 kartPos,kartForward;     // KartMove smoothed forward (+0x80)
 float cameraDistY=0,targetPosY=0,cameraFov=60,boostFov=0;
 float cameraPosY=0,cameraDistance=0; // original character camera parameters
 unsigned stage=0;             // Kinoko RaceManager::Stage
 bool previewActive=false;     // KMP course preview owns the screen
 bool beforeRespawn=false,inRespawn=false,finished=false;
 unsigned battleWipeSerial=0; // KartMove zero-balloon type0 wipe request
 int respawnTimer=0;          // KartCollide countdown after its physics tick
 uint16_t oobRequest=0;        // KartCollide::oobCameraRequest
 // Camera AREA lookup (KMP::Manager::FindAREA, type 0): current id or -1.
 int (*findArea)(void* context,int current,Vec3 position)=nullptr;void* areaContext=nullptr;
 // Isolated presentation query: retail auto-camera sphere and wall classification.
 bool (*collideEye)(void* context,float radius,Vec3 eye,Vec3 previous,
                    Vec3& push,bool& wall)=nullptr;
 void* collisionContext=nullptr;
};

struct RaceCameraView {Vec3 eye,target;float fov=60;};

// Presentation race-camera director. Chooses the eye, target and FOV each tick
// from retail RaceCamera::Update (PAL 0x805A21D0): the start pan (OpeningPan,
// 0x805A70A0/0x805A7358), the out-of-bounds holds requested by
// Kart::Link::SetCamera / StopCameraMovAndRotation, the respawn reset
// (UpdateCameraOnRespawn), and after the goal the KMP camera mover
// (AutoCameraMoverRace, 0x805AAF64..0x805ACA2C). Never writes simulation state.
class RaceCameraDirector {
public:
 enum Flags:uint16_t {FlagAuto=0x4,FlagHold=0x8,FlagRise=0x10,FlagOobHit=0x400};
 enum class Mode {Preview,StartPan,Chase,Auto};
 void setCourse(const RaceCameraCourse* course){course_=course;}
 // RaceCamera::Init with the BCP start pan (0x805A2034); the player's start
 // position/angles (degrees) as RaceManager::findKartStartPoint returns them.
 void reset(const StartPanParam& pan,Vec3 startPos,Vec3 startAngles,const RaceCameraInput& first);
 void step(const RaceCameraInput& in);
 RaceCameraView view()const{return view_;}
 Mode mode()const;
 uint16_t flags()const{return flags_;}
 bool startPanActive()const{return (panFlags_&2)!=0;}
 bool startPanFinished()const{return (panFlags_&8)!=0;}
 unsigned startPanFrame()const{return panFrame_;}
 // Retail OOB wipe (Kart::Link::StartOobWipe, game_image/wipe.brlyt): -1 when
 // idle, else the progress 0..1 of its 180-frame type 0 run.
 float wipeProgress()const{return wipe_<0?-1.f:std::min(1.f,float(wipe_)/float(WipeFrames));}
 int autoCamera()const{return auto_.camera;}
 int autoArea()const{return auto_.area;}
 // Demo CAME chain supplies shot IDs directly; use the same original
 // action handlers without goal/AREA selection or the race opening pan.
 bool selectDemoShot(unsigned shot,const RaceCameraInput&);
 void stepDemoShot(const RaceCameraInput&);
 bool goalHeld()const{return auto_.suppress;}
 static constexpr unsigned PanFrames=170;     // OpeningPan::Calc 0xaa
 static constexpr unsigned WipeFrames=180;    // wipe table 0x808b1c68[0]
 static constexpr unsigned GoalHoldFrames=300;// 0x808b21dc
private:
 struct Auto {
  int camera=-1,area=-1,type=-1;
  float elapsed=0,fov=60,dist=0,distTarget=0,pitch=0;
  Vec3 forward{0,0,1},prevEye,viewOffset,savedTarget;float viewDistance=0;
  unsigned hold=0,fixFrames=0;
  bool done=false,suppress=false;
  CameraRail rail;
 };
 void computeTarget(const RaceCameraInput& in,bool snap);
 void stepPan(const RaceCameraInput& in);
 void stepAuto(const RaceCameraInput& in);
 void switchCamera(const RaceCameraInput& in,int camera,bool raceEnd);
 void startAction(const RaceCameraInput& in);
 void updateAction(const RaceCameraInput& in);
 Vec3 autoTarget(const RaceCameraInput& in)const;
 void relativeEye(const RaceCameraInput& in,Vec3 viewStart,Vec3 viewEnd,Vec3 offset);
 void viewMove();
 const RaceCameraCourse* course_=nullptr;
 RaceCameraView view_;
 Vec3 eye_,target_;           // RaceCamera +0xa0 / +0xac
 float targetHeight_=100,targetOffset_=0,slopeOffset_=0; // +0x344, +0x108, +0x104
 float forwardPitch_=0,forwardPitchVelocity_=0;bool forwardPitchReady_=false;
 float forwardYaw_=0,forwardYawVelocity_=0,forwardYawExitOffset_=0;bool bulletYawActive_=false,bulletYawExiting_=false;
 float riseEye_=0,riseTarget_=0;           // +0x124, +0x128
 uint16_t flags_=0;
 bool wasBeforeRespawn_=false,wasInRespawn_=false;
 int wipe_=-1;
 unsigned battleWipeSerial_=0;
 // OpeningPan (+0x198).
 std::array<Vec3,16> panPoints_{};std::array<float,16> panSpeeds_{};unsigned panCount_=0;
 float panTension_=.2f,panFov_=55;uint16_t panFlags_=0;unsigned panFrame_=0;bool panStarted_=false;
 CameraRail panRail_;
 Auto auto_;
};
}
