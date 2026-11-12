#include <mkw/presentation_cadence.hpp>
#include <cassert>
#include <initializer_list>
#include <cstdio>
int main(){
 for(bool n:{false,true})for(bool s:{false,true})for(bool i:{false,true}){
  const unsigned ticks=mkw::racePresentationTicks(n,s,i);
  assert(ticks==((i||!n||s)?2u:1u));
  mkw::PresentationCadence cadence;cadence.setTicksPerSlot(ticks);unsigned draws=0;
  for(unsigned tick=0;tick<600;++tick){draws+=cadence.due(tick);assert(!cadence.due(tick));}
  assert(draws==600/ticks);
  // Missed presentations coalesce, not queue a burst of catch-up renders.
  assert(cadence.due(700));assert(!cadence.due(700));
  cadence.setTicksPerSlot(1);assert(cadence.due(701));assert(cadence.due(702));
  cadence.setTicksPerSlot(2);assert(cadence.due(702));assert(!cadence.due(703));assert(cadence.due(704));
 }
 puts("PASS eight hardware/stereo/intro combinations; 600 source ticks; missed-slot coalescing; intro/race cadence switches");
}
