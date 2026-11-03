#include <mkw/menu_ui.hpp>
#include <mkw/menu_transition.hpp>
#include <mkw/menu_assets.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace mkw;
int main(){
 // The cook emits white originals before styled aliases. Live material
 // animation must use the same base image at preload and during selection.
 unsigned styled=0;
 for(const auto& sprite:UiSprites)if(sprite.style&&*sprite.text){
  bool white=false;
  for(const auto& candidate:UiSprites)
   if(!candidate.style&&candidate.font==sprite.font&&!std::strcmp(candidate.text,sprite.text)){white=true;break;}
  assert(white);++styled;
 }
 const UiPage pages[]={UiPage::Title,UiPage::License,UiPage::MainMenu,UiPage::Mode,
  UiPage::EngineClass,UiPage::VSMode,UiPage::Character,UiPage::Vehicle,UiPage::Drift,
  UiPage::Cup,UiPage::Course,UiPage::Confirm,UiPage::Pause,UiPage::QuitConfirm};
 unsigned materials=0;
 for(auto page:pages){
  MenuState state;state.enter(page);UiRaceView race{};
  for(bool preload:{true,false}){
   UiTransitionView transition{};transition.preload=preload;
   std::vector<UiQuad> quads;composeMenu(state,race,quads,transition);
   for(const auto& q:quads)if(q.fontRemap){
    assert(q.sprite<std::size(UiSprites));assert(UiSprites[q.sprite].style==0);++materials;
   }
  }
 }
 assert(styled&&materials);std::printf("PASS styled=%u live-material-quads=%u\n",styled,materials);
}
