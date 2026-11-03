#pragma once
#include <mkw/race_results.hpp>
#include <mkw/gp_rank.hpp>
#include <mkw/license_identity.hpp>
#include <array>

namespace mkw {
// Scene-owned copy made before the race's heaps and result controls are freed.
// Standings retain the completed leaderboard order and original player ids.
struct AwardResults {
 struct Player {uint8_t racer=0,character=0,vehicle=0;bool human=false;RaceTeam team=RaceTeam::None;unsigned points=0;};
 NativeMii raceMii{};
 bool valid=false,teams=false,mirror=false,coinRunners=false;
 // Unknown until the race's original GP performance counters are captured.
 int grade=-1;
 unsigned mode=0,cup=0,engineClass=2,count=0;
 RaceTeam humanTeam=RaceTeam::None;
 std::array<Player,12> players{};
 std::array<RaceTeamScore,2> teamScores{};
 // Completed race snapshots survive scene teardown for the GP grade.
 std::array<GpRacePerformance,4> performance{};
 std::array<bool,4> performanceCaptured{};
 // AwardResults::PrepareResultItems (USA 80622b84..80622c18) uses
 // separate six-row columns for teams. Retain the score order within each.
 unsigned boardSlot(unsigned player)const{
  if(!teams)return count<=3?12+player:player;
  const auto team=players[player].team;
  unsigned row=0;
  for(unsigned i=0;i<player;++i)if(players[i].team==team)++row;
  return (team==RaceTeam::Blue?6u:0u)+row;
 }
 void calculateGrade(const std::array<unsigned,4>& courses){
  grade=-1;
  if(!valid||mode!=0||teams)return;
  for(unsigned i=0;i<count;++i)if(players[i].human){
   if(const auto result=gpCupGrade(performance,performanceCaptured,courses,engineClass,players[i].points))grade=int(*result);
   return;
  }
 }
 void capture(const RaceResultsFlow& results,unsigned raceMode,unsigned cupId,unsigned cc,bool mirrored,
              const std::array<uint8_t,12>& vehicles){
  *this={};
  if(!results.finalRace()||!results.totals()||results.rowCount()==0)return;
  mode=raceMode;cup=cupId;engineClass=cc;mirror=mirrored;teams=results.teams();humanTeam=results.humanTeam();
  count=std::min<unsigned>(results.rowCount(),players.size());
  for(unsigned i=0;i<count;++i){
   const auto& row=results.row(i);
   players[i]={row.racer,row.character,vehicles[row.racer],row.human,row.team,unsigned(row.previous)+row.gain};
  }
  teamScores={results.teamScore(RaceTeam::Red),results.teamScore(RaceTeam::Blue)};
  valid=true;
 }
};
}
