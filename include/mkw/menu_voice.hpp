#pragma once
#include <mkw/menu_ui.hpp>
#include <mkw/menu_voice_assets.hpp>

namespace mkw {
// Presentation-only picker; never consumes the race's random sequence.
class MenuVoice {
public:
 static constexpr std::uint32_t None=0xffffffffu;
 std::uint32_t input(const MenuState& before,const MenuState& after,const UiInput& key){
  if(!key.accept||key.back||before.unavailable||after.unavailable)return None;
  unsigned event=0,driver=after.selection.driver;
  if(before.page==UiPage::Character&&after.page==UiPage::Vehicle){
   if(driver!=before.characterDriver(before.cursor))return None;
  }else if(before.page==UiPage::Vehicle&&after.page==UiPage::Drift)event=1;
  else return None;
  if(driver>=24)return None;
  const unsigned character=UiCharacters[driver].retailId;
  if(MenuVoiceRanges[character][event].first==914)event=0; // VO_DUMMY
  const auto range=MenuVoiceRanges[character][event];
  auto& picker=pickers_[character][event];
  const unsigned count=range.last-range.first+1;
  // The menu pickers' pick (fn_1_35704C, PAL 0x80867400), not the race one: two
  // variants alternate after a random first pick; more are random, never the last
  // one played. fn_1_357118 (0x808674CC) then keeps the pick as the last one.
  int index=0;
  if(count==2)index=picker.last<0?int(random(2)):picker.last==0?1:0;
  else if(count>2)do index=int(random(count));while(index==picker.last);
  if(count>=2)picker.last=index;
  return range.first+unsigned(index);
 }
private:
 struct Picker {int last=-1;};
 Picker pickers_[24][2]{};
 std::uint32_t rng_=0x9e3779b9u;
 unsigned random(unsigned n){rng_^=rng_<<13;rng_^=rng_>>17;rng_^=rng_<<5;return n?rng_%n:0;}
};
}
