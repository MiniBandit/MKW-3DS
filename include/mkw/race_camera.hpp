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
// RaceChaseCamera::update fills the chase fields between the director's phases.
struct RaceCameraInput {
 // Retail RaceCamera forward state (+0x19c): eye and target position (kart
 // position less the smoothed hop), from the presentation chase camera.
 Vec3 chaseEye,chasePivot;
 Vec3 chaseBackEye;            // backward state (+0x268) eye: the look-behind view
 Vec3 lookBackTarget;          // RaceCamera +0xc4: the look-behind target offset
 Vec3 kartPos,kartForward;     // KartMove smoothed forward (+0x80)
 // boostFov: the boost FOV add (RaceCamera +0x120), eased into the FOV here.
 float cameraDistY=0,targetPosY=0,cameraFov=60,boostFov=0;
 unsigned stage=0;             // Kinoko RaceManager::Stage
 int course=-1;                // RaceConfig course id (+0xb68)
 bool previewActive=false;     // KMP course preview owns the screen
 bool beforeRespawn=false,inRespawn=false,finished=false;
 bool lookBack=false;          // flag 0x20 (0x805A66D8)
 // While held: the eye's x/z still follow (forward state +0x84 > 0.5 or its wall-hit byte).
 bool holdFollow=false;
 bool explosion=false;         // flag 1 (0x805A4DEC): the target drops 10 a frame to -50
 // OverZipper, velocity y < 0 and RaceCamera +0x118 for the half-pipe target (0x805A2CFC).
 bool halfPipe=false,falling=false;float halfPipeEase=0;
 // 0x805A40D0 in the chase view: the up vector's target and rate.
 Vec3 upTarget{0,1,0};float upRate=.1f;
 uint16_t oobRequest=0;        // KartCollide::oobCameraRequest
 // Read-only course-KCL sphere push (RaceChaseCamera::spherePush): true on a
 // hit, with the push-out offset and whether a wall was among the hits.
 bool (*spherePush)(float radius,Vec3 pos,Vec3 prev,Vec3& push,bool& wall)=nullptr;
 // Camera AREA lookup (KMP::Manager::FindAREA, type 0): current id or -1.
 int (*findArea)(void* context,int current,Vec3 position)=nullptr;void* areaContext=nullptr;
};

// up / side: the camera's up (RaceCamera +0x7c) and side axes (0x805A40D0);
// the shake moves the target along them.
struct RaceCameraView {Vec3 eye,target;float fov=60;Vec3 up{0,1,0},side{1,0,0};};

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
 // RaceCamera::Update in three phases around the chase states (RaceChaseCamera::update):
 // respawn / out-of-bounds requests and the target (false while the preview runs),
 // the held eye, then the shown view, FOV, KMP and start-pan cameras and up vector.
 bool beginStep(const RaceCameraInput& in);
 void holdEye(const RaceCameraInput& in);
 void endStep(const RaceCameraInput& in);
 void step(const RaceCameraInput& in){if(beginStep(in)){holdEye(in);endStep(in);}}
 Vec3 target()const{return target_;}  // RaceCamera +0xac
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
 bool goalHeld()const{return auto_.suppress;}
 static constexpr unsigned PanFrames=170;     // OpeningPan::Calc 0xaa
 static constexpr unsigned WipeFrames=180;    // wipe table 0x808b1c68[0]
 static constexpr unsigned WipeStart=40;      // respawn timer 130 reaches 90
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
 void computeTarget(const RaceCameraInput& in,bool snap,Vec3 pivot);
 void resetUp();
 void calcUp(const RaceCameraInput& in,bool chase,float rate);
 void clampGbaShyGuyBeach(const RaceCameraInput& in);
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
 float halfPipeDrop_=0,halfPipeLift_=0;    // +0x10c, +0x110
 float riseEye_=0,riseTarget_=0;           // +0x124, +0x128
 float fov_=60;                            // +0x11c
 // Up vector smoothing (+0x150 smoothed up target, +0x15c last up, +0x168
 // eased up, +0x174 rate, +0x178 quaternion as w, x, y, z).
 Vec3 smUp_{0,1,0},prevUp_{0,1,0},upSm_{0,1,0};float upT_=1;std::array<float,4> upQ_{1,0,0,0};
 uint16_t flags_=0;
 bool wasBeforeRespawn_=false,wasInRespawn_=false;
 int oobFrames_=-1,wipe_=-1;
 // OpeningPan (+0x198).
 std::array<Vec3,16> panPoints_{};std::array<float,16> panSpeeds_{};unsigned panCount_=0;
 float panTension_=.2f,panFov_=55;uint16_t panFlags_=0;unsigned panFrame_=0;bool panStarted_=false;
 CameraRail panRail_;
 Auto auto_;
};
}
