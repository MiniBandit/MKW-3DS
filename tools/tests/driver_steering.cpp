#include <mkw/driver_controller.hpp>
#include <cassert>
#include <cmath>
using namespace mkw;
int main(){
 DriverSignals s;s.accelerating=true;s.speed=50;s.steering=1;
 DriverController ordinary,cannon,frozen;
 const auto normal=ordinary.step(s);
 s.inCannon=true;const auto shot=cannon.step(s);
 assert(std::fabs(normal.frame-13.5f)<.001f);
 assert(std::fabs(shot.frame-14.7f)<.001f);
 s.inCannon=false;s.frozenInIce=true;
 for(int i=0;i<100;++i)assert(std::fabs(frozen.step(s).frame-15.f)<.001f);
 s.frozenInIce=false;s.steering=0;s.burnout=true;s.burnoutPitch=1;
 DriverController burnout;
 assert(std::fabs(burnout.step(s).frame-12.f)<.001f);
 assert(std::fabs(burnout.step(s).frame-11.4f)<.001f);
 burnout.reset();s.burnout=false;
 assert(std::fabs(burnout.step(s).frame-15.f)<.001f);
 // Presentation changes must not suppress successful-hit and throw clips.
 s.celebrate=true;assert(!std::strcmp(burnout.step(s).clipName,"gut"));
 s.celebrate=false;s.itemThrowDirection=-1;assert(!std::strcmp(burnout.step(s).clipName,"throw_b"));
 s.hit=true;assert(!std::strcmp(burnout.step(s).clipName,"damage"));
 s.hit=false;s.finished=true;s.finishType=0;
 for(unsigned i=0;i<60;++i)assert(std::strcmp(burnout.step(s).clipName,"1st"));
 assert(!std::strcmp(burnout.step(s).clipName,"1st"));
 burnout.reset();assert(std::strcmp(burnout.step(s).clipName,"1st"));
 assert(mkw::driverFinishType(1,12)==0);
 assert(mkw::driverFinishType(5,12)==1&&mkw::driverFinishType(6,12)==2);
 assert(mkw::driverFinishType(2,3)==1&&mkw::driverFinishType(3,3)==2);
 assert(mkw::driverFinishType(2,2)==2);
 // A body loop must not reset its independently authored face timeline.
 // Peach good uses 62 body frames and 100 face frames; Luigi has a
 // non-looping good expression. Sampling/clamping belongs to each bank.
 DriverController result;result.setDuration("good",62);
 DriverSignals winner;winner.finished=true;winner.finishType=1;
 DriverPose victory;
 for(unsigned i=0;i<61+125;++i)victory=result.step(winner);
 assert(!std::strcmp(victory.clipName,"good"));
 assert(victory.frame==125&&victory.faceFrame==125);
 // Returning to driving and finishing anew starts a fresh expression.
 winner.finished=false;winner.accelerating=true;winner.speed=50;result.step(winner);
 winner.finished=true;
 for(unsigned i=0;i<61;++i)victory=result.step(winner);
 assert(victory.frame==0&&victory.faceFrame==0);
}
