#include <mkw/race_camera.hpp>
#include <egg/math/Quat.hh>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mkw {
namespace {
// 0x8088dc40: degrees to radians as the race camera multiplies them.
constexpr float DegToRad=0.0174532924f;
uint16_t be16(const uint8_t* p){return uint16_t(unsigned(p[0])<<8|p[1]);}
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
float bef(const uint8_t* p){const uint32_t v=be32(p);float f;std::memcpy(&f,&v,4);return f;}
Vec3 bev(const uint8_t* p){return {bef(p),bef(p+4),bef(p+8)};}
// EGG::Vector3f::perpInPlane(rhs, true): (rhs x v) x rhs, normalized.
Vec3 perpInPlane(Vec3 v,Vec3 n){
 if(std::fabs(dot(v,n))==1.f)return {};
 const Vec3 r{(n.z*v.x-n.x*v.z)*n.z-(n.x*v.y-n.y*v.x)*n.y,(n.x*v.y-n.y*v.x)*n.x-(n.y*v.z-n.z*v.y)*n.z,(n.y*v.z-n.z*v.y)*n.y-(n.z*v.x-n.x*v.z)*n.x};
 const float l=length(r);return l!=0.f?r*(1.f/l):r;
}
// Row-major 3x3 rotation, EGG::Matrix34f::setAxisRotation (quaternion form).
struct Mat3 {
 float m[3][3]{{1,0,0},{0,1,0},{0,0,1}};
 static Mat3 axis(float angle,Vec3 a){
  const float s=std::sin(angle*.5f),c=std::cos(angle*.5f);const float x=a.x*s,y=a.y*s,z=a.z*s,w=c;
  Mat3 r;const float xx=2*x*x,yy=2*y*y,zz=2*z*z,xy=2*x*y,xz=2*x*z,yz=2*y*z,wx=2*w*x,wy=2*w*y,wz=2*w*z;
  r.m[0][0]=1-yy-zz;r.m[0][1]=xy-wz;r.m[0][2]=xz+wy;
  r.m[1][0]=xy+wz;r.m[1][1]=1-xx-zz;r.m[1][2]=yz-wx;
  r.m[2][0]=xz-wy;r.m[2][1]=yz+wx;r.m[2][2]=1-xx-yy;return r;
 }
 Mat3 operator*(const Mat3& b)const{Mat3 r;for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[i][j]=m[i][0]*b.m[0][j]+m[i][1]*b.m[1][j]+m[i][2]*b.m[2][j];return r;}
 Vec3 operator*(Vec3 v)const{return {m[0][0]*v.x+m[0][1]*v.y+m[0][2]*v.z,m[1][0]*v.x+m[1][1]*v.y+m[1][2]*v.z,m[2][0]*v.x+m[2][1]*v.y+m[2][2]*v.z};}
};
// EGG::Matrix34f::makeRT / makeSRT (Euler X, Y, Z in radians).
struct Mat34 {
 Mat3 r;Vec3 t;
 static Mat34 srt(Vec3 s,Vec3 a,Vec3 t){
  const float sx=std::sin(a.x),sy=std::sin(a.y),sz=std::sin(a.z),cx=std::cos(a.x),cy=std::cos(a.y),cz=std::cos(a.z);
  Mat34 o;auto& m=o.r.m;
  m[0][0]=cy*cz;m[1][0]=cy*sz;m[2][0]=-sy;
  m[0][1]=sx*sy*cz-cx*sz;m[1][1]=sx*sy*sz+cx*cz;m[2][1]=sx*cy;
  m[0][2]=cx*cz*sy+sx*sz;m[1][2]=cx*sz*sy-sx*cz;m[2][2]=cx*cy;
  for(int i=0;i<3;++i){m[i][0]*=s.x;m[i][1]*=s.y;m[i][2]*=s.z;}
  o.t=t;return o;
 }
 Mat34 operator*(const Mat34& b)const{Mat34 o;o.r=r*b.r;o.t=r*b.t+t;return o;}
 Vec3 operator*(Vec3 v)const{return r*v+t;}
};
}

// ---- CameraRail -------------------------------------------------------------

bool CameraRail::build(const Vec3* points,unsigned count,bool smooth,bool oscillating,float tension){
 points_.assign(points,points+count);count_=count;smooth_=smooth;oscillating_=oscillating;tension_=tension;
 if(count_<2||(smooth_&&count_<3)){count_=0;return false;}
 invalidate();init(0,0,0);return true;
}

void CameraRail::movePoints(const Vec3* points,unsigned count){
 if(count!=count_||!count_)return;
 std::copy(points,points+count,points_.begin());
 if(smooth_)invalidate();
}

