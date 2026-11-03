#include <mkw/race_camera.hpp>
#include <cassert>
#include <cstdio>
// Both collision activation phases must reach the same physics countdown,
// regardless of which countdown value the camera first observes.
int main(){
 for(int first:{130,129,100}){
  mkw::RaceCameraDirector c;
  mkw::RaceCameraInput in{};in.stage=2;in.beforeRespawn=true;
  for(int left=first;left>90;--left){in.respawnTimer=left;c.step(in);assert(c.wipeProgress()<0);}
  in.respawnTimer=90;c.step(in);assert(c.wipeProgress()==0);
  for(int t=1;t<=180;++t){
   in.respawnTimer=t<90?90-t:0;in.beforeRespawn=t<90;in.inRespawn=t>=90;
   c.step(in);assert(c.wipeProgress()==float(t)/180.f);
  }
  c.step(in);assert(c.wipeProgress()<0);
 }
 // A second fall while the current wipe is active does not restart it.
 mkw::RaceCameraDirector c;mkw::RaceCameraInput in{};in.stage=2;
 in.beforeRespawn=true;in.respawnTimer=90;c.step(in);
 in.beforeRespawn=false;c.step(in);
 in.beforeRespawn=true;in.respawnTimer=90;c.step(in);
 assert(c.wipeProgress()==2.f/180.f);
 puts("PASS real countdown trigger, both activation phases, rescue continuity, 180-frame endpoint and active-wipe guard");
}
