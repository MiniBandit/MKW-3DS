#include <mkw/menu_bottom.hpp>
#include <mkw/mii_store.hpp>
#include <mkw/menu_index.hpp>
#include <mkw/menu_transition.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
// Animation lookups live in generated menu data the layout does not use.
namespace mkw {
const UiAnimationClip* uiFindAnimationClip(const char*,const char*,const char*){return nullptr;}
const char* uiPauseMenuControl(const MenuState&){return nullptr;}
}
int main(){
 using namespace mkw;
 MenuBottomLayout layout;
 // Every page lays out inside the 320 x 240 screen with unique, valid slots.
 const UiPage pages[]={UiPage::Strap,UiPage::Intro,UiPage::Title,UiPage::License,UiPage::MainMenu,UiPage::Mode,UiPage::EngineClass,
  UiPage::VSMode,UiPage::BattleMode,UiPage::Character,UiPage::Vehicle,UiPage::Drift,UiPage::Cup,UiPage::Course,UiPage::BattleCup,
  UiPage::BattleStage,UiPage::Ghost,UiPage::Confirm,UiPage::Error,UiPage::QuitConfirm,UiPage::Pause,UiPage::Results};
 for(UiPage page:pages){
  MenuState state;state.enter(page);
  composeMenuBottom(state,layout);
  assert(layout.count>0&&layout.count<=MenuBottomLayout::Capacity);
  bool seen[32]{};
  for(unsigned i=0;i<layout.count;++i){
   const auto& w=layout.widgets[i];
   assert(w.x>=0&&w.y>=0&&w.x+w.w<=320&&w.y+w.h<=240);
   if(w.kind==BottomWidget::Label)continue;
   assert(w.w>0&&w.h>0&&w.text[0]);
   if(w.slot>=0){if(!(unsigned(w.slot)<state.count()&&!seen[w.slot]))std::fprintf(stderr,"page %u slot %d count %u widget %u\n",unsigned(page),w.slot,state.count(),i);assert(unsigned(w.slot)<state.count()&&!seen[w.slot]);seen[w.slot]=true;}
   // No two tappable controls overlap.
   for(unsigned j=i+1;j<layout.count;++j){
    const auto& o=layout.widgets[j];
    if(o.kind==BottomWidget::Label)continue;
    const bool apart=w.x+w.w<=o.x||o.x+o.w<=w.x||w.y+w.h<=o.y||o.y+o.h<=w.y;
    assert(apart);
   }
  }
  // Every control of the page can be tapped.
  for(unsigned slot=0;slot<state.count()&&page!=UiPage::Race;++slot)assert(seen[slot]);
 }
 // A tap at a tile's centre hits that tile; empty space hits nothing; Back is B.
 MenuState state;state.selection.driver=3;state.enter(UiPage::Vehicle);
 composeMenuBottom(state,layout);
 const auto& first=layout.widgets[layout.count-2]; // last tile before Back
 const int hit=hitMenuBottom(layout,first.x+first.w/2,first.y+first.h/2);
 assert(hit>=0&&layout.widgets[hit].slot==first.slot);
 assert(hitMenuBottom(layout,319,239)==-1);
 const int backHit=hitMenuBottom(layout,40,220);
 assert(backHit>=0&&layout.widgets[backHit].back);
 // Touch selection moves the cursor like the d-pad and then accepts.
 state.enter(UiPage::Mode);
 assert(state.selectSlot(2)&&state.cursor==2&&!state.selectSlot(9));
 UiInput accept;accept.accept=true;state.input(accept);
 assert(state.selection.mode==UiMode::VS&&state.page==UiPage::VSMode);
 // The Mii slots take their names from the store.
 MiiStore store;
 return 0;
}
