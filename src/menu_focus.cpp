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
 if(state.page==UiPage::Cup&&slot>=UiFocusMainParentBase)return int(slot-UiFocusMainParentBase)==state.previewCourse;
 return slot<UiFocusButtonCount?slot==cursor:
     state.page==UiPage::MainMenu&&cursor<UiMainMenuControls.size()&&slot==UiFocusMainParentBase+mainGroup(cursor);
}
void play(UiFocusControl& entry,const char* control,UiFocusPhase phase,float percent=0){
 entry.phase=phase;entry.clip=uiFindAnimationClip(control,"Select",phaseName(phase));
 entry.frame=entry.clip?entry.clip->duration*std::clamp(percent,0.f,1.f):0;
}
void reset(MenuState& state,unsigned cursor){
 state.focus={};state.focus.page=unsigned(state.page);state.focus.cursor=cursor;
 for(unsigned i=0;i<UiFocusControlCount;++i){
  play(state.focus.controls[i],uiFocusControlName(state,i),selected(state,cursor,i)?UiFocusPhase::Select:UiFocusPhase::Free);
  state.focus.controls[i].selectInClip=uiFindAnimationClip(uiFocusControlName(state,i),"SelectIn","SelectStop");
 }
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
  if(state.page==UiPage::Cup&&slot<UiFocusControlCount)return "CupSelectCourse";
  if(state.page!=UiPage::MainMenu||slot>=UiFocusControlCount)return "";
  constexpr const char* parents[]={"TopMenuSingleWaku","TopMenuMultiWaku","TopMenuWifiWaku","TopMenuChannelWaku"};
  return parents[slot-UiFocusMainParentBase];
 }
 if(slot>=state.count())return "";
 switch(state.page){
 case UiPage::License:if(slot==4)return "LicenseSelectS";return slot<2?"LicenseSelectT":"LicenseSelectB";
 case UiPage::MainMenu:if(slot==7)return "FileAdmin";return slot==0?"TopMenuSingle":slot<4?"TopMenuMulti":slot<6?"TopMenuWifi":"TopMenuChannel";
 case UiPage::Settings:return "OptionTopButton";
 case UiPage::LicenseSettings:return "LicenseManagementButton";
 case UiPage::RecordsGeneral:case UiPage::RecordsFavorites:case UiPage::RecordsOther:return "NextRecordButton";
 case UiPage::Mode:return "SingleTop";
 case UiPage::EngineClass:return "ClassSelect2";
 case UiPage::VSMode:return slot==2?"RuleSetting":"VSSelect";
 case UiPage::TeamConfirm:return "TeamConfirmOK";
 case UiPage::VSRules:return slot==6?"VSSetting":slot==5?"VSSettingUpDownBase":"VSSettingRadioBase";
 case UiPage::BattleMode:return slot==2?"RuleSetting":"BattleSelect";
 case UiPage::BattleRules:return slot==5?"BattleSetting":slot==4?"BattleSettingUpDownBase":"BattleSettingRadioBase";
 case UiPage::Character:{constexpr const char* names[]={"CharacterSelect4_0","CharacterSelect4_1","CharacterSelect4_2","CharacterSelect4_3"};return names[std::min(slot/8,3u)];}
 case UiPage::Vehicle:return state.columns()==2?"MachineSelect2_6":"MachineSelect1_6";
 case UiPage::Drift:return slot==2?"DriftHelp":"PadEasy";
 case UiPage::Cup:return "CupSelectCup";
 case UiPage::Course:return "CourseSelectCourse";
 case UiPage::BattleCup:return "BattleCupSelectCup";
 case UiPage::BattleStage:return "BattleStageSelectStage";
 case UiPage::Ghost:return "TimeAttackGhostList";
 case UiPage::DriftHelp:return "OKKey";
 case UiPage::Confirm:return "MenuEndConfirm";
 case UiPage::Pause:return "OptionTopButton";
 case UiPage::QuitConfirm:return "OptionTopButton";
 case UiPage::EraseLicense:return "YesNoButton";
 case UiPage::Unlock:return "NextButton";
 case UiPage::Error:return "ConfirmOK";
 default:return "";
 }
}

