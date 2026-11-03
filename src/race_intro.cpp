#include <mkw/race_intro.hpp>
#include <game/field/RailInterpolator.hh>
#include <game/field/RailManager.hh>
#include <game/system/CourseMap.hh>
#include <cmath>
#include <cstring>
#include <utility>

namespace mkw {
namespace K=Kinoko;
namespace {
std::uint16_t be16(const std::uint8_t* p){return std::uint16_t(unsigned(p[0])<<8|p[1]);}
std::uint32_t be32(const std::uint8_t* p){return std::uint32_t(p[0])<<24|std::uint32_t(p[1])<<16|std::uint32_t(p[2])<<8|p[3];}
float bef(const std::uint8_t* p){const std::uint32_t v=be32(p);float f;std::memcpy(&f,&v,4);return f;}
Vec3 bev(const std::uint8_t* p){return {bef(p),bef(p+4),bef(p+8)};}
// 0x8088DD44: CAME speeds are hundredths of a unit (or degree) per frame.
constexpr float SpeedScale=.01f;
// 0x8088DD74: the path camera's rail starts at this velocity.
constexpr float InitialRailVelocity=10.f;
constexpr std::size_t EntrySize=0x48;
}

RaceIntro::RaceIntro()=default;
RaceIntro::~RaceIntro()=default;

bool RaceIntro::load(const void* data,std::size_t size){
 stop();cameras_.clear();opening_=-1;
 const auto* k=static_cast<const std::uint8_t*>(data);
 if(!k||size<0x10||std::memcmp(k,"RKMD",4))return false;
 const unsigned sections=be16(k+8),header=be16(k+0xa);
 if(0x10+std::size_t(sections)*4>size)return false;
 for(unsigned i=0;i<sections;++i){
  const std::size_t at=std::size_t(header)+be32(k+0x10+i*4);
  if(at+8>size||std::memcmp(k+at,"CAME",4))continue;
  const unsigned count=be16(k+at+4);
  if(at+8+count*EntrySize>size)return false;
  for(unsigned c=0;c<count;++c){
   const auto* e=k+at+8+c*EntrySize;Camera cam;
   cam.type=e[0];cam.next=e[1];cam.route=e[3];
   cam.pathSpeed=be16(e+4);cam.fovSpeed=be16(e+6);cam.viewSpeed=be16(e+8);
   cam.position=bev(e+0xc);cam.fovStart=bef(e+0x24);cam.fovEnd=bef(e+0x28);
   cam.viewStart=bev(e+0x2c);cam.viewEnd=bev(e+0x38);cam.time=bef(e+0x44);
   cameras_.push_back(cam);
  }
  // CourseMap caches getCamera(MapdataCameraAccessor::getExtraValue()) as
  // the opening camera: the section header's first extra byte.
  const unsigned first=k[at+6];
  if(first<cameras_.size())opening_=int(first);
  break;
 }
 return opening_>=0;
}

unsigned RaceIntro::frames()const{
 unsigned total=0,guard=0;
 for(int i=opening_;i>=0&&std::size_t(i)<cameras_.size()&&guard<cameras_.size();++guard){
  total+=unsigned(std::ceil(cameras_[i].time));
  const unsigned next=cameras_[i].next;i=next<cameras_.size()?int(next):-1;
 }
 return total;
}

void RaceIntro::start(){
 stop();frame_=0;
 if(opening_<0)return;
 active_=true;shots_=0;begin(opening_);
 // Nothing is shown before the first tick positions the camera; start from
 // the authored placement so an early frame is never undefined.
 const auto& c=cameras_[current_];
 eye_=rail_?Vec3{rail_->curPos().x,rail_->curPos().y,rail_->curPos().z}:c.position;
 target_=c.viewStart;appliedFov_=c.fovStart;
}

std::vector<IntroPathFrame> RaceIntro::previewFrames()const{
 RaceIntro probe;probe.cameras_=cameras_;probe.opening_=opening_;probe.start();
 std::vector<IntroPathFrame> result;if(!probe.active())return result;
 result.push_back({probe.eye(),probe.target(),probe.fov(),probe.shot()});
 while(probe.active()&&result.size()<4096){const int shot=probe.shot();probe.step();result.push_back({probe.eye(),probe.target(),probe.fov(),shot});}
 return result;
}

void RaceIntro::stop(){rail_.reset();active_=false;current_=-1;}

// Director camera switch (PAL 0x805AD924) and the shot's state init.
void RaceIntro::begin(int index){
 current_=index;elapsed_=0;
 const auto& c=cameras_[index];
 fov_=c.fovStart;
 // 0x805ADA28 / 0x805ADB40: the view point restarts at viewStart.
 const Vec3 delta=c.viewEnd-c.viewStart;
 viewDistance_=std::sqrt(delta.z*delta.z+delta.x*delta.x+delta.y*delta.y);
 viewOffset_={};
 rail_.reset();
 if(c.type!=5)return;
 // 0x805ADB40: the route's interpolator (created with speed 0 by RailManager),
 // restarted at its first point, velocity 10, stopping at the path's end.
 auto* map=K::System::CourseMap::Instance();auto* rails=K::Field::RailManager::Instance();
 if(!map||!rails||c.route>=map->getPointInfoCount()||rails->rail(c.route)->pointCount()<2)return;
 if(map->getPointInfo(c.route)->setting(0))rail_.reset(new K::Field::RailSmoothInterpolator(0.f,c.route));
 else rail_.reset(new K::Field::RailLinearInterpolator(0.f,c.route));
 rail_->init(0.f,0);rail_->setCurrVel(InitialRailVelocity);rail_->setStopAtEnd(true);
}

void RaceIntro::step(){
 if(!active_)return;
 ++frame_;
 const auto& c=cameras_[current_];
 // Shot position. Type 5 (0x805ADCB0): velocity is the route points' first
 // setting, lerped by segment progress, then the rail advances. Type 4
 // (0x805ADAE4) holds its authored position.
 if(rail_){
  const auto points=K::Field::RailManager::Instance()->rail(c.route)->points();
  const auto speed=[&](int i){return i>=0&&std::size_t(i)<points.size()?float(points[i].setting[0]):0.f;};
  const float from=speed(rail_->curPointIdx()),to=speed(rail_->nextPointIdx());
  rail_->setCurrVel(from+(to-from)*rail_->segmentT());
  rail_->calc();
  const auto& p=rail_->curPos();eye_={p.x,p.y,p.z};
 }else eye_=c.position;
 // FOV (0x805A9D50): steps toward fovEnd, clamped to the authored range.
 if(c.fovSpeed){
  float low=c.fovStart,high=c.fovEnd,sign=1.f;
  if(high<low){sign=-1.f;std::swap(low,high);}
  const float next=fov_+SpeedScale*sign*float(c.fovSpeed);
  fov_=next<low?low:next>high?high:next;
 }else fov_=c.fovStart;
 appliedFov_=fov_;
 // View point (0x805AA9FC): moves from viewStart toward viewEnd, stopping there.
 if(c.viewSpeed){
  if(0.f<viewDistance_){
   const Vec3 delta=c.viewEnd-c.viewStart;Vec3 dir=delta;
   const float len=std::sqrt(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);
   if(len>0.f)dir=dir*(1.f/len);
   const float step=SpeedScale*float(c.viewSpeed);
   viewOffset_={viewOffset_.x+step*dir.x,viewOffset_.y+step*dir.y,viewOffset_.z+step*dir.z};
   const float moved=std::sqrt(viewOffset_.z*viewOffset_.z+viewOffset_.x*viewOffset_.x+viewOffset_.y*viewOffset_.y);
   if(viewDistance_<moved)viewOffset_=delta;
  }
  target_=c.viewStart+viewOffset_;
 }
 // Director (0x805AD5C8): each shot lasts `time` frames, then `next` starts;
 // the last shot ends the preview. A malformed loop still ends.
 elapsed_+=1.f;
 if(c.time<=elapsed_){
  if(c.next<cameras_.size()&&++shots_<64)begin(c.next);
  else stop();
 }
}
}
