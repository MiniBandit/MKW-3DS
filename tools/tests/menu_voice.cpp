#include <mkw/menu_voice.hpp>
#include <mkw/menu_transition.hpp>
#include <cassert>
#include <set>
#include <cstdio>
using namespace mkw;
int main(){
 MenuVoice voices;UiInput accept;accept.accept=true;
 unsigned selections=0;
 for(unsigned cursor=0;cursor<24;++cursor){
  MenuState before;before.enter(UiPage::Character);before.cursor=cursor;
  auto after=before;after.input(accept);assert(after.page==UiPage::Vehicle);
  const unsigned driver=after.selection.driver,retail=UiCharacters[driver].retailId;
  const auto range=MenuVoiceRanges[retail][0];
  std::set<unsigned> cycle;unsigned last=MenuVoice::None;
  for(unsigned j=0;j<12;++j){
   const auto id=voices.input(before,after,accept);assert(id>=range.first&&id<=range.last);
   assert(last==MenuVoice::None||id!=last);last=id;cycle.insert(id);
   if(j%3==2){assert(cycle.size()==3);cycle.clear();}
  }
  assert(voices.input(before,after,{})==MenuVoice::None);
  auto hovered=before;hovered.cursor=(cursor+1)%24;
  assert(voices.input(before,hovered,accept)==MenuVoice::None);
  auto blocked=after;blocked.unavailable=true;
  assert(voices.input(before,blocked,accept)==MenuVoice::None);
  auto back=accept;back.back=true;assert(voices.input(before,after,back)==MenuVoice::None);
  MenuTransition transition;auto logical=before;assert(transition.input(logical,accept)==UiAction::None);
  const auto view=transition.view();assert(view.active&&!view.entering&&!view.back&&view.to==UiPage::Vehicle);
  assert(view.exitHold>=MenuSelectAnimationFrames[driver]);
  auto unchanged=logical;assert(transition.input(logical,accept)==UiAction::None);assert(logical.page==unchanged.page);
  assert(uiMenuExitHold(before,true)==0);
  auto kartBefore=after;auto kartAfter=after;kartAfter.input(accept);assert(kartAfter.page==UiPage::Drift);
  auto kartRange=MenuVoiceRanges[retail][1];if(kartRange.first==914)kartRange=range;
  const auto kartVoice=voices.input(kartBefore,kartAfter,accept);assert(kartVoice>=kartRange.first&&kartVoice<=kartRange.last);
  auto returned=kartBefore;returned.enter(UiPage::Character);
  assert(voices.input(kartBefore,returned,back)==MenuVoice::None);
  ++selections;
 }
 for(unsigned cursor=24;cursor<26;++cursor){MenuState before;before.enter(UiPage::Character);before.cursor=cursor;auto after=before;after.input(accept);assert(after.unavailable);assert(voices.input(before,after,accept)==MenuVoice::None);}
 assert(MenuVoiceRanges[UiCharacters[0].retailId][0].first==923); // Mario
 assert(MenuVoiceRanges[UiCharacters[1].retailId][0].first==926); // Luigi
 assert(MenuVoiceRanges[UiCharacters[3].retailId][0].first==929); // Yoshi
 std::printf("Menu voice/confirmation scenarios passed for %u playable characters; Mii, hover, back and blocked inputs stay silent.\n",selections);
}
