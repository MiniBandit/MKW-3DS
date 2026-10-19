#include <mkw/kart_start_effects.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
int main(){
 KartStartEffects s;
 // Original KPad accelerator flag, independent of residual boost charge.
 assert(s.step(20,true,true,true,true,true,false,0,0)==KartStartEffects::Hold);
 assert(s.step(20,false,true,true,true,true,false,0,0)==3);
 assert(s.step(21,false,true,false,true,true,false,0,0)==0);
 assert(s.step(22,false,true,true,true,false,false,0,0)==1);
 assert(s.step(23,false,true,true,false,true,false,0,0)==2);
 assert(s.step(24,false,true,true,true,true,true,0,0)==1);
 // Source request tick holds existing instances; duration ticks 1..N-1
 // update all three layers at each colliding rear wheel, then fade them.
 assert(s.step(240,false,false,true,true,true,false,1,3)==KartStartEffects::Hold);
 assert(s.step(241,false,false,false,true,true,false,1,3)==3);
 assert(s.step(242,false,false,false,true,false,false,1,3)==1);
 assert(s.step(243,false,false,true,true,true,false,1,3)==0);
 assert(s.step(244,false,false,true,true,true,false,2,1)==KartStartEffects::Hold);
 assert(s.step(245,false,false,true,true,true,false,2,1)==0);
 // The retail global race-frame guard keeps the handles untouched past 2000.
 assert(s.step(2001,false,true,true,true,true,false,2,1)==KartStartEffects::Hold);
 s={};assert(s.step(0,false,false,false,true,true,false,0,0)==0);
 puts("PASS retail accelerator/release, preview, per-wheel floor/bike gates, first callback hold, duration edges, repeated callback, reset and frame guard");
}
