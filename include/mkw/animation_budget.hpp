#pragma once
#include <algorithm>
#include <atomic>
namespace mkw {
// Presentation only. Source physics clocks and gameplay animation controllers
// are never slowed. Main thread publishes before frame-local jobs start.
inline std::atomic<unsigned> animationStride{1},fireCacheBytes{0},fireCacheStride{0};
inline unsigned animationStep(){return animationStride.load(std::memory_order_relaxed);}
class AnimationBudget {
 float average_=16.67f;unsigned lastTick_=0,slow_=0,fast_=0;bool ready_=false;
public:
 void reset(){average_=16.67f;ready_=false;slow_=fast_=0;animationStride.store(1);}
 void present(unsigned tick,bool active){
  if(!active){reset();return;}
  if(!ready_||tick<lastTick_||tick-lastTick_>12){lastTick_=tick;ready_=true;return;}
  const float ms=float(tick-lastTick_)*1000.f/60.f;lastTick_=tick;
  average_+=(ms-average_)*.125f;
  unsigned step=animationStep();
  const bool slow=average_>(step==1?21.f:32.f);
  const bool fast=average_<(step==4?27.f:18.f);
  slow_=slow?slow_+1:0;fast_=fast?fast_+1:0;
  if(slow_>=12&&step<4){step*=2;slow_=fast_=0;}
  else if(fast_>=90&&step>1){step/=2;slow_=fast_=0;}
  animationStride.store(step,std::memory_order_relaxed);
 }
};
}