// RailSpline::invalidateTransitions (PAL 0x806EDA04) and RailLine's directions.
void CameraRail::invalidate(){
 const unsigned n=oscillating_?count_-1:count_;
 transitions_.assign(n,{});
 if(!smooth_){
  for(unsigned i=0;i+1<count_;++i){auto& t=transitions_[i];t.dir=points_[i+1]-points_[i];t.length=length(t.dir);t.lengthInv=1.f/t.length;}
  if(!oscillating_){auto& t=transitions_.back();t.dir=points_[0]-points_[count_-1];t.length=length(t.dir);t.lengthInv=1.f/t.length;}
  return;
 }
 percent_.assign(10*n+1,0.f);segments_=0;
 auto p1=[&](Vec3 a,Vec3 b,Vec3 c){Vec3 r=c-a;const float l=length(r);return b+normalized(r,{})*(l*tension_);};
 auto p2=[&](Vec3 a,Vec3 b,Vec3 c){Vec3 r=a-c;const float l=length(r);return b+normalized(r,{})*(l*tension_);};
 auto controls=[&](Transition& t,Vec3 a,Vec3 b,Vec3 c,Vec3 d){t.p0=b;t.p1=p1(a,b,c);t.p2=p2(b,c,d);t.p3=c;t.length=estimate(t);t.lengthInv=1.f/t.length;};
 const auto& p=points_;
 if(oscillating_){
  auto& f=transitions_[0];f.p0=p[0];f.p1=(p[1]-p[0])*.25f+p[0];f.p2=count_>2?p2(p[0],p[1],p[2]):(p[0]-p[1])*.25f+p[1];f.p3=p[1];
  f.length=estimate(f);f.lengthInv=1.f/f.length;
  for(unsigned i=1;i+1<n;++i)controls(transitions_[i],p[i-1],p[i],p[i+1],p[i+2]);
  if(n>1){auto& l=transitions_.back();l.p0=p[n-1];l.p1=p1(p[n-2],p[n-1],p[n]);l.p2=(p[n-1]-p[n])*.25f+p[n];l.p3=p[n];
   l.length=estimate(l);l.lengthInv=1.f/l.length;}
 }else{
  auto& f=transitions_[0];f.p0=p[0];f.p1=p1(p[n-1],p[0],p[1]);f.p2=p2(p[0],p[1],p[2%count_]);f.p3=p[1];
  f.length=estimate(f);f.lengthInv=1.f/f.length;
  for(unsigned i=1;i+1<n;++i)controls(transitions_[i],p[i-1],p[i],p[i+1],i+2!=n?p[i+2]:p[0]);
  auto& l=transitions_.back();l.p0=p[n-1];l.p1=p1(p[n-2],p[n-1],p[0]);l.p2=p2(p[n-1],p[0],p[1]);l.p3=p[0];
  l.length=estimate(l);l.lengthInv=1.f/l.length;
 }
}

Vec3 CameraRail::bezier(float t,const Transition& tr)const{
 const float d=1.f-t;
 return tr.p0*(d*d*d)+tr.p1*(3.f*t*(d*d))+tr.p2*(3.f*(t*t)*d)+tr.p3*(t*t*t);
}

// RailSpline::estimateLength (PAL 0x806EE56C): ten chords, cumulative fractions.
float CameraRail::estimate(const Transition& tr){
 std::array<Vec3,11> w;for(int i=0;i<11;++i)w[i]=bezier(.1f*float(i),tr);
 float len=0;
 for(int i=0;i<10;++i){percent_[segments_++]=len;len+=length(w[i]-w[i+1]);}
 for(int i=int(segments_)-1;i>int(segments_)-11;--i)percent_[i]/=len;
 return len;
}

// RailSmoothInterpolator::calcT (PAL 0x806EF0F8): arc-length reparameterization.
float CameraRail::calcT(float t)const{
 const unsigned base=unsigned(forward_?cur_:next_)*10u;
 float delta=0;unsigned idx=0;
 for(unsigned i=0;i<9;++i){const float a=percent_[base+i],b=percent_[base+i+1];if(a<=t&&b>t){delta=(t-a)/(b-a);idx=i;}}
 const float last=percent_[base+9];
 if(last<=t&&1.f>=t){idx=9;delta=(t-last)/(1.f-last);}
 return .1f*float(idx)+.1f*delta;
}

