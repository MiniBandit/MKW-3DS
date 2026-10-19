#include <mkw/resource_load.hpp>
#include <cassert>
#include <cstdio>
#include <vector>
int main(){
 unsigned cases=0;
 // A failure at any required stage must release every partial owner before
 // retry; simulation may begin only when all nine stages are ready.
 for(unsigned stage=0;stage<9;++stage)for(bool permanent:{false,true}){
  unsigned attempts=0,cleanups=0,resident=0;bool playable=false;
  auto attempt=[&]{assert(resident==0&&!playable);++attempts;
   for(unsigned n=0;n<9;++n){if(n==stage&&(permanent||attempts==1))return false;++resident;}
   playable=true;return true;};
  auto clear=[&]{++cleanups;resident=0;playable=false;};
  const bool ok=mkw::loadRequiredResources(attempt,clear);
  assert(ok==!permanent&&attempts==2&&cleanups==(permanent?2u:1u));
  assert(playable==ok&&resident==(ok?9u:0u));++cases;
 }
 unsigned attempts=0,cleanups=0;
 assert(mkw::loadRequiredResources([&]{++attempts;return true;},[&]{++cleanups;}));
 assert(attempts==1&&cleanups==0);
 assert(!mkw::loadRequiredResources([&]{++attempts;return true;},[&]{++cleanups;},0));
 assert(attempts==1&&cleanups==0);
 printf("PASS %u required-stage transient/permanent failure, cleanup/retry and playability cases; immediate success and zero tries\n",cases);
}
