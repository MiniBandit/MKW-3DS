#pragma once
#include <mkw/ceremony_vehicle.hpp>
#include <mkw/award_environment.hpp>
#include <mkw/award_results.hpp>
#include <mkw/asset_loader.hpp>
namespace mkw {
class AwardParadeJob final:public AssetJob {
public:
 AwardParadeJob(const AwardResults& results,const AwardEnvironmentDef& environment):results_(results),environment_(environment){label="ceremony vehicles";}
 void run()override;
 bool prepareMii(const NativeMii&);
 bool loaded=false;
 const char* detail()const override{return failure_.c_str();}
 void update(unsigned tick);
 void draw(Renderer&)const;
private:
 struct Actor {std::unique_ptr<CeremonyVehicle> model;VehicleAnimation::Snapshot initial;};
 AwardResults results_;const AwardEnvironmentDef& environment_;
 std::vector<Actor> actors_;std::string failure_;
};
}
