#pragma once
#include <mkw/award_results.hpp>

namespace Kinoko::EGG { class SceneManager; }
namespace mkw {
class Renderer;
enum class AwardSceneResult {Complete,Failed,Exit};
enum class AwardOutcome {None,Podium,Loss,Draw};
inline AwardOutcome awardOutcome(const AwardResults& results){
 if(!results.valid||!results.count||results.count>results.players.size())return AwardOutcome::None;
 bool human=false;
 for(unsigned i=0;i<results.count;++i)human|=results.players[i].human;
 if(!human)return AwardOutcome::None;
 if(results.teams){
  if(results.humanTeam!=RaceTeam::Red&&results.humanTeam!=RaceTeam::Blue)return AwardOutcome::None;
  const unsigned team=unsigned(results.humanTeam);
  const auto own=results.teamScores[team].total(),other=results.teamScores[team^1].total();
  return own==other?AwardOutcome::Draw:own>other?AwardOutcome::Podium:AwardOutcome::Loss;
 }
 for(unsigned i=0;i<results.count;++i)if(results.players[i].human)
  return i<3?AwardOutcome::Podium:AwardOutcome::Loss;
 return AwardOutcome::None;
}
// Retail Next (USA 8083900c..808391bc) sends team ties through
// loser_demo/camera 12 as well; the result layout supplies the draw message.
inline bool hasAwardPodium(const AwardResults& results){return awardOutcome(results)==AwardOutcome::Podium;}
inline bool hasAwardScene(const AwardResults& results){const auto outcome=awardOutcome(results);return outcome!=AwardOutcome::None;}
// Caller has drained race jobs and destroyed the race child scene. This loop
// owns ceremony resources until it returns; the driving phase owns a new ceremony simulation.
AwardSceneResult runAwardPodium(Renderer&,const AwardResults&,void (*report)(const char*),std::uint64_t transitionStart=0,Kinoko::EGG::SceneManager* scenes=nullptr);
}
