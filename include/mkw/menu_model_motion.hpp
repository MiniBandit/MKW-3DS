#pragma once
#include "mkw/menu_transition.hpp"
#include "mkw/menu_animations.hpp"
namespace mkw {
inline bool uiTimeTrialRecordBackdrop(const MenuState& state){
 return state.selection.mode==UiMode::TimeTrials&&
        (state.page==UiPage::Ghost||state.page==UiPage::Confirm);
}
// Random/ordered courses skip Cup, but keep Drift's retained preview while
// the StartRace modal opens/closes. This uses the existing ten-frame page clock.
inline bool uiRandomCourseConfirmPreview(const MenuState& state){
 return state.page==UiPage::Confirm&&
   ((state.selection.mode==UiMode::VS&&state.selection.vsCourses)||
    (state.selection.mode==UiMode::Battle&&state.selection.battleCourses));
}
inline bool uiRandomCoursePreviewMotion(const UiTransitionView& motion){
 return motion.underlay&&motion.underlay->page==UiPage::Drift&&
   ((motion.from==UiPage::Drift&&motion.to==UiPage::Confirm)||
    (motion.from==UiPage::Confirm&&motion.to==UiPage::Drift));
}
inline int uiBackdropModelIndex(const MenuState& state){
 switch(state.page){
 case UiPage::Mode:return int(std::min(state.cursor,3u));
 case UiPage::EngineClass:return 0;
 case UiPage::VSMode:case UiPage::VSRules:return 2;
 case UiPage::TeamConfirm:return state.selection.mode==UiMode::Battle?-1:2;
 case UiPage::BattleMode:return 3;
 case UiPage::Character:case UiPage::Vehicle:case UiPage::Drift:case UiPage::DriftHelp:
 case UiPage::Cup:case UiPage::Course:case UiPage::BattleCup:
 case UiPage::BattleStage:case UiPage::Ghost:case UiPage::Confirm:return int(state.selection.mode);
 default:return -1;
 }
}
struct UiModelPlacement {float x,y,scale;};
// ModelRendererValuesHolder's widescreen one-player phase triples, plus the
// original Driver/VehicleModelControl procedural page motion. Later pages
// retain the already-positioned vehicle control behind their own controls.
inline UiModelPlacement uiMenuModelPlacement(UiPage page,const UiTransitionView& motion) {
 const bool character=page==UiPage::Character,vehicle=page==UiPage::Vehicle;
 UiModelPlacement value=character?UiModelPlacement{-190,0,1}:
                              vehicle?UiModelPlacement{-120,50,1.2f}:UiModelPlacement{-160,60,1.2f};
 if(!motion.active||motion.section||motion.modal||(!character&&!vehicle))return value;
 const float progress=uiPageTransitionProgress(motion.elapsed,0,motion.maxDelay,
                                               motion.entering,motion.back,motion.exitHold);
 const auto adjacent=motion.entering?motion.from:motion.to;
 if(vehicle&&(adjacent==UiPage::Drift||adjacent==UiPage::Cup||adjacent==UiPage::BattleCup)) {
  const float phase=std::abs(progress);value.x=-120-40*phase;value.y=50+10*phase;
 }else if(!(character&&adjacent==UiPage::Character))value.x-=608*progress;
 return value;
}
// LCD placement shares retail timing, with exact adjacent-page endpoints.
inline UiModelPlacement uiTopMenuModelPlacement(UiPage page,const UiTransitionView& motion) {
 if(uiRandomCoursePreviewMotion(motion)){
  const float progress=std::clamp(std::max(0.f,motion.elapsed)/UiPageMotionFrames,0.f,1.f);
  const float hidden=motion.from==UiPage::Confirm?1.f-progress:progress;
  return {200.f-520.f*hidden,86.f,1.5f};
 }
 const auto rest=uiMenuModelPlacement(page,{}),animated=uiMenuModelPlacement(page,motion);
 const bool character=page==UiPage::Character,vehicle=page==UiPage::Vehicle;
 UiModelPlacement value=character?UiModelPlacement{200,108,1.2f}:
                              vehicle?UiModelPlacement{270,86,1.5f}:UiModelPlacement{200,86,1.5f};
 const auto adjacent=motion.entering?motion.from:motion.to;
 if(vehicle&&(adjacent==UiPage::Drift||adjacent==UiPage::Cup||adjacent==UiPage::BattleCup)) {
  const float phase=std::clamp((rest.x-animated.x)/40.f,0.f,1.f);
  value.x-=70*phase;
 }else {
  value.x+=(animated.x-rest.x)*(400.f/608.f);
  value.y-=(animated.y-rest.y)*(240.f/456.f);
 }
 if(page==UiPage::Drift&&motion.active&&!motion.section&&!motion.modal&&
    (adjacent==UiPage::Cup||adjacent==UiPage::BattleCup)){
  const float progress=std::abs(uiPageTransitionProgress(motion.elapsed,0,motion.maxDelay,motion.entering,motion.back,motion.exitHold));
  value.x-=520.f*progress;
 }
 value.scale*=animated.scale/rest.scale;
 return value;
}

// Reflow adaptation: all four decorative mode models follow the same retail
// ten-frame page curve/direction and confirmation hold used by the controls.
// Preserve their accepted LCD anchors, including GP's 16px lower placement.
inline float uiTimeTrialRecordSlide(const UiTransitionView& motion){
 if((motion.from==UiPage::Ghost&&motion.to==UiPage::Confirm)||
    (motion.from==UiPage::Confirm&&motion.to==UiPage::Ghost))return 0.f;
 if(!motion.active||motion.modal||
    (motion.from!=UiPage::Ghost&&motion.to!=UiPage::Ghost))return 0.f;
 return -640.f*uiPageTransitionProgress(motion.elapsed,0,motion.maxDelay,
                                      motion.entering,motion.back,motion.exitHold);
}
inline UiModelPlacement uiBackdropModelPlacement(unsigned model,const UiTransitionView& motion){
 UiModelPlacement value{200.f,126.f+(model==0?16.f:0.f),1.f};
 if(model==1&&(motion.from==UiPage::Ghost||motion.to==UiPage::Ghost)){value.x+=uiTimeTrialRecordSlide(motion);return value;}
 // The decorative object is replaced by the character preview at this
 // boundary even though its retained mode ID stays the same. Other pages
 // sharing the mode's trophy/flag keep the object in place.
 const auto decorative=[](UiPage p){
  return p==UiPage::Mode||p==UiPage::EngineClass||p==UiPage::VSMode||
         p==UiPage::VSRules||p==UiPage::TeamConfirm||p==UiPage::BattleMode;
 };
 const bool characterBoundary=(motion.to==UiPage::Character&&decorative(motion.from))||
                              (motion.from==UiPage::Character&&decorative(motion.to));
 if(!motion.active||motion.modal||!characterBoundary)return value;
 const float progress=uiPageTransitionProgress(motion.elapsed,0,motion.maxDelay,
                                              motion.entering,motion.back,motion.exitHold);
 value.x-=640.f*progress;
 return value;
}

}
