#include <mkw/inseki_motion.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){
 for(int direction:{0,1})for(int period:{180,2000,-2000})for(int initial:{0,-50,90}){
  mkw::InsekiMotion full,half,sparse;
  float expected=float(initial)*0.01745329238474369f;
  float step=6.2831854820251465f/float(period);if(!direction)step=-step;
  assert(full.update(0,direction,period,initial)==expected);
  for(unsigned tick=1;tick<=12000;++tick){
   expected+=step;
   assert(full.update(tick,direction,period,initial)==expected);
   assert(full.update(tick,direction,period,initial)==expected);
   if(!(tick%2))assert(half.update(tick,direction,period,initial)==expected);
   if(tick==12000)assert(sparse.update(tick,direction,period,initial)==expected);
  }
  assert(full.update(0,direction,period,initial)==float(initial)*0.01745329238474369f);
 }
 mkw::InsekiMotion invalid;assert(std::isfinite(invalid.update(200,0,0,0)));
 puts("Original signed direction/period/phase; 60/30fps and hidden gaps identical through 12000 ticks; repeated draws and rewind passed");
}