void CameraRail::init(float t,unsigned index,float speed){
 if(!count_)return;
 speed_=speed;t_=t;cur_=int(index);parked_=false;
 if(smooth_){
  if(int(index)==int(count_)-1&&oscillating_){next_=cur_-1;forward_=false;segmentVel_=vel_*transitions_[next_].lengthInv;}
  else{next_=index+1==count_?0:int(index)+1;forward_=true;segmentVel_=vel_*transitions_[cur_].lengthInv;}
  pos_=points_[cur_];
 }else{
  const bool last=int(index)==int(count_)-1;next_=last?cur_-1:cur_+1;forward_=last?!oscillating_:true;
  pos_=points_[cur_];direction_=points_[next_]-pos_;segmentVel_=speed_/length(direction_);
 }
 vel_=speed_;
}

void CameraRail::setVelocity(float velocity){
 vel_=velocity;
 if(!count_)return;
 if(smooth_)segmentVel_=velocity*transitions_[forward_?cur_:next_].lengthInv;
 else segmentVel_=vel_/length(direction_);
}

bool CameraRail::shouldChangeDirection()const{
 if(!oscillating_)return int(count_)==next_;
 return forward_?next_==int(count_):next_==-1;
}
void CameraRail::changeDirection(){if(!oscillating_)return;next_+=forward_?-2:2;forward_=!forward_;}
void CameraRail::nextIndices(){
 if(forward_){++cur_;++next_;}else{--cur_;--next_;}
 if(!oscillating_){if(next_==int(count_))next_=0;if(cur_==int(count_))cur_=0;}
}

CameraRail::Status CameraRail::calc(){
 if(!count_)return Status::ChangingDirection;
 if(smooth_){
  if(parked_){pos_=transitions_[count_-2].p3;return Status::ChangingDirection;}
  const float t=forward_?calcT(t_):calcT(1.f-t_);
  pos_=bezier(t,transitions_[forward_?cur_:next_]);
  t_+=segmentVel_;
  if(t_<=1.f)return Status::InProgress;
  Status status=Status::SegmentEnd;
  // RailSmoothInterpolator::calcNextSegment (PAL 0x806EF664).
  const float nextT=t_-1.f;
  if(oscillating_){
   if(next_==0||next_==int(count_)-1)t_=0;
   else if(forward_)t_=nextT*transitions_[cur_].length*transitions_[next_].lengthInv;
   else t_=nextT*transitions_[cur_-1].length*transitions_[next_-1].lengthInv;
  }else t_=nextT*transitions_[cur_].length*transitions_[next_].lengthInv;
  if(t_>1.f)t_=.99f;
  nextIndices();
  if(shouldChangeDirection()){
   status=Status::ChangingDirection;
   if(!stopAtEnd_)changeDirection();
   else{parked_=true;next_=int(count_)-1;cur_=int(count_)-2;}
  }
  segmentVel_=vel_*transitions_[forward_?cur_:next_].lengthInv;
  return status;
 }
 if(parked_){pos_=points_[count_-1];return Status::ChangingDirection;}
 t_+=segmentVel_;
 pos_=points_[cur_]*(1.f-t_)+points_[next_]*t_;
 if(t_<=1.f)return Status::InProgress;
 Status status=Status::SegmentEnd;
 // RailLinearInterpolator::calcNextSegment (PAL 0x806F0610).
 nextIndices();
 if(shouldChangeDirection())t_=0;
 else{
  const float prev=length(direction_);direction_=points_[next_]-points_[cur_];
  t_=((t_-1.f)*prev)/length(direction_);if(t_>1.f)t_=.99f;
 }
 if(shouldChangeDirection()){
  status=Status::ChangingDirection;
  if(!stopAtEnd_)changeDirection();
  else{parked_=true;next_=int(count_)-1;cur_=int(count_)-2;}
 }
 direction_=points_[next_]-points_[cur_];segmentVel_=vel_/length(direction_);
 return status;
}

// ---- KMP / BCP --------------------------------------------------------------

