#include <mkw/race_teams.hpp>
#include <mkw/race_results.hpp>
#include <mkw/race_flow_draw.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
static unsigned draws=0;
static void sink(const FlowBatch& b,void*){draws+=b.count;}
int main(){
 for(auto human:{RaceTeam::Red,RaceTeam::Blue}){
  auto teams=offlineVsTeams(true,human);unsigned counts[2]{};
  for(unsigned i=0;i<12;++i){++counts[unsigned(teams[i])];assert(teams[i]==RaceTeam(unsigned(human)^(i&1)));}
  assert(counts[0]==6&&counts[1]==6&&teams[0]==human);
 }
 for(auto t:offlineVsTeams(false,RaceTeam::Red))assert(t==RaceTeam::None);
 assert(!sameRaceTeam(RaceTeam::None,RaceTeam::None));assert(!sameRaceTeam(RaceTeam::Blue,RaceTeam::Red));assert(sameRaceTeam(RaceTeam::Blue,RaceTeam::Blue));
 RaceResultEntry entries[12]{};
 unsigned sums[2]{};
 for(unsigned i=0;i<12;++i){auto& e=entries[i];e.racer=i;e.character=i;e.human=i==0;e.previous=i;e.gain=RaceResultsFlow::points(12,i+1);e.team=RaceTeam(i&1);sums[i&1]+=e.previous+e.gain;}
 RaceResultsFlow flow;flow.fill(entries,12,false);
 assert(flow.teams()&&flow.humanTeam()==RaceTeam::Red);
 assert(flow.teamScore(RaceTeam::Red).total()==sums[0]);assert(flow.teamScore(RaceTeam::Blue).total()==sums[1]);
 assert(flow.teamDifference()==int(sums[0])-int(sums[1]));
 flow.finish();for(unsigned i=0;i<220;++i)flow.step({});assert(flow.boardVisible());
 FlowDrawer drawer;drawer.reset();drawer.step(flow);drawer.draw(flow,sink,nullptr);assert(draws);
 for(unsigned i=0;i<1000&&!flow.totals();++i)flow.step({true,false});assert(flow.totals());
 for(unsigned i=0;i<70;++i){flow.step({});drawer.step(flow);}draws=0;drawer.draw(flow,sink,nullptr);assert(draws>100);
 while(flow.phase()!=RaceFlowPhase::Total)flow.step({});
 while(flow.teamCounting()){flow.step({});assert(flow.phase()==RaceFlowPhase::Total);}
 for(unsigned i=0;i<flow.rowCount();++i)assert(flow.teamRowShown(i)==flow.row(i).previous+flow.row(i).gain);
 assert(flow.teamShown(RaceTeam::Red)==sums[0]&&flow.teamShown(RaceTeam::Blue)==sums[1]);
 assert(FlowTeamRowLayout.paneCount<=FlowPose::MaxPanes&&FlowTeamRowLayout.materialCount<=FlowPose::MaxMaterials);
 flow.reset();for(auto& e:entries)e.team=RaceTeam::None;flow.fill(entries,12,false);assert(!flow.teams()&&flow.teamDifference()==0);
 entries[0].team=RaceTeam::Red;entries[1].team=RaceTeam::Blue;entries[0].previous=entries[1].previous=10;entries[0].gain=entries[1].gain=5;flow.fill(entries,2,true);assert(flow.winningTeam()==RaceTeam::None);
 std::printf("PASS six-per-team assignment, sentinel handling, team aggregation/ties, source row draw (%u quads); FlowDrawer %zu bytes\n",draws,sizeof(FlowDrawer));
}
