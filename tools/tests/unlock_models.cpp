#include <mkw/menu_unlock_model.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
int main(){
 MenuState flags;
 flags.licenseUnlocks[0][0]=1u<<12;
 assert(flags.driverUnlocked(24)&&!flags.driverUnlocked(25));
 flags.licenseUnlocks[0][0]=1u<<13;
 assert(!flags.driverUnlocked(24)&&flags.driverUnlocked(25));
 unsigned drivers=0,vehicles=0,miis=0;
 for(const auto& row:UiUnlockTexts){
  MenuState s;s.selection.license=3;s.selection.driver=10;s.selection.vehicle=11;s.enter(UiPage::Unlock);s.unlockCondition=row.condition;s.unlockBit=row.bit;
  auto old=s.selection;auto model=unlockModelState(s);assert(s.selection==old&&s.page==UiPage::Unlock&&model.selection.license==3);
  if(!std::strcmp(row.kind,"Driver")){assert(model.page==UiPage::Character);assert(int(UiCharacters[model.selection.driver].retailId)==row.retailId);assert(UiCharacterDrivers[model.cursor]==model.selection.driver);++drivers;}
  else if(!std::strcmp(row.kind,"Vehicle")){
   assert(model.page==UiPage::Vehicle);constexpr unsigned types[]={0,1,2,6,7,8,3,4,5,9,10,11};
   assert(int(types[model.selection.vehicle]*3+UiCharacters[model.selection.driver].weight)==row.retailId);
   assert(model.cursor==model.selection.vehicle&&model.selection.mode==UiMode::VS);++vehicles;
  }else if(!std::strcmp(row.kind,"Mii")){assert(model.page==UiPage::Character);assert(model.selection.driver==24u+unsigned(row.retailId-24)/2);assert(UiCharacterDrivers[model.cursor]==model.selection.driver);++miis;}
  else assert(model.page==UiPage::Error);
 }
 assert(drivers&&vehicles&&miis==5);
 puts("Unlock model projection: all retail character/vehicle rewards, roster mapping, matching weight and immutable license selection passed; all five Mii notices retain the license identity slot");
}