bool RaceCameraCourse::load(const void* data,std::size_t size){
 cameras.clear();areaCamera.clear();routes.clear();goalCamera=-1;
 const auto* k=static_cast<const uint8_t*>(data);
 if(!k||size<0x10||std::memcmp(k,"RKMD",4))return false;
 const unsigned sections=be16(k+8),header=be16(k+0xa);
 if(0x10+std::size_t(sections)*4>size)return false;
 for(unsigned i=0;i<sections;++i){
  const std::size_t at=std::size_t(header)+be32(k+0x10+i*4);
  if(at+8>size)continue;
  const unsigned count=be16(k+at+4);
  if(!std::memcmp(k+at,"CAME",4)){
   if(at+8+count*0x48>size)return false;
   for(unsigned c=0;c<count;++c){
    const auto* e=k+at+8+c*0x48;Camera cam;
    cam.type=e[0];cam.next=e[1];cam.route=e[3];cam.pathSpeed=be16(e+4);cam.fovSpeed=be16(e+6);cam.viewSpeed=be16(e+8);
    cam.position=bev(e+0xc);cam.fovStart=bef(e+0x24);cam.fovEnd=bef(e+0x28);cam.viewStart=bev(e+0x2c);cam.viewEnd=bev(e+0x38);cam.time=bef(e+0x44);
    // KMP::Manager::Init (USA 0x8050E378): the goal camera is the first of type 0.
    if(cam.type==0&&goalCamera<0)goalCamera=int(cameras.size());
    cameras.push_back(cam);
   }
  }else if(!std::memcmp(k+at,"AREA",4)){
   if(at+8+count*0x30>size)return false;
   for(unsigned a=0;a<count;++a){const auto* e=k+at+8+a*0x30;areaCamera.push_back(e[1]==0&&e[2]!=0xff?int(e[2]):-1);}
  }else if(!std::memcmp(k+at,"POTI",4)){
   std::size_t p=at+8;
   for(unsigned r=0;r<count;++r){
    if(p+4>size)return false;
    const unsigned points=be16(k+p);Route route;route.smooth=k[p+2]==1;route.oscillating=k[p+3]==1;p+=4;
    if(p+points*0x10>size)return false;
    for(unsigned j=0;j<points;++j){route.points.push_back(bev(k+p));route.speeds.push_back(be16(k+p+0xc));p+=0x10;}
    routes.push_back(std::move(route));
   }
  }
 }
 for(auto& a:areaCamera)if(a>=int(cameras.size()))a=-1;
 return true;
}

bool StartPanParam::load(const void* data,std::size_t size){
 const auto* b=static_cast<const uint8_t*>(data);
 if(!b||size<0xf0+16*4)return false;
 count=be16(b+4);if(count<2||count>16)return false;
 tension=bef(b+8);scale=bev(b+0xc);rotation=bev(b+0x18);translation=bev(b+0x24);
 for(unsigned i=0;i<16;++i){points[i]=bev(b+0x30+i*12);speeds[i]=bef(b+0xf0+i*4);}
 return std::isfinite(tension);
}

void StartPanParam::defaults(){
 count=16;tension=.2f;scale={1,1,1};rotation={};translation={};
 float z=2000;for(unsigned i=0;i<16;++i){z-=200;points[i]={0,200,z};speeds[i]=20;}
}

// ---- Director ---------------------------------------------------------------

RaceCameraDirector::Mode RaceCameraDirector::mode()const{
 if(flags_&FlagAuto)return Mode::Auto;
 if((panFlags_&2)&&panStarted_)return Mode::StartPan;
 return Mode::Chase;
}

void RaceCameraDirector::reset(const StartPanParam& pan,Vec3 startPos,Vec3 startAngles,const RaceCameraInput& first){
 flags_=0;riseEye_=riseTarget_=0;wasBeforeRespawn_=wasInRespawn_=false;oobFrames_=wipe_=-1;
 auto_={};
 // RaceCamera::Init: OpeningPan::Init sets the target height to 100, OnRespawn
 // evaluates the target once (easing it), then Init sets 100 again.
 halfPipeDrop_=halfPipeLift_=slopeOffset_=0;
 targetHeight_=100;computeTarget(first,true,first.kartPos);targetHeight_=100;
 eye_=first.chaseEye;
 fov_=first.cameraFov;resetUp();
 // OpeningPan::Init (PAL 0x805A70A0): start frame (makeRT) x BCP SRT, last
 // point at the camera; stop-at-end rail at t=0; FOV 55.
 panCount_=std::min(pan.count,16u);panTension_=pan.tension;
 const Mat34 frame=Mat34::srt({1,1,1},startAngles*DegToRad,startPos)*Mat34::srt(pan.scale,pan.rotation*DegToRad,pan.translation);
 for(unsigned i=0;i<panCount_;++i){panPoints_[i]=frame*pan.points[i];panSpeeds_[i]=pan.speeds[i];}
 if(panCount_)panPoints_[panCount_-1]=eye_;
 panFlags_=0;panFrame_=0;panFov_=55;panStarted_=false;
 if(panCount_>=3&&panRail_.build(panPoints_.data(),panCount_,true,true,panTension_)){panRail_.setStopAtEnd(true);panFlags_=3;}
 view_={eye_,target_,first.cameraFov};
}

