#include <mkw/driver_motion_blend.hpp>
#include <mkw/driver_vehicle_response.hpp>
#include <mkw/driver_controller.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace mkw;
int main(){
 // Concrete retail call-site cases. In particular Quacker uses the kart
 // branches here despite being a bike, and wait/back take the final override.
 assert(driverBaseBlendRate(0,2,false)==.05f);
 assert(driverBaseBlendRate(0,2,true)==.1f);
 assert(driverBaseBlendRate(7,1,false)==.07f);
 assert(driverBaseBlendRate(7,1,true)==.1f);
 assert(driverBaseBlendRate(1,0,true)==.05f);
 assert(driverBaseBlendRate(1,5,false)==.05f);
 for(unsigned state:{8u,9u,10u})assert(driverBaseBlendRate(0,state,true)==.05f);
 for(unsigned state:{6u,7u})assert(driverBaseBlendRate(1,state,true)==.07f);
 assert(driverBaseBlendRate(7,0,false)==.1f);
 DriverMotionBlend b;
 for(auto [rate,ticks]:{std::pair{.1f,10},std::pair{.05f,20},std::pair{.07f,15}}){
  b.start(rate,true);assert(b.active()&&b.weight()==0);
  for(int i=1;i<ticks;++i){b.advance();assert(b.active());assert(std::fabs(b.weight()-rate*i)<1e-6f);}
  b.advance();assert(!b.active()&&b.weight()==1);b.advance();assert(b.weight()==1);
 }
 b.start(.07f,false);assert(!b.active()&&b.weight()==1);
 // The waiting bike's extra roll follows the same child-1 weight, rather
 // than snapping off or fading on an unrelated six-frame render clock.
 DriverLookParameters p;p.vehicleResponse={0,0,0,0,0,0,0,0,0,1,1,1,.8f,.9f};
 DriverVehicleResponse r;b.start(driverBaseBlendRate(7,0,true),true);
 for(unsigned t=0;t<10;++t){
  b.advance();r.step(0,7,b.active(),b.weight(),true,1,0,p);
  const float expected=-57.295780181884766f*(1-b.weight());
  for(float roll:r.angles().roll)assert(std::fabs(roll-expected)<1e-5f);
 }
 r.reset();b.start(driverBaseBlendRate(0,7,true),true);
 for(unsigned t=0;t<15;++t){
  b.advance();r.step(7,0,b.active(),b.weight(),true,1,0,p);
  for(float roll:r.angles().roll)assert(std::fabs(roll+57.295780181884766f*b.weight())<1e-5f);
 }
 DriverController controller;DriverSignals sig;sig.accelerating=true;sig.speed=50;
 auto pose=controller.step(sig);assert(pose.state==0&&pose.blendWeight==1);
 sig.drift=true;sig.driftDirection=1;pose=controller.step(sig);
 assert(pose.state==2&&pose.previousState==0&&pose.blendWeight==.05f);
 sig.drift=false;
 for(unsigned t=0;t<19;++t){pose=controller.step(sig);assert(pose.state==2);}
 assert(pose.blendWeight==1&&!pose.transitioning);
 pose=controller.step(sig);assert(pose.state==0&&pose.previousState==2&&pose.blendWeight==.1f);
 sig.hit=true;pose=controller.step(sig);assert(pose.state==26&&pose.previousState==0&&pose.blendWeight==.07f);
 controller.reset();sig.hit=false;pose=controller.step(sig);assert(pose.state==0&&pose.blendWeight==1);
 // A takeoff during a bike drift transition waits, then starts at frame
 // zero using .1 (the ordinary state-29 path, not state40 -> 29's .05).
 controller.reset();sig={};sig.bike=true;sig.handleRelativeBike=true;sig.accelerating=true;sig.speed=50;
 controller.step(sig);sig.drift=true;sig.driftDirection=1;controller.step(sig);
 sig.airborne=true;sig.drift=false;
 for(unsigned t=0;t<9;++t){pose=controller.step(sig);assert(pose.state==2);}
 pose=controller.step(sig);assert(pose.state==29&&pose.frame==0&&pose.blendWeight==.1f);
 // Landing while that takeoff is still blending stays queued without losing
 // its initial frames, despite the airborne edge being present only once.
 sig.airborne=false;
 for(unsigned t=0;t<9;++t){pose=controller.step(sig);assert(pose.state==29);}
 pose=controller.step(sig);assert(pose.state==27&&pose.frame==0&&pose.blendWeight==.1f);
 controller.reset();sig.airborne=false;sig.trick=false;controller.step(sig);
 sig.airborne=true;sig.trick=true;pose=controller.step(sig);
 assert(pose.state==30&&pose.blendWeight==.1f);
 b.reset();assert(!b.active()&&b.weight()==1);
 puts("PASS original base-state rates, 10/20/15-tick weights, first-pose/reset and bike wait response coupling");
}
