#pragma once
#include <mkw/award_results.hpp>
#include <mkw/award_camera.hpp>
#include <mkw/award_physics.hpp>
#include <mkw/ceremony_vehicle.hpp>
#include <mkw/award_drive_rails.hpp>
#include <mkw/kart_surface_effects.hpp>
#include <mkw/bike_scrape_effects.hpp>
namespace mkw {
// Scene-owned vehicle presentation; no pointers into the finished race heap.
class AwardDrive {
public:
 bool load(const AwardResults&);
 bool prepareMii(const NativeMii&);
 bool bindPhysics(AwardPhysicsScene&);
 void step();
 void pose();
 void draw(Renderer&)const;
 void drawEffects(Renderer&,const float* camera);
 AwardCameraSample camera()const;
 bool finished()const{return done_;}
 const char* detail()const{return failure_.c_str();}
private:
 struct Actor {
  std::unique_ptr<CeremonyVehicle> model;
  VehicleAnimation animation;
  VehicleAnimation::Snapshot snapshot;
  DriverController driverController;DriverPose driverPose;
  unsigned vehicle=0,place=0;
  Vec3 position{};float cameraDistY=0;
  KartSurfaceEffects surface;
  BikeScrapeEffects scrape;
  bool visible=false;

 };
 void shot(unsigned);
 std::vector<Actor> actors_;
 std::unique_ptr<RaceEffects> effects_;
 std::vector<Texture> effectTextures_;
 EffectLayerBuffer effectBuffer_;
 int driftSmoke_=-1;
 AwardPhysicsScene* physics_=nullptr;
 void samplePhysics(bool advanceDriver);
 AwardCameraRails cameraPaths_;
 unsigned shot_=0,shotFrame_=0,tick_=0;
 Vec3 celebrationOffset_{};
 bool done_=false;
 std::string failure_;
};
}