// fn_1_92948 (PAL 0x805A2CFC): target = pivot + (0, offset, 0). The offset
// eases (0.2) toward the intro-eased target height (0.05 per frame from 100
// before the countdown) less the vertical camera distance, plus 130 x the
// slope of the smoothed forward eased at 0.03, plus the half-pipe drop (+0x10c,
// to -100 at 0.03). The half-pipe lift (+0x110, to 100 x +0x118 while falling)
// is added along the up vector (0x805A40D0). During an explosion (flag 1) the
// offset instead drops 10 a frame to -50. OnRespawn evaluates it with rates
// (1, 0, 1). The flag 2 it sets has no reader in the camera module.
void RaceCameraDirector::computeTarget(const RaceCameraInput& in,bool snap,Vec3 pivot){
 if(flags_&FlagRise)return; // StopCameraMovAndRotation holds the target.
 if(in.stage<1)targetHeight_+=.05f*(in.targetPosY-targetHeight_);
 const float base=targetHeight_-in.cameraDistY;
 const float rate=snap?1.f:.03f,ease=snap?1.f:.2f;
 const float slope=snap?0.f:130.f*std::fabs(in.kartForward.y);
 float drop=0,lift=0;
 if(in.halfPipe){drop=-100.f;if(in.falling)lift=100.f*in.halfPipeEase;}
 halfPipeDrop_+=rate*(drop-halfPipeDrop_);
 halfPipeLift_+=rate*(lift-halfPipeLift_);
 if(!in.explosion){
  slopeOffset_+=(slope-slopeOffset_)*rate;
  targetOffset_+=ease*((halfPipeDrop_+(base+slopeOffset_))-targetOffset_);
 }else{
  targetOffset_-=10.f;
  if(targetOffset_<-50.f)targetOffset_=-50.f;
 }
 target_=pivot+Vec3{0,targetOffset_,0};
}

void RaceCameraDirector::resetUp(){smUp_=prevUp_=upSm_={0,1,0};upT_=1;upQ_={1,0,0,0};}

// fn_1_93D1C (PAL 0x805A40D0): the up vector. In the chase view a smoothed
// target (half-pipe normals, else world up) tilts the side axis; the result is
// eased while it points up (0.02 more each frame up to 1) and slerped from the
// last up otherwise. The chase target is then lifted along it (+0x110).
void RaceCameraDirector::calcUp(const RaceCameraInput& in,bool chase,float rate){
 using V=Kinoko::EGG::Vector3f;
 using Q=Kinoko::EGG::Quatf;
 auto e=[](Vec3 v){return V(v.x,v.y,v.z);};
 auto m=[](const V& v){return Vec3{v.x,v.y,v.z};};
 V dir=e(view_.target-view_.eye);dir.normalise();
 V side;
 if(chase){
  const V t=e(in.upTarget);V s=e(smUp_);
  s.x=s.x+(t.x-s.x)*rate;s.y=s.y+(t.y-s.y)*rate;s.z=s.z+(t.z-s.z)*rate;
  smUp_=m(s);side=s.cross(dir);
 }else side=V::ey.cross(dir);
 // The KMP camera's roll (AutoCameraMover +0x14) is never set by the race movers.
 side.normalise();
 V up=dir.cross(side);
 Q q(upQ_[0],upQ_[1],upQ_[2],upQ_[3]);
 const Q ident(1.f,0.f,0.f,0.f);
 const V prev=e(prevUp_);
 Q rot;rot.makeVectorRotation(prev,up);
 V eased=e(upSm_);
 if(up.y>=.15f){
  q=q.slerpTo(ident,upT_);
  eased.x+=(up.x-eased.x)*upT_;eased.y+=(up.y-eased.y)*upT_;eased.z+=(up.z-eased.z)*upT_;
  upT_=std::min(1.f,upT_+.02f);
 }else{
  q=ident.slerpTo(rot,rate);
  eased=q.rotateVector(prev);
  upT_=rate;
 }
 if(chase)up=eased;
 up.normalise();
 upSm_=m(eased);prevUp_=m(up);upQ_={q.w,q.v.x,q.v.y,q.v.z};
 if(chase)view_.target=view_.target+m(up)*halfPipeLift_;
 view_.up=m(up);view_.side=m(side);
}

// GBA Shy Guy Beach (course 0x1f): the eye stays above the water (0x80892354).
void RaceCameraDirector::clampGbaShyGuyBeach(const RaceCameraInput& in){
 if(in.course==31&&view_.eye.y<3050.f)view_.eye.y=3050.f;
}

