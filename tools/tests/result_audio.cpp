#include <mkw/race_results.hpp>
#include <mkw/sound_ids.hpp>
#include <cassert>
using namespace mkw;
static void check(bool teams){
 RaceResultsFlow flow;
 RaceResultEntry rows[12]{};
 for(unsigned i=0;i<12;++i){rows[i].racer=i;rows[i].human=i==0;rows[i].previous=20;rows[i].gain=12-i;rows[i].team=teams?(i%2?RaceTeam::Blue:RaceTeam::Red):RaceTeam::None;}
 flow.finish();
 unsigned ordinaryIn=0,teamIn=0,out=0,points=0;
 for(unsigned tick=0;tick<2200&&flow.phase()!=RaceFlowPhase::Menu;++tick){
  const auto events=flow.step({true,false});
  if(events&RaceResultsFlow::EventFill)flow.fill(rows,12,false);
  const int sound=flow.slideSound();
  ordinaryIn+=sound==snd::id::SE_RSLT_IN;
  teamIn+=sound==snd::id::SE_RSLT_IN_TEAM;
  out+=sound==snd::id::SE_RSLT_OUT;
  points+=flow.pointsChanged();
 }
 assert(flow.phase()==RaceFlowPhase::Menu);
 assert(ordinaryIn==(teams?12:24));
 assert(teamIn==(teams?2:0));
 assert(out==(teams?14:24));
 assert(points>0);
 for(int i=0;i<20;++i){flow.step({});assert(flow.slideSound()==-1);assert(!flow.pointsChanged());}
 flow.reset();assert(flow.slideSound()==-1);assert(!flow.pointsChanged());
}
int main(){check(false);check(true);}
