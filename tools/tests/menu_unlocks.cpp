#include <mkw/menu_ui.hpp>
#include <mkw/menu_assets.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace mkw;
int main(){
 MenuState m;m.selection.license=0;
 UiInput accept{};accept.accept=true;
 m.enter(UiPage::Character);unsigned slot=0;while(m.characterDriver(slot)!=12)++slot;
 m.cursor=slot;m.input(accept);assert(m.page==UiPage::Character&&m.unavailable);
 auto q=composeMenu(m,{});bool question=false;
 for(const auto& a:q)question|=!std::strcmp(UiSprites[a.sprite].key,"locked_choice");assert(question);
 m.licenseUnlocks[0][0]|=1u<<2;m.input(accept);assert(m.page==UiPage::Vehicle&&m.selection.driver==12);
 m.cursor=m.vehicleSlot(6);m.input(accept);assert(m.page==UiPage::Vehicle&&m.unavailable);
 m.licenseUnlocks[0][1]|=1u<<(34-32);m.input(accept);assert(m.page==UiPage::Drift);
 m.enter(UiPage::Cup);m.cursor=2;m.input(accept);assert(m.page==UiPage::Cup&&m.unavailable);
 m.licenseUnlocks[0][0]|=1u<<16;m.input(accept);assert(m.page==UiPage::Confirm);
 assert(m.supportsRace());m.selection.license=1;assert(!m.supportsRace());
 m.selection.license=0;m.selection.mode=UiMode::TimeTrials;m.selection.engineClass=UiEngineClass::CC50;
 assert(m.cupUnlocked(2)); // Unlocked in 150cc is enough for Time Trials.
 m.selection.mode=UiMode::GrandPrix;assert(!m.cupUnlocked(2));
 m.enter(UiPage::EngineClass);assert(m.count()==3);m.licenseUnlocks[0][1]|=1u<<20;assert(m.count()==4);
 m.selection.mixedEngineTypes=true;m.selection.engineClass=UiEngineClass::CC50;m.enter(UiPage::Vehicle);assert(m.count()==6);
 m.licenseUnlocks[0][0]|=1u<<30;assert(m.count()==12);
 m.selection.engineClass=UiEngineClass::CC100;assert(m.count()==6); // Toggle must not unlock the other class.
 MenuState card;card.licenseOccupied[0]=true;card.selection.license=0;card.enter(UiPage::LicenseSettings);
 // Stale result bytes alone must not reveal locked cup or Mirror trophies.
 card.licenseCups[0].fill(1);
 auto drawn=[&](const char* key){unsigned count=0;for(const auto& a:composeMenu(card,{}))if(!std::strcmp(UiSprites[a.sprite].key,key))++count;return count;};
 assert(drawn("license_icon_n_2_hatena")==1&&drawn("license_icon_n_2_star")==0);
 assert(drawn("license_trophy_1")==12);
 card.licenseUnlocks[0][0]|=1u<<14;
 assert(drawn("license_icon_n_2_hatena")==0&&drawn("license_icon_n_2_star")==1);
 assert(drawn("license_trophy_1")==13);
 card.licenseUnlocks[0][1]|=1u<<20;
 assert(drawn("license_trophy_1")==17);
 puts("Menu unlocks: locked selections, earned choices, license isolation, TT any-class cups, Mirror and mixed-type isolation passed");
}
