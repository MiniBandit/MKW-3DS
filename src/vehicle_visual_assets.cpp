#include <mkw/vehicle_visual_assets.hpp>
#include <mkw/mii_garage.hpp>
#include <cstdio>
#include <cstring>

namespace mkw {
bool VehicleVisualAssets::load(unsigned character,unsigned vehicle,RaceTeam team,std::string* failure){
 VehicleVisualAssets next;
 if(failure)*failure="vehicle selection";
 for(const auto& entry:GarageChoices)
  if(entry.character==character&&entry.vehicle==vehicle){next.choice=&entry;break;}
 if(!next.choice)for(const auto& entry:MiiGarageChoices)if(entry.character==character&&entry.vehicle==vehicle){next.choice=&entry;break;}
 if(!next.choice)return false;
 auto path=[&](const char* part,const char* extension){
  const char* suffix=!std::strcmp(part,"driver")||!std::strcmp(part,"shadow")?"":teamVehicleSuffix(vehicle,team);
  char name[160];std::snprintf(name,sizeof name,"models/%s%s%s.%s",next.choice->prefix,suffix,part,extension);
  return std::string(name);
 };
 auto model=[&](const char* part,std::unique_ptr<Model>& destination){
  const auto name=path(part,"gmdl");if(failure)*failure=name;
  auto asset=std::make_unique<Model>();if(!asset->load(name.c_str()))return false;
  destination=std::move(asset);return true;
 };
 auto bank=[&](const char* part,Model& mesh,std::unique_ptr<DriverAnimation>& destination){
  const auto name=path(part,"dskm");if(failure)*failure=name;
  auto asset=std::make_unique<DriverAnimation>();if(!asset->load(name.c_str(),mesh))return false;
  asset->prepareLitSkinning(mesh);destination=std::move(asset);return true;
 };
 if(!model("body",next.body)||!model("driver",next.driver)||!model("shadow",next.shadow)||
    !bank("driver",*next.driver,next.animation))return false;
 constexpr const char* wheelNames[]={"tire_fl","tire_fr","tire_rl","tire_rr"};
 for(unsigned i=0;i<(next.choice->bike?2u:4u);++i)
  if(!model(wheelNames[i],next.wheels[i]))return false;
 if(next.choice->bodyAnimation&&!bank("body",*next.body,next.bodyAnimation))return false;
 for(unsigned i=0;i<next.extras.size();++i)if(next.choice->extrasMask&(1u<<i)){
  if(!model(GarageExtraNames[i],next.extras[i]))return false;
  if((next.choice->extraAnimationMask&(1u<<i))&&!bank(GarageExtraNames[i],*next.extras[i],next.extraAnimation[i]))return false;
 }
 *this=std::move(next);if(failure)failure->clear();return true;
}
}
