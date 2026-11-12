#pragma once
#include <mkw/roster.hpp>
#include <mkw/license_unlocks.hpp>
#include <array>
#include <cstdint>

namespace mkw {
// Retail Random::NextLimited (USA8054f5ac). Seed comes from the host clock.
struct CpuRosterRandom {
 std::uint64_t state=1;
 unsigned next(unsigned limit){
  state=state*0x690379b2b2e3d431ULL+0x508ebdULL;
  return unsigned((std::uint64_t(std::uint32_t(state>>32))*limit)>>32);
 }
};
// Garage display indices, retained across races/restarts and shared with Teams.
struct CpuRoster {
 std::array<unsigned,12> choices{};
 bool ready=false;
 void characters(unsigned human,const LicenseUnlockFlags& flags,CpuRosterRandom& random,unsigned miiWeight=1){
  std::array<unsigned,3> weights{};std::array<bool,24> used{};
  choices[0]=human*12;
  if(human<StarterDrivers.size()){used[StarterDrivers[human].character]=true;++weights[StarterDrivers[human].vehicle];}
  else ++weights[std::min(miiWeight,2u)];
  // CharacterSelect USA808241c8: sample retail IDs 0..23, reject locked,
  // duplicate and full weight classes (four of each for twelve racers).
  for(unsigned slot=1;slot<choices.size();++slot){
   for(;;){
    const unsigned id=random.next(24);
    if(used[id]||!licenseCharacterUnlocked(flags,id))continue;
    unsigned driver=0;while(StarterDrivers[driver].character!=id)++driver;
    const unsigned weight=StarterDrivers[driver].vehicle;
    if(weights[weight]>=4)continue;
    used[id]=true;++weights[weight];choices[slot]=driver*12;break;
   }
  }
  ready=true;
 }
 void vehicles(unsigned humanVehicle,unsigned kind,bool battle,const LicenseUnlockFlags& flags,CpuRosterRandom& random){
  // KartSelect USA8082a584 uses twelve retail table entries, not menu order.
  constexpr unsigned menuIndex[]={0,1,2,6,7,8,3,4,5,9,10,11};
  choices[0]=choices[0]/12*12+humanVehicle;
  for(unsigned slot=1;slot<choices.size();++slot){
   const unsigned driver=choices[slot]/12,weight=StarterDrivers[driver].vehicle;
   if(battle){ // BattleKartSelect USA808520ac: only standard kart/bike.
    const unsigned pick=random.next(2);choices[slot]=driver*12+3*(kind<2?kind:pick);continue;
   }
   for(;;){
    const unsigned entry=random.next(12),id=weight+entry*3;
    if((kind==0&&entry>=6)||(kind==1&&entry<6)||!licenseVehicleUnlocked(flags,id))continue;
    choices[slot]=driver*12+menuIndex[entry];break;
   }
  }
 }
};
}
