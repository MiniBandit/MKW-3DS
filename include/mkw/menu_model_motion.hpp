#pragma once
#include "mkw/menu_transition.hpp"
#include "mkw/menu_animations.hpp"
namespace mkw {
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
// The shown menu kart's rotation (+0x28) one frame on, toward `target`
// (fn_1_3223A8, PAL 8083275c): a twelfth of the forward gap, back to 0 past
// 719.5. Constants 808aca68 (720.99988), 808aca6c (12), 808aca70 (719.5).
inline float uiKartTurn(float rotation,unsigned target){
 const int gap=int(720.9998779296875f+float(target)-rotation)%720;
 rotation+=float(gap)/12.f;
 return rotation>719.5f?0.f:rotation;
}
// Its Euler Y in radians: (90+rotation)*2*pi/720 (808aca74, 808aca78,
// 808aca7c, fn_1_195EF0 with 808aca80), so rotation 0 faces 45 degrees.
inline float uiKartYaw(float rotation){return (90.f+rotation)*2.f*3.14159274f/720.f;}
}
