#include <mkw/battle_result_panel.hpp>
#include <mkw/battle_result_assets.hpp>
#include <mkw/race_results.hpp>
#include <cstdio>
#include <cstring>
namespace mkw {
void BattleResultPanel::step(const RaceResultsFlow& flow){
 if(!flow.battle()||!flow.boardVisible()){reset();return;}
 if(!started_){started_=true;title_=flowPlay(FlowBattleWinLayout,"Fade","Hide");}
 // ResultBattleUpdate alone creates BattleWin (8083a9d4..aa44).
 // Page+20 is the clock since activation, including the entry slide;
 // 8083aa90..aab8 starts Fadein at20. It continues during the exit slide.
 if(!flow.totals()){
  if(!titleStarted_&&flow.phase()!=RaceFlowPhase::UpdateOut&&flow.pageFrame()>=20){
   titleStarted_=true;title_=flowPlay(FlowBattleWinLayout,"Fade","Fadein");
  }else if(titleStarted_&&title_.clip>=0){
   const auto& clip=FlowBattleWinLayout.clips[title_.clip];
   // Native Fadein returns to the inactive Hide pose at frame0. Do not
   // advance the two-frame visibility selector to its visible frame1.
   if(std::strcmp(clip.name,"Hide")){
    if(title_.frame+clip.rate>=clip.frames)title_=flowPlay(FlowBattleWinLayout,"Fade","Hide");
    else flowAdvance(FlowBattleWinLayout,title_,1.f);
   }
  }
 }
}
void BattleResultPanel::draw(const RaceResultsFlow& flow,FlowDrawer& drawer,FlowSink sink,void* context){
 if(!started_||!flow.battle()||!flow.boardVisible())return;
 for(unsigned team=0;team<2;++team){
  const auto& col=flow.totals()?FlowTeamColumns[team]:FlowBattleColumns[team];
  if(!flow.totals()){
   const auto& point=FlowBattleWinPoints[team];
   FlowControl at{col.x+point.x+flow.slideOffset(col.delay),col.y+point.y,col.sx*point.sx,col.sy*point.sy,0};
   auto play=flowPlay(FlowBattleWinPointLayout,"team",team?"blue":"red");
   char score[12];std::snprintf(score,sizeof score,"%u",flow.teamShown(RaceTeam(team)));
   const FlowAwardBinding text[]={{"point",score,-2,team?&FlowBattleNumberBlue:&FlowBattleNumberRed,true}};
   drawer.drawAward(FlowBattleWinPointLayout,at,&play,1,text,1,sink,context);
  }else{
   // 807ec188..208 creates BattleSetPoint children instead of a big total.
   // 80841c8c: capsule scale .8+1/n, spacing42*scale, y150.
   const unsigned n=std::max(1u,flow.battleTarget()),columns=n>5?(n+1)/2:n;const float scale=.8f+1.f/float(columns);
   const auto& def=team?FlowBattleMarkBlueLayout:FlowBattleMarkRedLayout;
   for(unsigned i=0;i<n;++i){
    const bool lit=i<flow.battleWins(RaceTeam(team));
    FlowPlay plays[]={flowPlay(def,"OnOff",lit?"On":"Off"),flowPlay(def,"Loop",i+1==flow.battleWins(RaceTeam(team))&&flow.winningTeam()==RaceTeam(team)?"Loop":"Stop")};
    flowAdvance(def,plays[0],float(flow.pageFrame()));flowAdvance(def,plays[1],float(flow.pageFrame()));
    // The source scales capsule_null, not the control's translation.
    FlowControl at{col.x+42.f*scale*(float(i%columns)-float(columns-1)*.5f)+flow.slideOffset(col.delay),col.y+150.f+(n>5?(i<columns?21.f:-21.f)*scale:0.f),scale,scale,0};
    drawer.drawAward(def,at,plays,2,nullptr,0,sink,context);
   }
  }
 } if(!flow.totals()){
  if(titleStarted_){
  auto title=FlowBattleWinControl;title.x+=flow.slideOffset(title.delay);
  const unsigned winner=flow.winningTeam()==RaceTeam::Red?0:flow.winningTeam()==RaceTeam::Blue?1:2;
  const FlowAwardBinding text[]={{"text_00",FlowBattleWinText[winner],-2,&FlowBattleWinFont},{"text_light",FlowBattleWinText[winner],-2,&FlowBattleWinFont}};
  drawer.drawAward(FlowBattleWinLayout,title,&title_,1,text,2,sink,context);
  }
 }

}
}
