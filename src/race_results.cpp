#include <mkw/race_results.hpp>
#include <mkw/race_flow_layout.hpp>
#include <mkw/battle_result_assets.hpp>
#include <mkw/sound_ids.hpp>
#include <algorithm>

namespace mkw {
void RaceResultCount::fill(unsigned previous,unsigned gain){
 // FillScore: shown x10, then fn_1_2E53E8: remaining = 10 x gain, the
 // target is their sum and offline rows always count by +/-2 per frame.
 score10=float(previous)*10.f;
 remaining=float(gain)*10.f;
 target10=int(remaining+score10);
 step=gain>0?2.f:-2.f;
 rising=gain>0;
 shown=int(previous);
}

void RaceResultCount::update(){
 // CtrlRaceResult::OnUpdate while the page is active. The shown number is
 // the truncated tenths plus one (retail rounds up the moment counting
 // starts), clamped to the target; reaching it ends the count.
 if(rising&&remaining>.05f){
  score10+=step;remaining-=step;
  int next=int(score10)/10+1;
  if(target10/10<next){remaining=0;next=target10/10;}
  shown=next;
 }else if(!rising&&remaining< -.05f){
  score10+=step;remaining-=step;
  int next=int(score10)/10-1;
  if(next<target10/10){remaining=0;next=target10/10;}
  shown=next;
 }
}

void RaceResultsFlow::reset(){*this=RaceResultsFlow{};}

void RaceResultsFlow::finish(bool timeTrial){
 if(phase_!=RaceFlowPhase::Racing)return;
 timeTrial_=timeTrial;
 enter(RaceFlowPhase::Finished);finishFrame_=0;
}

float RaceResultsFlow::hudSlide()const{
 if(phase_<RaceFlowPhase::HudExit)return -1.f;
 if(phase_>RaceFlowPhase::HudExit)return 1.f;
 return std::min(1.f,float(pageFrame_)*.05f);
}

bool RaceResultsFlow::teamCounting()const{
 if(!teams_)return false;
 for(unsigned i=0;i<count_;++i)if(teamRowDisplayed_[i]<teamRowTarget(i))return true;
 if(!battle_||!totals())for(unsigned i=0;i<2;++i)if(teamDisplayed_[i]<teamScores_[i].total())return true;
 return false;
}

bool RaceResultsFlow::counting()const{
 if(battle_)return teamCounting();
 for(unsigned i=0;i<count_;++i)if(counts_[i].busy())return true;
 return false;
}

float RaceResultsFlow::slideOffset(float delay)const{
 // Page::AnimateControls 0x805DDD7C: forward pages enter from +W and leave
 // toward -W; each control moves linearly over 10 frames after its delay.
 const float t=float(pageFrame_);
 const float p=std::clamp((t-delay)/float(SlideFrames),0.f,1.f);
 switch(phase_){
 case RaceFlowPhase::UpdateIn:case RaceFlowPhase::TotalIn:case RaceFlowPhase::MenuIn:return SlideWidth*(1.f-p);
 case RaceFlowPhase::UpdateOut:case RaceFlowPhase::TotalOut:return -SlideWidth*p;
 default:return 0;
 }
}

int RaceResultsFlow::slideSound()const{
 const bool entering=phase_==RaceFlowPhase::UpdateIn||phase_==RaceFlowPhase::TotalIn;
 const bool leaving=phase_==RaceFlowPhase::UpdateOut||phase_==RaceFlowPhase::TotalOut;
 if(!entering&&!leaving)return -1;
 const bool teamBoard=teams_&&(battle_||totals());
 bool starts=false;
 if(teamBoard){
  for(unsigned team=0;team<2;++team)starts|=float(pageFrame_)==(battle_&&!totals()?FlowBattleColumns[team]:FlowTeamColumns[team]).delay;
 }else{
  for(unsigned i=0;i<count_;++i)starts|=float(pageFrame_)==FlowResultRows[i].delay;
 }
 if(!starts)return -1;
 return leaving?snd::id::SE_RSLT_OUT:teamBoard?snd::id::SE_RSLT_IN_TEAM:snd::id::SE_RSLT_IN;
}

void RaceResultsFlow::fill(const RaceResultEntry* byPosition,unsigned count,bool finalRace){
 count_=std::min(count,12u);final_=finalRace;filled_=true;
 teamScores_={};teams_=false;battle_=false;humanTeam_=RaceTeam::None;
 for(unsigned i=0;i<count_;++i){const auto& row=byPosition[i];
  if(row.team==RaceTeam::None)continue;
  teams_=true;auto& score=teamScores_[unsigned(row.team)];score.previous+=row.previous;score.gain+=row.gain;
  if(row.human)humanTeam_=row.team;
 }
 for(unsigned i=0;i<count_;++i){rows_[i]=byPosition[i];counts_[i].fill(rows_[i].previous,rows_[i].gain);}
}

void RaceResultsFlow::setBattle(unsigned redWins,unsigned blueWins,unsigned target){
 battle_=true;battleWins_={redWins,blueWins};battleTarget_=target;
 // ResultBattleUpdate33 uses two BT team controls, current scores from0.
 // Cumulative previous/total scores belong to the later34 page only.
 teamScores_={};teamDisplayed_={};teamRowDisplayed_={};
 for(unsigned i=0;i<count_;++i)if(unsigned(rows_[i].team)<2)teamScores_[unsigned(rows_[i].team)].gain+=rows_[i].gain;
 sortBattleRows(false);
}

unsigned RaceResultsFlow::teamRowTarget(unsigned row)const{
 const auto& r=rows_[row];
 if(!battle_)return unsigned(r.previous)+r.gain;
 return totals()?unsigned(uint16_t(unsigned(r.previous)+r.gain)):unsigned(r.gain);
}

void RaceResultsFlow::sortBattleRows(bool totals){
 // CtrlRaceResultTeam comparator807ecd98: target score, local player,
 // target+previous score, then racer id. Apply independently of GP/VS.
 std::sort(rows_.begin(),rows_.begin()+count_,[totals](const RaceResultEntry& a,const RaceResultEntry& b){
  const unsigned ta=totals?uint16_t(unsigned(a.previous)+a.gain):a.gain;
  const unsigned tb=totals?uint16_t(unsigned(b.previous)+b.gain):b.gain;
  if(ta!=tb)return ta>tb;
  if(a.human!=b.human)return a.human;
  if(a.previous!=b.previous)return a.previous>b.previous;
  return a.racer<b.racer;
 });
}

void RaceResultsFlow::stepTeamCounts(){
 // 807ec990 accepts counts only while the parent page is active4.
 // Individual scores advance.5, team scores1, both from this page's
 // own initial value. Battle34 has set-win headers, no round-sum count.
 for(unsigned i=0;i<count_;++i){const unsigned before=teamRowShown(i);
  teamRowDisplayed_[i]=std::min(float(teamRowTarget(i)),teamRowDisplayed_[i]+.5f);
  pointsChanged_|=before!=teamRowShown(i);
 }
 if(!battle_||!totals())for(unsigned i=0;i<2;++i){const unsigned before=unsigned(teamDisplayed_[i]);
  teamDisplayed_[i]=std::min(float(teamScores_[i].total()),teamDisplayed_[i]+1.f);
  pointsChanged_|=before!=unsigned(teamDisplayed_[i]);
 }
}

void RaceResultsFlow::sortTotals(){
 if(battle_){
  sortBattleRows(true);
  for(unsigned i=0;i<count_;++i){counts_[i].fill(uint16_t(unsigned(rows_[i].previous)+rows_[i].gain),0);teamRowDisplayed_[i]=float(rows_[i].previous);}
  for(unsigned i=0;i<2;++i)teamDisplayed_[i]=float(battleWins_[i]);
  return;
 }
 // GPVSLeaderboardTotal::FillRows / ComparePlayers 0x8085A5F4: descending
 // total; on a tie a human ranks first, then this race's points decide.
 std::stable_sort(rows_.begin(),rows_.begin()+count_,[](const RaceResultEntry& a,const RaceResultEntry& b){
  const int ta=a.previous+a.gain,tb=b.previous+b.gain;
  if(ta!=tb)return ta>tb;
  if(a.human!=b.human)return a.human;
  return a.gain>b.gain;
 });
 for(unsigned i=0;i<count_;++i){counts_[i].fill(rows_[i].previous+rows_[i].gain,0);teamRowDisplayed_[i]=float(rows_[i].previous);}
 for(unsigned i=0;i<2;++i)teamDisplayed_[i]=float(teamScores_[i].previous);
}

uint32_t RaceResultsFlow::step(const RaceFlowInput& in){
 uint32_t events=EventNone;
 pointsChanged_=false;
 if(phase_==RaceFlowPhase::Racing||phase_==RaceFlowPhase::Done)return events;
 if(finishFrame_>=0)++finishFrame_;
 ++pageFrame_;
 // Battle BT parent delays45/50 plus the sixth row's14, then10 slide.
 const unsigned slideDone=battle_?unsigned((totals()?FlowTeamColumns[1]:FlowBattleColumns[1]).delay+FlowTeamRows[5].delay)+SlideFrames:55+SlideFrames;
 switch(phase_){
 case RaceFlowPhase::Finished:
  if(pageFrame_>=FinishCountFrames+FinishHoldFrames){enter(RaceFlowPhase::HudExit);events|=EventHudExit;}
  break;
 case RaceFlowPhase::HudExit:
  if(pageFrame_>=HudExitFrames){
   if(timeTrial_){enter(RaceFlowPhase::MenuIn);cursor_=0;events|=EventMenuShown;}
   else {enter(RaceFlowPhase::UpdateIn);timeout_=BoardTimeout;events|=EventFill|EventResultsShown;}
  }
  break;
 case RaceFlowPhase::UpdateIn:
  if(pageFrame_>=slideDone)phase_=RaceFlowPhase::Update; // frame counter continues
  break;
 case RaceFlowPhase::Update:{
  // Leaderboard::AfterControlUpdate 0x8083A634: counting delays the timeout;
  // A/B/START (0x8083A5E0) leave only once every row has finished counting.
  if(battle_)stepTeamCounts();
  else for(unsigned i=0;i<count_;++i){const int before=counts_[i].shown;counts_[i].update();pointsChanged_|=before!=counts_[i].shown;}
  const bool busy=counting();
  if(busy)++timeout_;
  else if(in.advance||pageFrame_>=timeout_)enter(RaceFlowPhase::UpdateOut);
  break;
 }
 case RaceFlowPhase::UpdateOut:
  if(pageFrame_>=slideDone){sortTotals();enter(RaceFlowPhase::TotalIn);timeout_=BoardTimeout;events|=EventTotalsShown;}
  break;
 case RaceFlowPhase::TotalIn:
  if(pageFrame_>=slideDone)phase_=RaceFlowPhase::Total;
  break;
 case RaceFlowPhase::Total:
  if(teams_){
   stepTeamCounts();
   if(teamCounting()){++timeout_;break;}
  }
  if(in.advance||pageFrame_>=timeout_)enter(RaceFlowPhase::TotalOut);
  break;
 case RaceFlowPhase::TotalOut:
  if(pageFrame_>=slideDone){enter(RaceFlowPhase::MenuIn);cursor_=0;events|=EventMenuShown;}
  break;
 case RaceFlowPhase::MenuIn:
  if(pageFrame_>=MenuSlideMaxDelay+SlideFrames)phase_=RaceFlowPhase::Menu;
  break;
 case RaceFlowPhase::Menu:
  if(in.accept){chosen_=int(cursor_);enter(RaceFlowPhase::Chosen);events|=EventOptionChosen;}
  break;
 case RaceFlowPhase::Chosen:
  if(pageFrame_>=ChooseFrames)enter(RaceFlowPhase::Leaving);
  break;
 case RaceFlowPhase::Leaving:
  if(pageFrame_>=LeaveFrames){enter(RaceFlowPhase::Done);events|=EventLeave;}
  break;
 default:break;
 }
 return events;
}
}