bool RaceCameraDirector::beginStep(const RaceCameraInput& in){
 if(in.previewActive){wasBeforeRespawn_=in.beforeRespawn;wasInRespawn_=in.inRespawn;return false;}
 // UpdateCameraOnRespawn (PAL 0x80590E28): RaceCamera::OnRespawn re-seats the
 // camera behind the kart and clears every request.
 if(in.inRespawn&&!wasInRespawn_){
  flags_=0;riseEye_=riseTarget_=0;oobFrames_=-1;
  halfPipeDrop_=slopeOffset_=0;
  computeTarget(in,true,in.kartPos);eye_=in.chaseEye;
  // OnRespawn resets the up smoothing; its one rate-1 evaluation leaves the
  // same state for the next update (eased rate 1, quaternion identity).
  resetUp();
 }
 // Out of bounds: the request issued with the activation (KartCollide hook).
 if(in.beforeRespawn&&!wasBeforeRespawn_){flags_|=in.oobRequest&(FlagHold|FlagRise|FlagOobHit);oobFrames_=0;}
 else if(in.beforeRespawn&&oobFrames_>=0)++oobFrames_;
 wasBeforeRespawn_=in.beforeRespawn;wasInRespawn_=in.inRespawn;
 // Kart::Collision::UpdateRespawn (PAL 0x80573ED4): the wipe starts when the
 // 130-frame respawn timer reaches 90; Kart::Status advances it (+0x94).
 if(wipe_>=0&&++wipe_>int(WipeFrames))wipe_=-1;
 if(in.beforeRespawn&&oobFrames_==int(WipeStart)&&wipe_<0)wipe_=0;
 // RaceCamera::Update (PAL 0x805A21D0).
 computeTarget(in,false,in.chasePivot);
 return true;
}

// Update after the chase orbit: the eye follows the forward state unless an
// out-of-bounds request holds it (rising for 0x110); while held its x/z still
// follow when the forward state pitches over a floor or pushed off a wall.
void RaceCameraDirector::holdEye(const RaceCameraInput& in){
 if(!(flags_&FlagHold)){eye_=in.chaseEye;return;}
 if(flags_&(FlagRise|0x100)){
  const float e=riseEye_+.04f*(800.f-riseEye_);eye_.y+=e-riseEye_;riseEye_=e;
  const float t=riseTarget_+.04f*(in.targetPosY-riseTarget_);target_.y-=t-riseTarget_;riseTarget_=t;
 }
 if(in.holdFollow){eye_.x=in.chaseEye.x;eye_.z=in.chaseEye.z;}
}

// Update after the chase collision: the shown eye (the backward state's while
// looking back, with its target offset), the FOV, the GBA Shy Guy Beach clamp,
// the KMP camera mover, the start pan and the up vector.
void RaceCameraDirector::endStep(const RaceCameraInput& in){
 view_.eye=in.lookBack?in.chaseBackEye:eye_;
 view_.target=in.lookBack?target_+in.lookBackTarget:target_;
 // The KMP camera mover after the goal (Update: flag 4 needs no OOB hit).
 if(in.finished&&course_&&!(flags_&FlagOobHit))flags_|=FlagAuto;else flags_&=uint16_t(~FlagAuto);
 // The FOV eases toward the entry's FOV plus the boost add: 0.1 while a boost
 // adds, else 0.03 (0x80892308 / 0x80892338).
 if(!(flags_&FlagAuto))fov_+=(in.boostFov>0.f?.1f:.03f)*((in.boostFov+in.cameraFov)-fov_);
 view_.fov=fov_;
 clampGbaShyGuyBeach(in);
 stepPan(in);
 if(flags_&FlagAuto){stepAuto(in);clampGbaShyGuyBeach(in);}
 // Update passes 0.1; only the chase view's targets change it.
 const bool chase=!(flags_&FlagAuto)&&!(panFlags_&2);
 calcUp(in,chase,chase?in.upRate:.1f);
}

// OpeningPan::Calc (PAL 0x805A7358).
void RaceCameraDirector::stepPan(const RaceCameraInput& in){
 panStarted_=true;
 if(panFlags_&1){
  if(!(panFlags_&4)){
   const unsigned a=panRail_.current(),b=panRail_.next();
   const float va=a<panCount_?panSpeeds_[a]:0,vb=b<panCount_?panSpeeds_[b]:0;
   panRail_.setVelocity(va+(vb-va)*panRail_.segmentT());
   if(panRail_.calc()==CameraRail::Status::ChangingDirection)panFlags_|=4;
  }
  if(panFrame_<PanFrames)++panFrame_;else panFlags_=uint16_t((panFlags_&~3u)|8u);
 }
 // Until the race starts the rail ends at the live camera position.
 if(in.stage<2&&panCount_){panPoints_[panCount_-1]=eye_;panRail_.movePoints(panPoints_.data(),panCount_);}
 if(!(flags_&FlagAuto)&&(panFlags_&2)){
  panFov_+=.07f*(in.cameraFov-panFov_);
  view_={panRail_.position(),target_,panFov_};
 }
}

