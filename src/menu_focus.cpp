#include <mkw/menu_focus.hpp>
#include <mkw/menu_ui.hpp>
#include <mkw/menu_transition.hpp>
#include <mkw/menu_animations.hpp>
#include <mkw/menu_index.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mkw {
namespace {
const char* phaseName(UiFocusPhase phase){
 switch(phase){case UiFocusPhase::Free:return "Free";case UiFocusPhase::FreeToSelect:return "FreeToSelect";
 case UiFocusPhase::Select:return "Select";case UiFocusPhase::SelectToFree:return "SelectToFree";}return "Free";
}
// MainMenuPage never selects its four waku frames (TopMenu*Waku): fn_1_3401CC
// (PAL 80850580) sets their Select group to Free, the button select handler
// fn_1_341168 (8085151c) only sets the instruction text, and the deselect
// handler fn_1_3411EC (808515a0) only reverses a FreeToSelect/Select nothing
// starts. So they stay in Free; the looping 10-frame Select (text_light) never runs.
bool selected(const MenuState&,unsigned cursor,unsigned slot){return slot<UiFocusButtonCount&&slot==cursor;}
void play(UiFocusControl& entry,const char* control,UiFocusPhase phase,float percent=0){
 entry.phase=phase;entry.clip=uiFindAnimationClip(control,"Select",phaseName(phase));
 entry.frame=entry.clip?entry.clip->duration*std::clamp(percent,0.f,1.f):0;
}
void reset(MenuState& state,unsigned cursor){
 state.focus={};state.focus.page=unsigned(state.page);state.focus.cursor=cursor;
 for(unsigned i=0;i<UiFocusControlCount;++i){
  auto& entry=state.focus.controls[i];const char* control=uiFocusControlName(state,i);const bool on=selected(state,cursor,i);
  play(entry,control,on?UiFocusPhase::Select:UiFocusPhase::Free);
  // PushButton_init (PAL 805bdbe0): SelectIn and OK start on their stop
  // clips; Loop starts paused at frame 0, and PushButton_calc resumes it only
  // for the default-selected button (selectDefault leaves SelectIn stopped).
  entry.selectIn=uiFindAnimationClip(control,"SelectIn","SelectStop");entry.selectInFrame=0;
  entry.loop=on?0.f:UiFocusFreeLoopFrame;
 }
}
void change(UiFocusControl& entry,const char* control,bool select){
 // PushButton::{HandleSelect,HandleDeselect}, USA805b6358/805b648c.
 // Interruptions preserve the completed fraction of the reverse clip.
 if(select){
  // PAL 805bdffc: Group_setAnimation(SelectIn group, SelectIn, frame 0).
  if(const auto* in=uiFindAnimationClip(control,"SelectIn","SelectIn")){entry.selectIn=in;entry.selectInFrame=0;}
  if(entry.phase==UiFocusPhase::Free)play(entry,control,UiFocusPhase::FreeToSelect);
  else if(entry.phase==UiFocusPhase::SelectToFree){
   const float percent=entry.clip&&entry.clip->duration>0?1.f-entry.frame/entry.clip->duration:0;
   play(entry,control,UiFocusPhase::FreeToSelect,percent);
  }
 }else{
  if(entry.phase==UiFocusPhase::Select)play(entry,control,UiFocusPhase::SelectToFree);
  else if(entry.phase==UiFocusPhase::FreeToSelect){
   const float percent=entry.clip&&entry.clip->duration>0?1.f-entry.frame/entry.clip->duration:0;
   play(entry,control,UiFocusPhase::SelectToFree,percent);
  }
 }
}
}

const char* uiFocusControlName(const MenuState& state,unsigned slot){
 if(slot>=UiFocusButtonCount){
  if(state.page!=UiPage::MainMenu||slot>=UiFocusControlCount)return "";
  constexpr const char* parents[]={"TopMenuSingleWaku","TopMenuMultiWaku","TopMenuWifiWaku","TopMenuChannelWaku"};
  return parents[slot-UiFocusMainParentBase];
 }
 if(slot>=state.count())return "";
 switch(state.page){
 case UiPage::License:return slot<2?"LicenseSelectT":"LicenseSelectB";
 case UiPage::MainMenu:return slot==0?"TopMenuSingle":slot<4?"TopMenuMulti":slot<6?"TopMenuWifi":"TopMenuChannel";
 case UiPage::Mode:return "SingleTop";
 case UiPage::EngineClass:return "ClassSelect2";
 case UiPage::VSMode:return "VSSelect";
 case UiPage::BattleMode:return "BattleSelect";
 case UiPage::Character:{constexpr const char* names[]={"CharacterSelect4_0","CharacterSelect4_1","CharacterSelect4_2","CharacterSelect4_3"};return names[std::min(slot/8,3u)];}
 case UiPage::Vehicle:return state.columns()==2?"MachineSelect2_6":"MachineSelect1_6";
 case UiPage::Drift:return "PadEasy";
 case UiPage::Cup:return "CupSelectCup";
 case UiPage::Course:return "CourseSelectCourse";
 case UiPage::BattleCup:return "BattleCupSelectCup";
 case UiPage::BattleStage:return "BattleStageSelectStage";
 case UiPage::Ghost:return "TimeAttackGhostList";
 case UiPage::Confirm:return "MenuEndConfirm";
 case UiPage::Pause:return uiPauseMenuControl(state);
 case UiPage::QuitConfirm:return "YesNoButton";
 case UiPage::Error:case UiPage::NoGhosts:return "ConfirmOK";
 default:return "";
 }
}

