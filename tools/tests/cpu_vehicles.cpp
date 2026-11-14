#include <mkw/menu_ui.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
// Retail engine-class buttons limit the CPUs' machines like the player's:
// 50cc karts, 100cc bikes, 150cc/Mirror and Karts and Bikes both.
static unsigned race(UiEngineClass engine,bool mixed,bool allUnlocked,std::uint64_t seed){
 MenuState m;m.selection.license=0;m.selection.mode=UiMode::GrandPrix;
 m.selection.engineClass=engine;m.selection.mixedEngineTypes=mixed;
 if(allUnlocked)m.licenseUnlocks[0].fill(0xffffffffu);
 m.cpuRandom.state=seed;
 UiInput accept{};accept.accept=true;
 m.enter(UiPage::Character);unsigned slot=0;while(m.characterDriver(slot)!=0)++slot;
 m.cursor=slot;m.input(accept);assert(m.page==UiPage::Vehicle);
 m.cursor=0;m.input(accept);assert(m.page==UiPage::Drift&&m.cpuRoster.ready);
 unsigned kinds=0; // bit 0 kart, bit 1 bike
 for(unsigned i=1;i<12;++i){const unsigned v=m.cpuRoster.choices[i]%12;kinds|=(v%6<3)?1u:2u;}
 return kinds;
}
int main(){
 for(bool all:{false,true})for(std::uint64_t seed=1;seed<=200;++seed){
  assert(race(UiEngineClass::CC50,false,all,seed)==1u);
  assert(race(UiEngineClass::CC100,false,all,seed)==2u);
 }
 for(bool all:{false,true}){unsigned c150=0,cMirror=0,c50mixed=0,c100mixed=0;
  for(std::uint64_t seed=1;seed<=200;++seed){
   c150|=race(UiEngineClass::CC150,false,all,seed);
   cMirror|=race(UiEngineClass::Mirror,false,all,seed);
   if(all){c50mixed|=race(UiEngineClass::CC50,true,all,seed);c100mixed|=race(UiEngineClass::CC100,true,all,seed);}
  }
  assert(c150==3u&&cMirror==3u);
  if(all)assert(c50mixed==3u&&c100mixed==3u);
 }
 // Without the Karts and Bikes unlock the toggle changes nothing.
 for(std::uint64_t seed=1;seed<=50;++seed)assert(race(UiEngineClass::CC50,true,false,seed)==1u);
 puts("PASS GP CPU machines: 50cc karts, 100cc bikes, 150cc/Mirror/Karts and Bikes both");
}
