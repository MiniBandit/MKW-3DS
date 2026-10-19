#pragma once
#include "mkw/menu_transition.hpp"
#include "mkw/menu_animations.hpp"
namespace mkw {
struct UiModelPlacement {float x,y,scale;};
// ModelRendererValuesHolder's widescreen one-player phase triples, plus the
// original Driver/VehicleModelControl procedural page motion. Later pages
// retain the already-positioned vehicle control behind their own controls.
// centered: the page's controls are on the lower screen, so the preview takes the
// middle of the top screen and grows (menu_bottom_ui.hpp).
inline UiModelPlacement uiMenuModelPlacement(UiPage page,const UiTransitionView& motion,bool centered=false) {
 const bool character=page==UiPage::Character,vehicle=page==UiPage::Vehicle;
 UiModelPlacement value=character?UiModelPlacement{-190,0,1}:
                              vehicle?UiModelPlacement{-120,50,1.2f}:UiModelPlacement{-160,60,1.2f};
 if(centered)value=character?UiModelPlacement{0,-5,1.4f}:vehicle?UiModelPlacement{0,35,1.3f}:UiModelPlacement{0,45,1.25f};
 if(!motion.active||motion.section||motion.modal||(!character&&!vehicle))return value;
 const float progress=uiPageTransitionProgress(motion.elapsed,0,motion.maxDelay,
                                               motion.entering,motion.back,motion.exitHold);
 const auto adjacent=motion.entering?motion.from:motion.to;
 if(centered&&(!(character&&adjacent==UiPage::Character)))value.x-=608*progress*(vehicle&&(adjacent==UiPage::Drift||adjacent==UiPage::Cup||adjacent==UiPage::BattleCup)?0:1);
 else if(centered){}
 else if(vehicle&&(adjacent==UiPage::Drift||adjacent==UiPage::Cup||adjacent==UiPage::BattleCup)) {
  const float phase=std::abs(progress);value.x=-120-40*phase;value.y=50+10*phase;
 }else if(!(character&&adjacent==UiPage::Character))value.x-=608*progress;
 return value;
}
}
