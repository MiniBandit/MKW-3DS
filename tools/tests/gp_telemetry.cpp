#include <mkw/gp_telemetry.hpp>
#include <cassert>
int main(){
 mkw::GpTelemetry stats;stats.reset(true,40);
 stats.step(false,false,true,false,true,0,40);
 assert(stats.data().frames==0);
 stats.step(true,false,true,false,true,2,41);
 stats.step(true,false,false,true,false,2,41);
 stats.step(true,true,true,false,false,3,42);
 assert(stats.data().miniTurbos==2);
 assert(stats.finished()&&stats.data().frames==3&&stats.data().firstPlaceFrames==2);
 assert(stats.data().rocketStart&&stats.data().respawns==1&&stats.data().itemHits==3);
 stats.step(true,true,true,true,true,9,48);
 assert(stats.data().frames==3&&stats.data().itemHits==3&&stats.data().respawns==1);
 assert(stats.data().miniTurbos==2);
 stats.reset(false,48);
 stats.step(true,false,false,false,false,0,49);
 assert(stats.data().miniTurbos==1);
 stats.reset(false);
 assert(!stats.finished()&&!stats.data().automaticDrift&&!stats.data().rocketStart);
 assert(stats.data().frames==0&&stats.data().itemHits==0);
 stats.reset(false,0xffffffffu);
 stats.step(true,false,false,false,false,0,0);
 assert(stats.data().miniTurbos==1);
 stats.reset(false,0,120,7);
 stats.step(true,false,false,false,false,0,0,126,8);
 assert(stats.data().offroadFrames==6&&stats.data().objectHits==1);
 stats.step(true,true,false,true,false,0,0,129,8);
 assert(stats.data().offroadFrames==9&&stats.data().objectHits==1);
 stats.step(true,true,false,false,false,0,0,999,99);
 assert(stats.data().offroadFrames==9&&stats.data().objectHits==1);
 stats.reset(false,0,999,99);
 stats.step(true,false,false,false,false,0,0,999,99);
 assert(stats.data().offroadFrames==0&&stats.data().objectHits==0);
 stats.reset(false);
 for(int i=0;i<30;++i)stats.wallStep(false,false,false,0,true);
 stats.wallStep(true,false,true,30,true);
 assert(stats.data().wallHits==0); // Exact speed threshold is excluded.
 stats.wallStep(true,false,true,31,true);
 assert(stats.data().wallHits==1);
 for(int i=0;i<30;++i)stats.wallStep(true,false,true,31,true);
 assert(stats.data().wallHits==1); // Sustained contact cannot count each frame.
 stats.wallStep(true,false,true,31,false); // Zipper immunity still consumes the hit cooldown.
 assert(stats.data().wallHits==1);
 for(int i=0;i<31;++i)stats.wallStep(true,true,true,31,true);
 assert(stats.data().wallHits==1); // Rescue never triggers a wall hit.
 stats.wallStep(true,false,true,31,true);
 assert(stats.data().wallHits==2);
 stats.step(true,true,false,false,false,0,0);
 for(int i=0;i<40;++i)stats.wallStep(true,false,true,31,true);
 assert(stats.data().wallHits==2);
 assert(!mkw::countedWallContact(true,0xc,5)); // Bushes are separate effects.
 assert(!mkw::countedWallContact(true,0x1e,1));
 assert(mkw::countedWallContact(true,0xc,0));
}