// ---- AutoCameraMoverRace ----------------------------------------------------

Vec3 RaceCameraDirector::autoTarget(const RaceCameraInput& in)const{
 // 0x808b21d8: 100 above the kart, less the vertical camera distance.
 return in.kartPos+Vec3{0,100.f-in.cameraDistY,0};
}

// fn_1_99E78 (PAL 0x805AA22C): a kart-relative eye. Without an offset the eye
// is `dist` along the smoothed forward yawed by -viewStart.x about up and
// pitched by -viewStart.y; with one, along the offset in the forward frame.
// viewEnd then turns the aim by (-x, -y) degrees about the eye.
void RaceCameraDirector::relativeEye(const RaceCameraInput& in,Vec3 viewStart,Vec3 viewEnd,Vec3 offset){
 auto& a=auto_;
 a.forward=a.forward+(in.kartForward-a.forward)*(in.inRespawn?1.f:.1f);
 const Vec3 up=perpInPlane({0,1,0},a.forward),fwd=a.forward;
 Vec3 delta;
 if(dot(offset,offset)<=1.1920929e-07f){
  const Vec3 side=cross(up,fwd);
  const float pitch=std::clamp(-viewStart.y-a.pitch,-89.f,89.f);
  const Mat3 m=Mat3::axis(DegToRad*-viewStart.x,up)*Mat3::axis(DegToRad*pitch,side);
  delta=(m*fwd)*a.dist;
 }else{
  // fn_1_9E400: columns (up x fwd, fwd x side, fwd).
  const Vec3 s=normalized(cross(up,fwd),{1,0,0}),u=normalized(cross(fwd,s),{0,1,0});
  const Vec3 o=s*offset.x+u*offset.y+fwd*offset.z;
  a.distTarget=length(o);delta=normalized(o,{0,0,1})*a.dist;
 }
 Vec3& eye=view_.eye;Vec3& target=view_.target;
 eye=target+delta;
 const Vec3 aim=target-eye,d=normalized(aim,{0,0,1});
 const Vec3 up2=perpInPlane({0,1,0},d),c=cross(up2,d);
 const Mat3 r=Mat3::axis(DegToRad*-viewEnd.x,up2)*Mat3::axis(DegToRad*-viewEnd.y,c);
 target=eye+r*aim;
}

// fn_1_9A648 (PAL 0x805AA9FC): the view point slides from viewStart toward
// viewEnd at viewSpeed/100 per frame.
void RaceCameraDirector::viewMove(){
 auto& a=auto_;const auto& c=course_->cameras[a.camera];
 if(!c.viewSpeed)return;
 if(a.viewDistance>0){
  const Vec3 dir=normalized(c.viewEnd-c.viewStart,{});
  a.viewOffset=a.viewOffset+dir*(.01f*float(c.viewSpeed));
  if(length(a.viewOffset)>a.viewDistance)a.viewOffset=c.viewEnd-c.viewStart;
 }
 view_.target=c.viewStart+a.viewOffset;
}

// fn_1_9B9E0 / fn_1_99BA0 (PAL 0x805ABD94, 0x805A9F54): a camera switch.
void RaceCameraDirector::switchCamera(const RaceCameraInput& in,int camera,bool raceEnd){
 auto& a=auto_;
 const int oldType=a.camera>=0?course_->cameras[a.camera].type:-1;
 a.camera=camera;a.elapsed=0;
 const auto& c=course_->cameras[camera];
 a.fov=c.fovStart;
 if(oldType!=c.type||a.suppress){a.forward=in.kartForward;a.prevEye=view_.eye;a.pitch=0;}
 a.type=raceEnd?3:c.type;
 startAction(in);
}

void RaceCameraDirector::startAction(const RaceCameraInput& in){
 auto& a=auto_;const auto& c=course_->cameras[a.camera];(void)in;
 auto rail=[&]{
  if(c.route<course_->routes.size()){const auto& r=course_->routes[c.route];
   if(a.rail.build(r.points.data(),unsigned(r.points.size()),r.smooth,r.oscillating,.1f)){a.rail.init(0,0,0);a.rail.setVelocity(10);a.rail.setStopAtEnd(true);return;}}
  a.rail=CameraRail{};
 };
 switch(a.type){
 case 1:a.fixFrames=0;a.savedTarget={};break;                  // OnAction1Start
 case 2:rail();break;                                          // OnAction2Start
 case 3:case 9:a.distTarget=a.dist=c.viewStart.z;break;        // OnRaceEndStart
 case 4:a.viewDistance=length(c.viewEnd-c.viewStart);a.viewOffset={};break;
 case 5:a.viewDistance=length(c.viewEnd-c.viewStart);a.viewOffset={};rail();break; // OnAction6Start
 case 6:rail();break;                                          // OnAction5Start
 default:break;
 }
}

