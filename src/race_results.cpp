#include <mkw/race_results.hpp>
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

void RaceResultsFlow::finish(){
 if(phase_!=RaceFlowPhase::Racing)return;
 enter(RaceFlowPhase::Finished);finishFrame_=0;
}

float RaceResultsFlow::hudSlide()const{
 if(phase_<RaceFlowPhase::HudExit)return -1.f;
 if(phase_>RaceFlowPhase::HudExit)return 1.f;
 return std::min(1.f,float(pageFrame_)*.05f);
}

bool RaceResultsFlow::counting()const{
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

void RaceResultsFlow::fill(const RaceResultEntry* byPosition,unsigned count,bool finalRace){
 count_=std::min(count,12u);final_=finalRace;filled_=true;
 for(unsigned i=0;i<count_;++i){rows_[i]=byPosition[i];counts_[i].fill(rows_[i].previous,rows_[i].gain);}
}

void RaceResultsFlow::sortTotals(){
 // GPVSLeaderboardTotal::FillRows / ComparePlayers 0x8085A5F4: descending
 // total; on a tie a human ranks first, then this race's points decide.
 std::stable_sort(rows_.begin(),rows_.begin()+count_,[](const RaceResultEntry& a,const RaceResultEntry& b){
  const int ta=a.previous+a.gain,tb=b.previous+b.gain;
  if(ta!=tb)return ta>tb;
  if(a.human!=b.human)return a.human;
  return a.gain>b.gain;
 });
 for(unsigned i=0;i<count_;++i)counts_[i].fill(rows_[i].previous+rows_[i].gain,0);
}

uint32_t RaceResultsFlow::step(const RaceFlowInput& in){
 uint32_t events=EventNone;
 if(phase_==RaceFlowPhase::Racing||phase_==RaceFlowPhase::Done)return events;
 if(finishFrame_>=0)++finishFrame_;
 ++pageFrame_;
 const unsigned slideDone=55+SlideFrames; // rank12 delay + slide
 switch(phase_){
 case RaceFlowPhase::Finished:
  if(pageFrame_>=FinishCountFrames+FinishHoldFrames){enter(RaceFlowPhase::HudExit);events|=EventHudExit;}
  break;
 case RaceFlowPhase::HudExit:
  if(pageFrame_>=HudExitFrames){enter(RaceFlowPhase::UpdateIn);timeout_=BoardTimeout;events|=EventFill|EventResultsShown;}
  break;
 case RaceFlowPhase::UpdateIn:
  if(pageFrame_>=slideDone)phase_=RaceFlowPhase::Update; // frame counter continues
  break;
 case RaceFlowPhase::Update:{
  // Leaderboard::AfterControlUpdate 0x8083A634: counting delays the timeout;
  // A/B/START (0x8083A5E0) leave only once every row has finished counting.
  for(unsigned i=0;i<count_;++i)counts_[i].update();
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
  if(pageFrame_>=ChooseFrames){enter(RaceFlowPhase::Leaving);events|=EventLeaveStart;}
  break;
 case RaceFlowPhase::Leaving:
  if(pageFrame_>=LeaveFrames){enter(RaceFlowPhase::Done);events|=EventLeave;}
  break;
 default:break;
 }
 return events;
}
}
