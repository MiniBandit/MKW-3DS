#include <mkw/cpu_roster.hpp>
#include <mkw/garage.hpp>
#include <mkw/menu_ui.hpp>
#include <cassert>
#include <set>
#include <iostream>
int main(){
 using namespace mkw;unsigned runs=0;std::set<std::array<unsigned,12>> variants;
 for(unsigned full=0;full<2;++full){LicenseUnlockFlags flags{};if(full)flags.fill(~0u);
  for(unsigned human=0;human<24;++human){if(!licenseCharacterUnlocked(flags,StarterDrivers[human].character))continue;
   for(unsigned seed=0;seed<64;++seed){CpuRosterRandom rng{seed};CpuRoster r;r.characters(human,flags,rng);
    std::set<unsigned> ids;std::array<unsigned,3> weights{};
    for(auto c:r.choices){auto& d=StarterDrivers[c/12];assert(licenseCharacterUnlocked(flags,d.character));ids.insert(d.character);++weights[d.vehicle];}
    assert(ids.size()==12&&weights[0]==4&&weights[1]==4&&weights[2]==4);auto chars=r.choices;
    for(unsigned kind=0;kind<3;++kind){r.vehicles(0,kind,false,flags,rng);assert(r.choices[0]==human*12);
     for(unsigned i=1;i<12;++i){assert(r.choices[i]/12==chars[i]/12);auto& v=GarageChoices[r.choices[i]];assert(licenseVehicleUnlocked(flags,v.vehicle));assert(kind==2||v.bike==(kind==1));}
     variants.insert(r.choices);++runs;
     r.vehicles(0,kind,true,flags,rng);for(unsigned i=1;i<12;++i)assert(r.choices[i]%12==0||r.choices[i]%12==3);
    }
   }
  }
 }
 assert(variants.size()>1000);
 std::cout<<"CPU roster: "<<runs<<" combinations and "<<variants.size()<<" unique lineups passed\n";
}
