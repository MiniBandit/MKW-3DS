#pragma once
#include <mkw/menu_ui.hpp>
#include <mkw/menu_voice_assets.hpp>
#include <mkw/mii_voice.hpp>

namespace mkw {
// Presentation-only picker; never consumes the race's random sequence.
class MenuVoice {
public:
 static constexpr std::uint32_t None=0xffffffffu;
 std::uint32_t input(const MenuState& before,const MenuState& after,const UiInput& key){
  if(!key.accept||key.back||before.unavailable||after.unavailable)return None;
  unsigned event=0,driver=after.selection.driver;
  if(before.page==UiPage::Character&&(after.page==UiPage::Vehicle||after.page==UiPage::TeamConfirm)){
   if(driver!=before.characterDriver(before.cursor))return None;
  }else if(before.page==UiPage::Vehicle&&after.page==UiPage::Drift)event=1;
  else return None;
  if(driver>=26||(driver>=24&&!after.selection.miiSelected))return None;
  const unsigned character=driver<24?UiCharacters[driver].retailId:24+std::min(after.selectedMiiVoice,23u);
  if(character>=24)event=0; // Retail Mii uses one picker for both confirmations.
  else if(MenuVoiceRanges[character][event].first==914)event=0; // VO_DUMMY
  const auto range=character<24?MenuVoiceRanges[character][event]:MenuVoiceRange{std::uint16_t(MiiMenuVoiceRanges[character-24][0]),std::uint16_t(MiiMenuVoiceRanges[character-24][1])};
  auto& picker=pickers_[character][event];
  const unsigned count=range.last-range.first+1;
  unsigned index=0;
  if(count==2)index=random(2);
  else if(count>2){
   unsigned available[32],n=0;
   for(unsigned i=0;i<count;++i)if(!(picker.mask&(1u<<i))&&int(i)!=picker.last)available[n++]=i;
   // The last unplayed sample is always eligible; a completed cycle resets
   // below while retaining last, preventing a repeat across cycle boundaries.
   if(!n)for(unsigned i=0;i<count;++i)if(!(picker.mask&(1u<<i)))available[n++]=i;
   index=available[random(n)];
  }
  picker.last=int(index);picker.mask|=1u<<index;
  if(picker.mask==((1u<<count)-1))picker.mask=0;
  return range.first+index;
 }
private:
 struct Picker {unsigned mask=0;int last=-1;};
 Picker pickers_[48][2]{};
 std::uint32_t rng_=0x9e3779b9u;
 unsigned random(unsigned n){rng_^=rng_<<13;rng_^=rng_>>17;rng_^=rng_<<5;return n?rng_%n:0;}
};
}
