#pragma once
#include <cstdint>
namespace mkw {
// obj_inseki: USA8077d950..96c,8077da30..4c,8077da74..aa0.
// GOBJ settings 1/2/3 are direction/period/initial degrees (signed s16).
// The axis is EGG's unit Z, initialized at 80009a58..74.
class InsekiMotion {
 float angle_=0,step_=0;unsigned tick_=0;bool initialized_=false;
public:
 float update(unsigned tick,std::int16_t direction,std::int16_t period,std::int16_t degrees){
  if(!initialized_||tick<tick_){
   angle_=float(degrees)*0.01745329238474369f;
   step_=period?6.2831854820251465f/float(period):0.f;
   if(!direction)step_=-step_;
   tick_=0;initialized_=true;
  }
  // Advance simulation ticks rather than render calls, including time hidden.
  while(tick_<tick){angle_+=step_;++tick_;}
  return angle_;
 }
};
}
