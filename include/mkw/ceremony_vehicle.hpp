#pragma once
#include <mkw/vehicle_visual_assets.hpp>
#include <mkw/vehicle_animation.hpp>
#include <mkw/driver_controller.hpp>
#include <mkw/native_mii_head.hpp>
#include <mkw/mii_body.hpp>

namespace mkw {
// A seated ceremony actor. No live race configuration, kart, item or physics
// pointers survive in this owner. The scene supplies the original choreography.
class CeremonyVehicle {
public:
 bool load(unsigned character,unsigned vehicle,RaceTeam team,std::string* failure=nullptr);
 bool pose(const VehicleAnimation::Snapshot&,const DriverPose&,unsigned tick);
 void draw(Renderer&)const;
 Renderer::ShadowVolume shadow()const;
 float clipDuration(const char* name)const;
 bool faceHeight(float& height)const;
 void freezeLighting();
 bool prepareMii(const NativeMii&);
private:
 std::unique_ptr<NativeMiiHead> head_;MiiBodySpec mii_{};NativeMiiHead::Expression expression_=NativeMiiHead::Normal;
 VehicleVisualAssets assets_;
 VehicleAnimation vehicle_;
 C3D_Mtx seated_{};
 bool posed_=false;
};
}