void uiFocusReset(MenuState& state){state.previewCourse=-1;reset(state,state.cursor);}
void uiFocusMovieChapter(MenuState& state,int chapter){
 if(state.page!=UiPage::Cup)return;
 if(state.focus.page!=unsigned(state.page)||state.focus.cursor!=state.cursor)uiFocusReset(state);
 if(chapter==state.previewCourse)return;
 state.previewCourse=chapter;
 for(unsigned i=0;i<4;++i)change(state.focus.controls[UiFocusMainParentBase+i],"CupSelectCourse",int(i)==chapter);
}
void uiFocusSelect(MenuState& state,unsigned previousCursor){
 if(state.focus.page!=unsigned(state.page)||state.focus.cursor!=previousCursor)reset(state,previousCursor);
 if(previousCursor==state.cursor)return;
 for(unsigned slot=0;slot<UiFocusControlCount;++slot){
  const bool was=selected(state,previousCursor,slot),now=selected(state,state.cursor,slot);
  if(was!=now){
   const char* control=uiFocusControlName(state,slot);
   change(state.focus.controls[slot],control,now);
   if(now){
    // HandleSelect805b6404 starts the separate group2 pulse at frame0.
    state.focus.controls[slot].selectInClip=uiFindAnimationClip(control,"SelectIn","SelectIn");
    state.focus.controls[slot].selectInFrame=0;
   }
  }
 }
 state.focus.cursor=state.cursor;
}
void uiFocusAdvance(MenuState& state,unsigned ticks){
 if(state.focus.page!=unsigned(state.page)||state.focus.cursor!=state.cursor)uiFocusReset(state);
 if(state.focus.mainConfirmSlot<7){
  state.focus.mainConfirmFrame+=ticks;
  const auto* clip=uiFindAnimationClip(uiFocusControlName(state,state.focus.mainConfirmSlot),"OK","OK");
  if(!clip||state.focus.mainConfirmFrame*clip->rate>=clip->duration)state.focus.mainConfirmSlot=~0u;
 }
 for(unsigned slot=0;slot<UiFocusControlCount;++slot){
  auto& entry=state.focus.controls[slot];
  for(unsigned tick=0;tick<ticks&&entry.selectInClip;++tick){
   const auto* clip=entry.selectInClip;
   if(clip->rate<=0||clip->duration<=0)break;
   entry.selectInFrame+=clip->rate;
   if(entry.selectInFrame<clip->duration)continue;
   if(clip->loop)entry.selectInFrame=std::fmod(entry.selectInFrame,clip->duration);
   else if(*clip->next){entry.selectInClip=uiFindAnimationClip(uiFocusControlName(state,slot),"SelectIn",clip->next);entry.selectInFrame=0;}
   else entry.selectInFrame=clip->duration;
  }
  if(!entry.clip)continue;
  // AnimationGroup::Update8060b7c4 advances before testing the end. Chained
  // clips start at frame0; unlike a timeline, they discard frame overshoot.
  for(unsigned i=0;i<ticks;++i){
   const auto* clip=entry.clip;if(!clip||clip->duration<=0||clip->rate<=0)break;
   entry.frame+=clip->rate;
   if(entry.frame<clip->duration)continue;
   if(clip->loop)entry.frame=std::fmod(entry.frame,clip->duration);
   else if(*clip->next){
    entry.phase=!std::strcmp(clip->next,"Select")?UiFocusPhase::Select:UiFocusPhase::Free;
    entry.clip=uiFindAnimationClip(uiFocusControlName(state,slot),"Select",clip->next);entry.frame=0;
   }else entry.frame=clip->duration;
  }
 }
}

void uiFocusMainConfirm(MenuState& state,unsigned slot){
 if(state.page!=UiPage::MainMenu||slot>=7)return;
 if(state.focus.page!=unsigned(state.page)||state.focus.cursor!=state.cursor)uiFocusReset(state);
 state.focus.mainConfirmSlot=slot;state.focus.mainConfirmFrame=0;
}
UiFocusView uiFocusAnimation(const MenuState& state,unsigned slot){
 UiFocusView view;view.control=uiFocusControlName(state,slot);if(slot>=UiFocusControlCount)return view;
 view.selected=selected(state,state.cursor,slot);
 if(state.focus.page==unsigned(state.page)&&state.focus.cursor==state.cursor){
  const auto& entry=state.focus.controls[slot];view.clip=entry.clip;view.phase=entry.phase;view.frame=entry.frame;
  view.selectInClip=entry.selectInClip;view.selectInFrame=entry.selectInFrame;
 }else{
  // Explicit menu fixtures may set cursor directly. Such a snapshot denotes
  // a settled selection, never a fabricated navigation event.
  view.phase=view.selected?UiFocusPhase::Select:UiFocusPhase::Free;
  view.clip=uiFindAnimationClip(view.control,"Select",phaseName(view.phase));
 }
 if(view.clip&&view.clip->reverse)view.frame=view.clip->duration-view.frame;
 if(view.selectInClip&&view.selectInClip->reverse)view.selectInFrame=view.selectInClip->duration-view.selectInFrame;
 return view;
}
float UiFocusView::value(const char* element,const char* kind,unsigned target,float fallback,unsigned index)const{
 return clip?uiAnimationValue(*clip,element,kind,target,frame,fallback,index):fallback;
}
}
