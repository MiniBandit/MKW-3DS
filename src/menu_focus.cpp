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
unsigned mainGroup(unsigned cursor){return cursor==0?0:cursor<4?1:cursor<6?2:3;}
bool selected(const MenuState& state,unsigned cursor,unsigned slot){
 return slot<UiFocusButtonCount?slot==cursor:
     state.page==UiPage::MainMenu&&slot==UiFocusMainParentBase+mainGroup(cursor);
}
void play(UiFocusControl& entry,const char* control,UiFocusPhase phase,float percent=0){
 entry.phase=phase;entry.clip=uiFindAnimationClip(control,"Select",phaseName(phase));
 entry.frame=entry.clip?entry.clip->duration*std::clamp(percent,0.f,1.f):0;
}
void reset(MenuState& state,unsigned cursor){
 state.focus={};state.focus.page=unsigned(state.page);state.focus.cursor=cursor;
 for(unsigned i=0;i<UiFocusControlCount;++i)
  play(state.focus.controls[i],uiFocusControlName(state,i),selected(state,cursor,i)?UiFocusPhase::Select:UiFocusPhase::Free);
}
void change(UiFocusControl& entry,const char* control,bool select){
 // PushButton::{HandleSelect,HandleDeselect}, USA805b6358/805b648c.
 // Interruptions preserve the completed fraction of the reverse clip.
 if(select){
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
 case UiPage::Error:return "ConfirmOK";
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
  // AnimationGroup::Update8060b7c4 advances before testing the end. Chained
  // clips start at frame0; unlike a timeline, they discard frame overshoot.
  for(unsigned i=0;i<ticks;++i){
   const auto* clip=entry.clip;if(!clip||clip->duration<=0||clip->rate<=0)break;
   entry.frame+=clip->rate;
   if(entry.frame<clip->duration)continue;
   // The main menu's group frames (TopMenu*Waku) author Select as a 10-frame
   // light burst: looped it flashed six times a second, so it plays once and holds.
   if(clip->loop&&slot>=UiFocusButtonCount&&entry.phase==UiFocusPhase::Select)entry.frame=clip->duration;
   else if(clip->loop)entry.frame=std::fmod(entry.frame,clip->duration);
   else if(*clip->next){
    entry.phase=!std::strcmp(clip->next,"Select")?UiFocusPhase::Select:UiFocusPhase::Free;
    entry.clip=uiFindAnimationClip(uiFocusControlName(state,slot),"Select",clip->next);entry.frame=0;
   }else entry.frame=clip->duration;
  }
 }
}
UiFocusView uiFocusAnimation(const MenuState& state,unsigned slot){
 UiFocusView view;view.control=uiFocusControlName(state,slot);if(slot>=UiFocusControlCount)return view;
 view.selected=selected(state,state.cursor,slot);
 if(state.focus.page==unsigned(state.page)&&state.focus.cursor==state.cursor){
  const auto& entry=state.focus.controls[slot];view.clip=entry.clip;view.phase=entry.phase;view.frame=entry.frame;
 }else{
  // Explicit menu fixtures may set cursor directly. Such a snapshot denotes
  // a settled selection, never a fabricated navigation event.
  view.phase=view.selected?UiFocusPhase::Select:UiFocusPhase::Free;
  view.clip=uiFindAnimationClip(view.control,"Select",phaseName(view.phase));
 }
 if(view.clip&&view.clip->reverse)view.frame=view.clip->duration-view.frame;
 return view;
}
float UiFocusView::value(const char* element,const char* kind,unsigned target,float fallback,unsigned index)const{
 return clip?uiAnimationValue(*clip,element,kind,target,frame,fallback,index):fallback;
}
}