void RaceCameraDirector::updateAction(const RaceCameraInput& in){
 auto& a=auto_;const auto& c=course_->cameras[a.camera];
 auto railStep=[&]{
  if(!a.rail.valid())return false;
  const auto& r=course_->routes[c.route];
  const unsigned i=a.rail.current(),j=a.rail.next();
  const float vi=i<r.speeds.size()?float(r.speeds[i]):0,vj=j<r.speeds.size()?float(r.speeds[j]):0;
  a.rail.setVelocity(vi+(vj-vi)*a.rail.segmentT());a.rail.calc();return true;
 };
 switch(a.type){
 case 0:view_.target=autoTarget(in);relativeEye(in,c.viewStart,c.viewEnd,{});break;
 case 1:{ // OnAction1Update: fixed eye; the target stops after pathSpeed frames.
  view_.eye=c.position;bool hold=false;
  if(c.pathSpeed&&++a.fixFrames>=c.pathSpeed){hold=true;a.fixFrames=c.pathSpeed;}
  if(hold)view_.target=a.savedTarget;else{view_.target=autoTarget(in);a.savedTarget=view_.target;}
  break;}
 case 2:if(railStep())view_.eye=a.rail.position();view_.target=autoTarget(in);break;
 case 3:case 9:view_.target=autoTarget(in);relativeEye(in,c.viewStart,c.viewEnd,{});break;
 case 4:viewMove();view_.eye=c.position;break;
 case 5:if(railStep())view_.eye=a.rail.position();viewMove();break;
 case 6:{view_.target=autoTarget(in);const Vec3 o=railStep()?a.rail.position():Vec3{};relativeEye(in,c.viewStart,c.viewEnd,o);break;}
 default:break;
 }
}

// AutoCameraMoverRace::vf_0x10 (PAL 0x805AB57C) with fn_1_9C880 (0x805ACC34).
void RaceCameraDirector::stepAuto(const RaceCameraInput& in){
 auto& a=auto_;
 // The output starts from the race camera (Update wrote it before the mover).
 view_={eye_,target_,view_.fov};
 if(!a.done&&course_->goalCamera>=0){
  a.suppress=a.hold<GoalHoldFrames;
  if(!a.suppress)a.done=true;
  if(a.hold==0)switchCamera(in,course_->goalCamera,true);
  ++a.hold;
 }
 // Camera AREAs: a new area switches unless the goal camera holds.
 if(in.findArea){
  const int area=in.findArea(in.areaContext,a.area,in.kartPos);
  if(area>=0&&area!=a.area){
   a.area=area;
   if(!a.suppress){
    const int camera=area<int(course_->areaCamera.size())?course_->areaCamera[area]:-1;
    if(camera>=0)switchCamera(in,camera,false);else a.camera=-1;
   }
  }
 }
 if(a.camera<0)return;
 updateAction(in);
 // fn_1_9999C: FOV steps from fovStart toward fovEnd at fovSpeed/100.
 const auto& c=course_->cameras[a.camera];
 if(c.fovSpeed){
  float low=c.fovStart,high=c.fovEnd,sign=1;if(high<low){std::swap(low,high);sign=-1;}
  a.fov=std::clamp(a.fov+.01f*sign*float(c.fovSpeed),low,high);
 }else a.fov=c.fovStart;
 view_.fov=a.fov;
 // fn_1_9A954 (PAL 0x805AAD08) for held goal, KartFollow and kart-path cameras:
 // a radius-65 sphere swept from the last eye pushes the eye out of the course
 // KCL (the read-only query of RaceChaseCamera); a wall within 300 raises the
 // pitch offset 0.5 a frame up to 60. The distance then relaxes to its target
 // (0.05 within twice it, else snaps; at least 300) and the pitch decays.
 if(a.suppress||a.type==3||a.type==6){
  Vec3 push;bool wall=false;
  if(in.spherePush&&in.spherePush(65.f,view_.eye,a.prevEye,push,wall)){
   view_.eye=view_.eye+push;
   a.dist=length(view_.eye-view_.target);
   if(wall&&a.dist<300.f){a.pitch+=.5f;if(a.pitch>60.f)a.pitch=60.f;}
  }
  a.pitch+=.03f*(0.f-a.pitch);
  a.dist=a.dist<=2.f*a.distTarget?a.dist+.05f*(a.distTarget-a.dist):a.distTarget;
  if(a.dist<300.f)a.dist=300.f;
  a.prevEye=view_.eye;
 }
}
}
