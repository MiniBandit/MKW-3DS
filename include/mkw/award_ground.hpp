#pragma once
#include <mkw/math.hpp>
#include <game/field/KColData.hh>
#include <cstdio>
#include <memory>
#include <vector>

namespace mkw {
// Ceremony-owned collision data: the racing scene has already been destroyed.
class AwardGround {
public:
 bool load(const char* path){
  FILE* f=std::fopen(path,"rb");if(!f)return false;
  std::fseek(f,0,SEEK_END);const long n=std::ftell(f);std::rewind(f);
  if(n<0x3c){std::fclose(f);return false;}
  bytes_.resize(n);const bool ok=std::fread(bytes_.data(),1,n,f)==size_t(n);std::fclose(f);
  if(!ok)return false;
  data_=std::make_unique<Kinoko::Field::KColData>(bytes_.data());return true;
 }
 bool place(Vec3 route,float rideHeight,Vec3& position,Vec3& up){
  using namespace Kinoko::Field;
  // ENPT is a navigation target, not a wheel contact. Probe the floor under
  // the path and apply the BSP ride height along its normal.
  // The scene KCL caps sphere queries at 250 units. Use the same 100-unit
  // ground probe as the loss parade so the correction uses the actual radius.
  constexpr float radius=100.f,lift=0.f;
  Kinoko::EGG::Vector3f center{route.x,route.y+lift,route.z};
  data_->lookupSphere(radius,center,Kinoko::EGG::Vector3f::inf,KCL_TYPE_FLOOR);
  float distance,best=-1;Kinoko::EGG::Vector3f normal,bestNormal;uint16_t attribute;
  while(data_->checkSphereCollision(&distance,&normal,&attribute))
   if(normal.y>.1f&&distance>best){best=distance;bestNormal=normal;}
  if(best<0)return false;
  center+=bestNormal*(best-radius+rideHeight);
  position={center.x,center.y,center.z};up={bestNormal.x,bestNormal.y,bestNormal.z};return true;
 }
private:
 std::vector<unsigned char> bytes_;
 std::unique_ptr<Kinoko::Field::KColData> data_;
};
}
