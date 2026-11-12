#include <mkw/award_drive.hpp>
#include <mkw/award_drive_data.hpp>
#include <cmath>
#include <cstring>
#include <game/kart/KartObject.hh>
#include <game/kart/KartState.hh>
#include <game/kart/KartMove.hh>
#include <game/system/KPadController.hh>
#include <game/kart/KartParam.hh>
namespace mkw {
bool AwardDrive::prepareMii(const NativeMii& identity){for(auto& a:actors_)if(!a.model->prepareMii(identity))return false;return true;}

namespace {
Vec3 vec(const Kinoko::EGG::Vector3f& p){return {p.x,p.y,p.z};}
Vec3 heading(const Kinoko::Kart::KartObject& kart){const auto& p=kart.pose();return normalized(Vec3{p[0,2],0,p[2,2]},{0,0,1});}
}
bool AwardDrive::load(const AwardResults& results){
 actors_.clear();
 effects_=std::make_unique<RaceEffects>();
 if(!effects_->load("textures/award-driving-effects.bin")){failure_="ceremony driving smoke bank";return false;}
 driftSmoke_=effects_->find("rk_driftSmoke");if(driftSmoke_<0)return false;
 effectTextures_.resize(effects_->textures.size());
 for(unsigned i=0;i<effectTextures_.size();++i){
  const auto path=std::string("textures/")+effects_->textures[i].name+".t3x";
  if(!effectTextures_[i].load(path.c_str())){failure_=path;return false;}
 }
 // Retail winningrun_demo supplies one lane per podium place.
 for(unsigned i=0;i<std::min(results.count,3u);++i){
  const auto& p=results.players[i];Actor a;a.vehicle=p.vehicle;a.place=i;
  a.model=std::make_unique<CeremonyVehicle>();
  if(!a.model->load(p.character,p.vehicle,results.teams?p.team:RaceTeam::None,&failure_))return false;
  if(!a.scrape.bind(*effects_)){failure_="ceremony bike scrape bank";return false;}
  a.driverController.reset();
  for(const char* name:DriverController::names)a.driverController.setDuration(name,a.model->clipDuration(name));
  actors_.push_back(std::move(a));
 }
 if(actors_.empty()){failure_="ceremony driving cast";return false;}
 cameraPaths_.prepare();shot(0);done_=false;tick_=0;physics_=nullptr;return true;
}
void AwardDrive::shot(unsigned index){
 shot_=index;shotFrame_=0;
 cameraPaths_.select(~0u);
 const auto& c=AwardDriveShots[index];
 // OnAction4Start/Update uses CAME.position and its view-point track;
 // its route field does not start a rail (same rule as RaceCameraDirector).
 if((c.type==5||c.type==2)&&c.route<std::size(AwardDriveRails))cameraPaths_.select(c.route);
 celebrationOffset_={};
 if(c.type==2&&!actors_.empty()){
  const auto* p=cameraPaths_.position();
  const auto aim=actors_.front().position+Vec3{0,100-actors_.front().cameraDistY,0};
  const auto world=(p?vec(*p):c.position)-aim;
  // Preserve HW12's radial distance, but remove its 3/4 side angle.
  const float distance=length(world);
  celebrationOffset_={0,0,distance};
 }
}
void AwardDrive::step(){
 if(done_)return;
 ++tick_;
 if(!physics_||!physics_->step()){failure_="ceremony physics unavailable";done_=true;return;}
 samplePhysics(true);
 cameraPaths_.step();
 if(effects_)effects_->step();
 if(++shotFrame_>=AwardDriveShots[shot_].duration){
  const auto next=AwardDriveShots[shot_].next;
  if(next<std::size(AwardDriveShots))shot(next);else done_=true;
 }
}
bool AwardDrive::bindPhysics(AwardPhysicsScene& physics){
 if(!physics.active()||physics.count()!=actors_.size())return false;
 if(!effectBuffer_.init())return false;
 physics_=&physics;samplePhysics(false);return true;
}
void AwardDrive::samplePhysics(bool advanceDriver){
 using namespace Kinoko;
 for(auto& a:actors_){
  auto* kart=physics_->kart(a.place);if(!kart){a.visible=false;continue;}
  const auto& status=kart->status();
  a.position=vec(kart->pos());a.cameraDistY=kart->cameraDistY();a.visible=physics_->visible(a.place);
  a.animation.step(*kart);a.snapshot=a.animation.snapshot();
  if(!a.visible){
   for(unsigned w=0;w<2;++w)effects_->kill(a.place*2+w);
   for(unsigned n=0;n<BikeScrapeEffects::Slots;++n)effects_->kill(8+a.place*4+n);
   for(unsigned n=0;n<KartSurfaceEffects::Slots;++n)effects_->kill(KartSurfaceEffects::FirstSlot+a.place*KartSurfaceEffects::Slots+n);
   continue;
  }
  if(!advanceDriver)continue;
  DriverSignals s;s.vehicleId=a.vehicle;s.bike=kart->isBike();
  s.handleRelativeBike=kart->param()->stats().body==Kart::KartParam::Stats::Body::Handle_Relative_Bike;
  s.steering=kart->inputs()->currentState().stick.x;s.speed=kart->move()->speed();
  s.drift=status.onBit(Kart::eStatus::DriftManual,Kart::eStatus::DriftAuto);
  s.driftDirection=kart->move()->hopStickX();
  s.airborne=status.offBit(Kart::eStatus::TouchingGround);s.hopping=status.onBit(Kart::eStatus::Hop);
  s.wheelie=status.onBit(Kart::eStatus::Wheelie);s.boost=status.onBit(Kart::eStatus::Boost);
  s.accelerating=status.onBit(Kart::eStatus::Accelerate);s.braking=status.onBit(Kart::eStatus::Brake);
  s.ceremonyVictory=physics_->celebrating(a.place);s.finishType=a.place==0?0:1;
  a.driverPose=a.driverController.step(s);
  // Same KartEffect wheel anchors and grounded drift-smoke gate as racing.
  const auto& m=kart->pose();EffectFrame f;f.origin=a.position;f.scale=kart->scale().y;
  f.x={m[0,0],m[1,0],m[2,0]};f.y={m[0,1],m[1,1],m[2,1]};f.z={m[0,2],m[1,2],m[2,2]};
  const unsigned l=kart->isBike()?1:kart->tireCount()-2,r=kart->tireCount()-1;
  KartSurfaceEffects::Wheel wheels[2];
  for(unsigned w=0;w<2;++w){const auto tire=w?r:l;const auto& c=kart->collisionData(tire);
   wheels[w]={vec(kart->wheelEdgePos(tire)),uint32_t(c.closestFloorFlags),c.closestFloorSettings,c.bFloor};
   auto wheel=f;wheel.origin=vec(kart->wheelPos(tire));
   // Outside-drift bikes use the original charging smoke root;
   // inside-drift bikes use hangOnTail below. Keep the accepted kart gate.
   const bool outsideBike=s.bike&&kart->param()->stats().driftType==Kart::KartParam::Stats::DriftType::Outside_Drift_Bike;
   effects_->attach(a.place*2+w,driftSmoke_,wheel,s.drift&&!s.airborne&&
       (!s.bike||(outsideBike&&w==0&&unsigned(kart->move()->driftState())==1)));
  }
  auto rear=f;rear.origin=vec(kart->wheelPos(r));const auto& contact=kart->collisionData(r);
  rear.y=normalized(contact.bFloor?vec(contact.floorNrm):Vec3{0,1,0},{0,1,0});
  rear.z=normalized(tangent(f.z,rear.y),{0,0,1});rear.x=normalized(cross(rear.y,rear.z),{1,0,0});
  auto body=rear;body.origin={a.position.x,rear.origin.y,a.position.z};
  a.scrape.step(*effects_,body,rear,unsigned(kart->move()->driftState()),kart->move()->hopStickX(),
      status.onBit(Kart::eStatus::DriftAuto),contact.bFloor,
      s.bike&&kart->param()->stats().driftType==Kart::KartParam::Stats::DriftType::Inside_Drift_Bike,8+a.place*4);
  a.surface.step(*effects_,f,wheels,unsigned(Kinoko::Course::Win_Demo),s.bike,a.visible,unsigned(kart->move()->driftState())>=2,
      length(vec(kart->velocity())),kart->move()->softSpeedLimit(),false,false,false,a.place);

 }
}
void AwardDrive::pose(){
 for(auto& a:actors_)if(a.visible)a.model->pose(a.snapshot,a.driverPose,tick_);
}
AwardCameraSample AwardDrive::camera()const{
 const auto& c=AwardDriveShots[shot_];AwardCameraSample out{};
 out.cameraType=1;out.projectionType=0;out.near=10;out.far=300000;out.aspect=5.f/3;
 const auto* cameraPosition=cameraPaths_.position();out.position=cameraPosition?vec(*cameraPosition):c.position;
 const float delta=c.fovEnd-c.fovStart;
 out.fovy=c.fovStart+std::copysign(std::min(std::abs(delta),c.fovSpeed*.01f*shotFrame_),delta);
 const Vec3 d=c.viewEnd-c.viewStart;const float size=length(d);
 out.aim=c.viewStart+(size>0?d*(std::min(size,c.viewSpeed*.01f*shotFrame_)/size):Vec3{});
 // CAME type 2 tracks the leading kart while moving along its camera rail.
 if(c.type==2&&!actors_.empty()){
  float faceHeight=100-actors_.front().cameraDistY;
  // The last rendered face_1 joint supplies the actor's actual head height;
  // keep its kart-relative offset while this simulation step moves forward.
  actors_.front().model->faceHeight(faceHeight);
  out.aim=actors_.front().position+Vec3{0,faceHeight,0};
  // Presentation correction for the port's independently simulated demo drive:
  // keep the entry framing while cheering instead of combining a closing rail
  // with the 50 -> 20 degree zoom. The opening/drift shots retain retail tracks.
  // Dad's reference calls for a fixed distance, with only a small final push.
  constexpr float closeFrames=60.f,closeAmount=.05f;
  const float t=std::clamp((float(shotFrame_)-(c.duration-closeFrames))/closeFrames,0.f,1.f);
  const float ease=t*t*(3.f-2.f*t);
  const auto* kart=physics_?physics_->kart(0):nullptr;
  const auto forward=kart?heading(*kart):Vec3{0,0,1},right=cross(Vec3{0,1,0},forward);
  // Face the vehicle head-on, at its existing face/upper-body target height.
  // Dad requested another 30% closer than HW12 (whose factor was 0.9).
  // Dad accepted HW13's distance/front direction; add only a gentle 8-degree
  // downward view, retaining the same radial distance from the face target.
  constexpr float pitch=8.f*3.14159265358979323846f/180.f;
  const auto offset=right*celebrationOffset_.x+
      (forward*std::cos(pitch)+Vec3{0,std::sin(pitch),0})*celebrationOffset_.z;
  out.position=out.aim+offset*(.9f*.7f*(1.f-closeAmount*ease));
  out.fovy=c.fovStart;
 }
 return out;
}
void AwardDrive::drawEffects(Renderer& renderer,const float* camera){
 if(!effects_)return;
 float view[3][4];std::memcpy(view,camera,sizeof view);effects_->setCamera(view);
 renderer.drawSceneEffects(*effects_,effectTextures_.data(),effectBuffer_);
}
void AwardDrive::draw(Renderer& renderer)const{
 std::array<Renderer::ShadowVolume,3> shadows{};
 for(unsigned i=0;i<actors_.size();++i)if(actors_[i].visible)shadows[i]=actors_[i].model->shadow();
 renderer.drawShadowVolumes(shadows.data(),actors_.size(),.5f);
 for(const auto& a:actors_)if(a.visible)a.model->draw(renderer);
}
}
