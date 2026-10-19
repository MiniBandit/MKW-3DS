#include <mkw/license_unlocks.hpp>
#include <cassert>
#include <cstdio>
int main(){
 using namespace mkw;
 LicenseUnlockFlags flags{};
 unsigned characters=0,vehicles=0,cups=0;
 for(unsigned i=0;i<26;++i)characters+=licenseCharacterUnlocked(flags,i);
 for(unsigned i=0;i<36;++i)vehicles+=licenseVehicleUnlocked(flags,i);
 for(unsigned i=0;i<8;++i)cups+=licenseCupUnlocked(flags,i,0);
 assert(characters==12&&vehicles==18&&cups==4);
 // Every completion bit changes exactly its retail target. Distinct engine
 // classes must not inherit each other's cup unlocks.
 for(unsigned bit=0;bit<128;++bit){
  flags={};flags[bit/32]=std::uint32_t(1)<<(bit%32);
  unsigned nc=0,nv=0,nu=0;
  for(unsigned i=0;i<26;++i)nc+=licenseCharacterUnlocked(flags,i);
  for(unsigned i=0;i<36;++i)nv+=licenseVehicleUnlocked(flags,i);
  for(unsigned cc=0;cc<4;++cc)for(unsigned i=0;i<8;++i)nu+=licenseCupUnlocked(flags,i,cc);
  assert(nc==12+(bit<14));assert(nv==18+(bit>=34&&bit<=51));
  assert(nu==16+(bit>=14&&bit<=29));
 }
 flags={~0u,~0u,~0u,~0u};
 assert(!licenseCharacterUnlocked(flags,26)&&!licenseVehicleUnlocked(flags,36));
 assert(!licenseCupUnlocked(flags,8,0)&&!licenseCupUnlocked(flags,0,4));
 assert(!licenseCompleted(flags,128));
 puts("Retail unlock flags: starter roster, all 128 independent bits, per-class cups and invalid IDs passed");
}
