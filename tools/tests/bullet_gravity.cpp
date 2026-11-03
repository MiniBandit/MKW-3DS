#include <mkw/bullet_bill.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
int main(){
 BulletGravity g;
 assert(!g.step(1,true,false));assert(!g.latched);
 assert(g.step(2,false,false));assert(!g.latched);
 assert(g.step(0,true,false)&&g.latched);
 assert(g.step(0,false,false)); // latch persists when leaving the tube
 assert(!g.step(1,true,false)&&!g.latched);
 assert(!g.step(0,false,false));
 assert(g.step(3,true,false));
 assert(!g.step(2,true,true)&&!g.latched); // respawn wins even over force-flight
 for(int i=0;i<11;++i){assert(g.step(2,true,false));g.observeWall(true,-.8f);}
 assert(!g.step(2,true,false)&&g.recovery==59);
 for(int i=0;i<4;++i)g.observeWall(false,0);
 assert(g.wallFrames==0);
 for(int i=0;i<59;++i)assert(!g.step(2,true,false));
 assert(g.step(2,true,false));
 g.reset();for(int i=0;i<11;++i)g.observeWall(true,-.75f);
 assert(g.step(2,true,false)); // threshold is strict
 const Vec3 a{0,0,0},b{100,0,0},n{0,1,0};
 assert(!bulletInsideRoute({50,-1,0},a,b,1,2,n,true,0));
 assert(bulletInsideRoute({50,1,0},a,b,1,2,n,true,0));
 assert(bulletInsideRoute({50,1,0},a,b,1,2,n,false,0));
 assert(!bulletInsideRoute({50,0,75},a,b,1,2,n,false,0));
 assert(bulletInsideRoute({200,0,99},a,b,1,2,n,false,0));
 assert(!bulletInsideRoute({200,0,100},a,b,1,2,n,false,0));
 assert(bulletInsideRoute({},a,a,1,1,n,false,0));
 puts("PASS route gravity modes, latch, respawn override, ceiling recovery and interpolated tube boundaries");
}
