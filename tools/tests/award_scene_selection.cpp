#include <mkw/award_scene.hpp>
#include <cassert>
using namespace mkw;
int main(){
 AwardResults results;
 assert(!hasAwardPodium(results));
 results.valid=true;results.count=12;
 for(unsigned rank=0;rank<12;++rank){
  for(auto& player:results.players)player.human=false;
  results.players[rank].human=true;
  assert(hasAwardPodium(results)==(rank<3));
  assert(hasAwardScene(results));
  assert(awardOutcome(results)==(rank<3?AwardOutcome::Podium:AwardOutcome::Loss));
 }
 results.teams=true;
 for(const auto team:{RaceTeam::Red,RaceTeam::Blue}){
  results.humanTeam=team;
  const unsigned t=unsigned(team);
  results.teamScores[t]={90,10};results.teamScores[t^1]={100,0};
  assert(!hasAwardPodium(results)); // Tie uses the loss scene with its draw layout.
  assert(awardOutcome(results)==AwardOutcome::Draw);assert(hasAwardScene(results));
  results.teamScores[t].gain=11;assert(hasAwardPodium(results));
  results.teamScores[t].gain=9;assert(!hasAwardPodium(results));
  assert(awardOutcome(results)==AwardOutcome::Loss);
 }
 // A mixed overall ranking still groups the source team board into columns.
 results.count=12;results.teams=true;
 for(unsigned i=0;i<12;++i)results.players[i].team=i%2?RaceTeam::Red:RaceTeam::Blue;
 for(unsigned i=0;i<12;++i)assert(results.boardSlot(i)==i/2+(i%2?0:6));
 results.teams=false;
 for(unsigned i=0;i<12;++i)assert(results.boardSlot(i)==i);
 results.count=3;
 for(unsigned i=0;i<3;++i)assert(results.boardSlot(i)==12+i);
 results.humanTeam=RaceTeam::Red;
 results.count=0;assert(awardOutcome(results)==AwardOutcome::None);
 results.count=13;assert(awardOutcome(results)==AwardOutcome::None);
 results.count=12;
 for(auto& player:results.players)player.human=false;
 assert(awardOutcome(results)==AwardOutcome::None);
 results.humanTeam=RaceTeam::None;assert(!hasAwardPodium(results));
}
