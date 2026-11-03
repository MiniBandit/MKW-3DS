#include <mkw/award_scene_assets.hpp>
#include <mkw/award_lighting.hpp>
#include <mkw/award_environment_data.hpp>

namespace mkw {
AwardSceneAssets::AwardSceneAssets(const AwardResults& results,unsigned environmentId,RaceTeam winner,unsigned trophyRank)
 :results_(results),environment_(environmentId),trophyRank_(trophyRank),winner_(winner){label="award scene";}
void AwardSceneAssets::run(){
 Model::setLightingCourse(environment_<std::size(AwardLightings)?&AwardLightings[environment_]:nullptr);
 if(cancelled())return;
 if(!results_.valid||environment_>=std::size(AwardEnvironments)||trophyRank_>3){failure_="award configuration";return;}
 const auto selection=makeAwardCast(results_,winner_);
 if(environment_==0&&!selection.valid){failure_="award cast selection";return;}
 // Keep all allocations local until every required model and animation is ready.
 auto scenery=std::make_unique<AwardEnvironmentJob>(AwardEnvironments[environment_]);
 scenery->run();
 if(!scenery->loaded){failure_="environment: ";failure_+=scenery->detail();return;}
 if(cancelled())return;
 std::unique_ptr<AwardCastJob> actors;
 std::unique_ptr<AwardDrive> driving;
 std::unique_ptr<AwardParadeJob> vehicles;
 if(environment_==0){
  // RaceConfig::initAwards only builds the three-racer winning run for
  // GAMETYPE_GRAND_PRIX_WIN. VS goes straight to its result ceremony.
  if(results_.mode==0&&!results_.teams){
   driving=std::make_unique<AwardDrive>();
   if(!driving->load(results_)){failure_="driving: ";failure_+=driving->detail();return;}
  }else{
   actors=std::make_unique<AwardCastJob>(selection);actors->run();
   if(!actors->loaded){failure_="cast: ";failure_+=actors->detail();return;}
  }
 }else if(environment_==1){
  vehicles=std::make_unique<AwardParadeJob>(results_,AwardEnvironments[environment_]);vehicles->run();
  if(!vehicles->loaded){failure_="vehicles: ";failure_+=vehicles->detail();return;}
 }else{failure_="ceremony outcome choreography";return;}
 if(cancelled())return;
 std::unique_ptr<AwardTrophyJob> prize;
 if(trophyRank_){
  prize=std::make_unique<AwardTrophyJob>(results_.mode==0?results_.cup:8,trophyRank_);
  prize->run();
  if(!prize->model){failure_="award trophy model, motion or medal material";return;}
 }
 if(cancelled())return;
 environment=std::move(scenery);drive=std::move(driving);cast=std::move(actors);parade=std::move(vehicles);trophy=std::move(prize);
 failure_.clear();loaded=true;
}
bool AwardSceneAssets::loadPodium(){
 if(cast)return true;
 // Caller retires GPU work before discarding the moving vehicles.
 drive.reset();
 auto actors=std::make_unique<AwardCastJob>(makeAwardCast(results_,winner_));actors->run();
 if(!actors->loaded){failure_="cast: ";failure_+=actors->detail();return false;}
 cast=std::move(actors);return true;
}

}
