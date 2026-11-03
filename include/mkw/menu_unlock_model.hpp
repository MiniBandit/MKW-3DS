#pragma once
#include <mkw/menu_ui.hpp>
#include <mkw/menu_unlock_texts.hpp>
#include <cstring>
namespace mkw {
// Model-only projection of the notification; never changes license settings.
inline MenuState unlockModelState(const MenuState& state){
 auto model=state;model.page=UiPage::Error;
 const UiUnlockText* notice=nullptr;
 for(const auto& row:UiUnlockTexts)if(row.condition==state.unlockCondition&&row.bit==state.unlockBit){notice=&row;break;}
 if(!notice)return model;
 int character=-1,vehicle=-1;
 if(!std::strcmp(notice->kind,"Driver")||!std::strcmp(notice->kind,"Mii"))character=notice->retailId;
 else if(!std::strcmp(notice->kind,"Vehicle")){
  vehicle=notice->retailId;
  // Unlock::SetModels USA80859764, jump table 808d67d0. The
  // representative rider is authored per reward, not the selected driver.
  constexpr unsigned riders[]={6,0,3,6,16,9,6,0,2,0,0,0,0,0,0,0,0,0,6,0,2,6,0,2,6,0,2};
  if(vehicle<9||vehicle>=36)return model;
  character=riders[vehicle-9];
 }
 // Mii rewards use the selected license identity through MenuModels.
 if(character<0)return model;
 unsigned driver=0;while(driver<UiCharacters.size()&&int(UiCharacters[driver].retailId)!=character)++driver;
 if(driver==UiCharacters.size())return model;
 model.selection.driver=driver;
 if(vehicle<0){
  model.page=UiPage::Character;
  model.cursor=unsigned(std::find(UiCharacterDrivers.begin(),UiCharacterDrivers.end(),driver)-UiCharacterDrivers.begin());
 }else{
  constexpr unsigned types[]={0,1,2,6,7,8,3,4,5,9,10,11};
  unsigned choice=0;while(choice<12&&int(types[choice]*3+UiCharacters[driver].weight)!=vehicle)++choice;
  if(choice==12)return model;
  model.page=UiPage::Vehicle;model.selection.vehicle=choice;
  model.selection.mode=UiMode::VS;model.selection.vsVehicles=2;model.cursor=choice;
 }
 return model;
}
}
