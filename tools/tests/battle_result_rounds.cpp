#include <mkw/battle_session.hpp>
#include <mkw/race_results.hpp>
#include <mkw/race_flow_draw.hpp>
#include <mkw/battle_result_panel.hpp>
#include <mkw/battle_result_assets.hpp>
#include <mkw/sound_ids.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace mkw;
namespace {
struct Capture {
 std::vector<std::string> text;
 unsigned quads=0;
 static void collect(const FlowBatch& batch,void* context){
  auto& c=*static_cast<Capture*>(context);c.quads+=batch.count;
  std::string text;
  for(unsigned n=0;n<batch.count;++n){const auto& q=batch.quads[n];
   for(unsigned k=0;k<4;++k)assert(std::isfinite(q.x[k])&&std::isfinite(q.y[k]));
   if(batch.texture!=FlowKanjiFont.texture)continue;
   for(const auto& g:FlowKanjiGlyphs)if(std::abs(q.u[0]-float(g.x)/FlowKanjiFont.atlasW)<1e-6f&&
      std::abs(q.v[0]-float(g.y)/FlowKanjiFont.atlasH)<1e-6f){text+=char(g.code);break;}
  }
  if(!text.empty())c.text.push_back(text);
 }
 bool contains(const char* s)const{for(const auto& t:text)if(t==s)return true;return false;}
};
RaceResultsFlow results(const BattleSession& session,const std::array<RaceTeam,12>& teams,unsigned count){
 RaceResultsFlow flow;flow.finish();
 for(unsigned tick=0;tick<300;++tick)if(flow.step({})&RaceResultsFlow::EventFill){
  RaceResultEntry rows[12];
  // Intentionally reverse source order: native per-page comparators, not
  // the race rank, determine the two teams' individual row order.
  for(unsigned slot=0;slot<count;++slot){const unsigned i=count-slot-1;
   rows[slot]={uint8_t(i),uint8_t(i),i==0,uint16_t(session.previousScore(i)),uint16_t(session.roundScore(i)),teams[i]};}
  flow.fill(rows,count,session.complete());flow.setBattle(session.wins(RaceTeam::Red),session.wins(RaceTeam::Blue),session.target());
  return flow;
 }
 assert(false);return flow;
}
unsigned rowFor(const RaceResultsFlow& flow,unsigned racer){for(unsigned i=0;i<flow.rowCount();++i)if(flow.row(i).racer==racer)return i;assert(false);return 0;}
void active(RaceResultsFlow& flow,RaceFlowPhase wanted){
 for(unsigned guard=0;flow.phase()!=wanted&&guard<500;++guard)flow.step({});assert(flow.phase()==wanted);
}
}
int main(){
 std::array<RaceTeam,12> teams;for(unsigned i=0;i<12;++i)teams[i]=RaceTeam(i&1);
 BattleSession set;set.reset(10);
 std::array<uint16_t,12> scores{60,8,3,4,7,1};
 assert(set.seal(scores,teams,6));assert(set.totalScore(0)==60&&set.previousScore(0)==0&&set.roundWinner()==RaceTeam::Red);
 scores[0]=999;assert(!set.seal(scores,teams,6));assert(set.totalScore(0)==60&&set.roundScore(0)==60); // no double accumulation
 assert(set.nextRound());scores={1,15,6,2,7,9};assert(set.seal(scores,teams,6));
 const unsigned previous[]={60,8,3,4,7,1},total[]={61,23,9,6,14,10};
 for(unsigned i=0;i<6;++i)assert(set.previousScore(i)==previous[i]&&set.totalScore(i)==total[i]);
 assert(set.roundWinner()==RaceTeam::Blue&&set.wins(RaceTeam::Red)==1&&set.wins(RaceTeam::Blue)==1);
 auto flow=results(set,teams,6);
 assert(flow.winningTeam()==RaceTeam::Blue&&flow.teamDifference()==-12); // cumulative red84 must not win this round
 const unsigned updateOrder[]={1,5,4,2,3,0};for(unsigned i=0;i<6;++i)assert(flow.row(i).racer==updateOrder[i]);
 for(unsigned frame=0;frame<74;++frame){
  assert(flow.phase()==RaceFlowPhase::UpdateIn&&flow.pageFrame()==frame);
  if(frame==45||frame==50)assert(flow.slideSound()==snd::id::SE_RSLT_IN_TEAM);
  for(unsigned i=0;i<6;++i)assert(flow.teamRowShown(i)==0);
  flow.step({true,false});
 }
 assert(flow.phase()==RaceFlowPhase::Update&&flow.pageFrame()==74);
 flow.step({true,false});assert(flow.phase()==RaceFlowPhase::Update&&flow.teamShown(RaceTeam::Red)==1&&flow.teamShown(RaceTeam::Blue)==1);
 for(unsigned i=0;i<6;++i)assert(flow.teamRowShown(i)==0); // first.5 tick truncates
 flow.step({true,false});for(unsigned i=0;i<6;++i)assert(flow.teamRowShown(i)==1);
 for(unsigned guard=0;flow.phase()==RaceFlowPhase::Update&&guard<1000;++guard)flow.step({true,false});
 assert(flow.phase()==RaceFlowPhase::UpdateOut&&!flow.teamCounting());
 for(unsigned i=0;i<6;++i)assert(flow.teamRowShown(rowFor(flow,i))==scores[i]);
 assert(flow.teamShown(RaceTeam::Red)==14&&flow.teamShown(RaceTeam::Blue)==26);
 FlowDrawer drawer;drawer.step(flow);Capture current;drawer.draw(flow,Capture::collect,&current);
 assert(current.quads&&current.contains("14")&&current.contains("26")); // production two-column round totals, not VS rank rows
 assert(current.contains("15")&&current.contains("7")&&current.contains("6"));
 active(flow,RaceFlowPhase::TotalIn);
 const unsigned totalOrder[]={0,1,4,5,2,3};for(unsigned i=0;i<6;++i)assert(flow.row(i).racer==totalOrder[i]);
 for(unsigned i=0;i<6;++i)assert(flow.teamRowShown(rowFor(flow,i))==previous[i]);
 assert(flow.winningTeam()==RaceTeam::Blue&&flow.teamDifference()==-12); // winner does not change on cumulative board
 active(flow,RaceFlowPhase::Total);
 flow.step({true,false});assert(flow.phase()==RaceFlowPhase::Total);
 for(unsigned i=0;i<6;++i)assert(flow.teamRowShown(rowFor(flow,i))==previous[i]);
 flow.step({true,false});assert(flow.teamRowShown(rowFor(flow,0))==61&&flow.teamRowShown(rowFor(flow,1))==9);
 for(unsigned guard=0;flow.phase()==RaceFlowPhase::Total&&guard<1000;++guard)flow.step({true,false});
 assert(flow.phase()==RaceFlowPhase::TotalOut&&!flow.teamCounting());
 for(unsigned i=0;i<6;++i)assert(flow.teamRowShown(rowFor(flow,i))==total[i]);
 Capture cumulative;drawer.step(flow);drawer.draw(flow,Capture::collect,&cumulative);
 assert(cumulative.quads&&cumulative.contains("61")&&cumulative.contains("23"));
 assert(!cumulative.contains("84")&&!cumulative.contains("39")); // totals page header belongs to set wins, not a team sum
 // The native comparator resolves a point tie by local player, previous
 // cumulative score, then racer id, separately in each team's subset.
 RaceResultsFlow ties;RaceResultEntry tieRows[]={{5,5,false,3,5,RaceTeam::Blue},{2,2,false,10,5,RaceTeam::Red},
  {0,0,true,0,5,RaceTeam::Red},{1,1,false,10,5,RaceTeam::Blue},{4,4,false,10,5,RaceTeam::Red}};
 ties.fill(tieRows,5,false);ties.setBattle(0,0,10);
 const unsigned tied[]={0,1,2,4,5};for(unsigned i=0;i<5;++i)assert(ties.row(i).racer==tied[i]);
 // Actual halfword writer wraps, rather than clamping to a made-up carry
 // limit. The totals page then switches to the wrapped target on calc.
 BattleSession wrap;wrap.reset(10);scores={65530,0};assert(wrap.seal(scores,teams,2)&&wrap.nextRound());
 scores={10,0};assert(wrap.seal(scores,teams,2)&&wrap.previousScore(0)==65530&&wrap.totalScore(0)==4);
 auto wrapped=results(wrap,teams,2);active(wrapped,RaceFlowPhase::Update);
 for(unsigned i=0;i<50&&wrapped.phase()==RaceFlowPhase::Update;++i)wrapped.step({true,false});
 active(wrapped,RaceFlowPhase::Total);assert(wrapped.teamRowShown(rowFor(wrapped,0))==65530);
 wrapped.step({});assert(wrapped.teamRowShown(rowFor(wrapped,0))==4&&!wrapped.teamCounting());
 set.reset();for(unsigned i=0;i<12;++i)assert(!set.previousScore(i)&&!set.totalScore(i)&&!set.roundScore(i));
 puts("PASS production two-round Battle session snapshot/idempotence, halfword accumulation/wrap/reset, both BT result pages, authored74-frame entrance and team cues, per-page row sort and active-only.5/1 counting, dismissal gates and actual round/cumulative score draw batches; target presentation pending");
}
