// A page must preload textures used by later fades, selection curves and
// border atlas frames. Exercise the production composer without a GPU.
#include <mkw/menu_ui.hpp>
#include <mkw/menu_transition.hpp>
#include <mkw/menu_assets.hpp>
#include <set>
#include <string>
#include <cstdio>
#include <cstring>
using namespace mkw;
int main(){
 const UiPage pages[]={UiPage::Title,UiPage::License,UiPage::MainMenu,UiPage::Mode,UiPage::EngineClass,UiPage::VSMode,UiPage::BattleMode,UiPage::Character,UiPage::Vehicle,UiPage::Drift,UiPage::Cup,UiPage::Course,UiPage::BattleCup,UiPage::BattleStage,UiPage::Ghost,UiPage::Confirm,UiPage::Pause,UiPage::QuitConfirm,UiPage::Error};
 UiRaceView race{};std::vector<UiQuad> q;unsigned checked=0,missing=0;
 for(unsigned mode=0;mode<4;++mode)for(const auto page:pages){
  MenuState initial;initial.selection.mode=UiMode(mode);initial.enter(page);
  for(unsigned cursor=0;cursor<initial.count();++cursor){
   auto state=initial;state.cursor=cursor;
   UiTransitionView preload;preload.preload=true;preload.selectedControl=uiSelectedMenuControl(state);
   composeMenu(state,race,q,preload);
   std::set<std::string> resident,reported;
   for(const auto& quad:q)if(quad.movie==~0u&&quad.vehicleIcon==~0u&&quad.licensePortrait==~0u)resident.insert(UiSprites[quad.sprite].path);
   for(unsigned frame=0;frame<600;++frame){
    composeMenu(state,race,q);
    for(const auto& quad:q)if(quad.movie==~0u&&quad.vehicleIcon==~0u&&quad.licensePortrait==~0u){
     ++checked;const auto path=UiSprites[quad.sprite].path;
     if(!resident.contains(path)&&reported.insert(path).second){++missing;std::fprintf(stderr,"missing page=%u cursor=%u age=%u %s\n",unsigned(page),cursor,frame,path);}
    }
    state.advance(1);
   }
  }
 }
 // Opening pause enters through the race controller, even though the race
 // does not navigate through MenuTransition::input to create this page.
 unsigned pauseEntrances=0;
 for(unsigned mode=0;mode<4;++mode){
  MenuState state;state.selection.mode=UiMode(mode);state.enter(UiPage::Pause);
  MenuTransition transition;transition.enterPage(state);
  if(!transition.active()||!transition.view().entering||transition.view().section)return 2;
  if(std::strcmp(uiSelectedMenuControl(state),uiPauseMenuControl(state)))return 3;
  composeMenu(state,race,q);unsigned steadyButtons=0;
  for(const auto& quad:q)if(quad.sprite<std::size(UiSprites)&&(!std::strcmp(UiSprites[quad.sprite].key,"button_select")||!std::strcmp(UiSprites[quad.sprite].key,"button_free")))++steadyButtons;
  composeMenu(state,race,q,transition.view());unsigned entranceButtons=0;
  for(const auto& quad:q)if(quad.sprite<std::size(UiSprites)&&(!std::strcmp(UiSprites[quad.sprite].key,"button_select")||!std::strcmp(UiSprites[quad.sprite].key,"button_free")))++entranceButtons;
  if(steadyButtons!=3||entranceButtons>=steadyButtons)return 4;
  transition.advance(state,30);if(transition.active())return 5;
  ++pauseEntrances;
 }
 std::printf("residency: %u later-frame quads, %u missing textures\n",checked,missing);
 std::printf("pause: %u original mode controllers enter progressively\n",pauseEntrances);
 return missing?1:0;
}