void uiFocusReset(MenuState& state){reset(state,state.cursor);}
void uiFocusSelect(MenuState& state,unsigned previousCursor){
 if(state.focus.page!=unsigned(state.page)||state.focus.cursor!=previousCursor)reset(state,previousCursor);
 if(previousCursor==state.cursor)return;
 for(unsigned slot=0;slot<UiFocusControlCount;++slot){
  const bool was=selected(state,previousCursor,slot),now=selected(state,state.cursor,slot);
  if(was!=now)change(state.focus.controls[slot],uiFocusControlName(state,slot),now);
 }
 state.focus.cursor=state.cursor;
}
void uiFocusAdvance(MenuState& state,unsigned ticks){
 if(state.focus.page!=unsigned(state.page)||state.focus.cursor!=state.cursor)uiFocusReset(state);
 for(unsigned slot=0;slot<UiFocusControlCount;++slot){
  auto& entry=state.focus.controls[slot];if(!entry.clip)continue;
  const char* control=uiFocusControlName(state,slot);
  const auto* loopClip=uiFindAnimationClip(control,"Loop","Loop");
  // AnimationGroup::Update8060b7c4 advances before testing the end. Chained
  // clips start at frame0; unlike a timeline, they discard frame overshoot.
  for(unsigned i=0;i<ticks;++i){
   if(const auto* clip=entry.clip;clip&&clip->duration>0&&clip->rate>0){
    entry.frame+=clip->rate;
    if(entry.frame>=clip->duration){
     if(clip->loop)entry.frame=std::fmod(entry.frame,clip->duration);
     else if(*clip->next){
      entry.phase=!std::strcmp(clip->next,"Select")?UiFocusPhase::Select:UiFocusPhase::Free;
      entry.clip=uiFindAnimationClip(control,"Select",clip->next);entry.frame=0;
     }else entry.frame=clip->duration;
    }
   }
   if(const auto* clip=entry.selectIn;clip&&clip->duration>0&&clip->rate>0){
    entry.selectInFrame+=clip->rate;
    if(entry.selectInFrame>=clip->duration){
     if(clip->loop)entry.selectInFrame=std::fmod(entry.selectInFrame,clip->duration);
     else if(const auto* next=*clip->next?uiFindAnimationClip(control,"SelectIn",clip->next):nullptr){entry.selectIn=next;entry.selectInFrame=0;}
     else entry.selectInFrame=clip->duration;
    }
   }
   // PushButton_calc runs after the animator: a Free button's Loop is reset
   // to frame 30 and paused; otherwise it plays on from where it was.
   if(entry.phase==UiFocusPhase::Free)entry.loop=UiFocusFreeLoopFrame;
   else{
    entry.loop+=loopClip?loopClip->rate:1.f;
    if(loopClip&&loopClip->duration>0&&entry.loop>=loopClip->duration)entry.loop=std::fmod(entry.loop,loopClip->duration);
   }
  }
 }
}
UiFocusView uiFocusAnimation(const MenuState& state,unsigned slot){
 UiFocusView view;view.control=uiFocusControlName(state,slot);if(slot>=UiFocusControlCount)return view;
 view.selected=selected(state,state.cursor,slot);
 if(state.focus.page==unsigned(state.page)&&state.focus.cursor==state.cursor){
  const auto& entry=state.focus.controls[slot];view.clip=entry.clip;view.phase=entry.phase;view.frame=entry.frame;
  view.selectIn=entry.selectIn;view.selectInFrame=entry.selectInFrame;view.loop=entry.loop;
 }else{
  // Explicit menu fixtures may set cursor directly. Such a snapshot denotes
  // a settled selection, never a fabricated navigation event.
  view.phase=view.selected?UiFocusPhase::Select:UiFocusPhase::Free;
  view.clip=uiFindAnimationClip(view.control,"Select",phaseName(view.phase));
  view.selectIn=uiFindAnimationClip(view.control,"SelectIn","SelectStop");
  view.loop=view.selected?float(state.focusAge):UiFocusFreeLoopFrame;
 }
 if(view.clip&&view.clip->reverse)view.frame=view.clip->duration-view.frame;
 if(view.selectIn&&view.selectIn->reverse)view.selectInFrame=view.selectIn->duration-view.selectInFrame;
 return view;
}
float UiFocusView::value(const char* element,const char* kind,unsigned target,float fallback,unsigned index)const{
 return clip?uiAnimationValue(*clip,element,kind,target,frame,fallback,index):fallback;
}
}
