#include <mkw/luma_motion.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace mkw;
static bool near(float a,float b){return std::fabs(a-b)<.001f;}
int main(){
    LumaMotion luma;LumaMotion::Input in;
    in.position={100,200,300};in.scaleY=2;luma.step(in);
    assert(near(luma.position().y,380));
    // Hopping raises the chassis, not the companion target.
    in.position.y+=20;in.hopHeight=20;luma.step(in);
    assert(near(luma.position().y,380));
    // Constant-speed following must remain bounded, not drift with elapsed time.
    in.hopHeight=0;in.velocity={0,0,80};
    for(int i=0;i<10000;++i){in.previousPosition=in.position;in.position+=in.velocity;luma.step(in);}
    assert(near(in.position.z-luma.position().z,40));
    // A cannon translates the kart independently of its dynamics velocity.
    in.inCannon=true;in.previousPosition=in.position;in.position.z+=1000;
    luma.step(in);assert(in.position.z-luma.position().z<150);
    // Respawn teleports immediately, without interpolating across the course.
    in.resetPosition=true;in.position={-400,10,90};in.scaleY=.5f;luma.step(in);
    assert(near(luma.position().x,-400)&&near(luma.position().y,55)&&near(luma.position().z,90));
    luma.reset();in={};luma.step(in);in.overZipper=true;in.bodyForward={1,0,0};
    for(int i=0;i<20;++i)luma.step(in);
    assert(luma.forward().x>.999f);
    assert(std::fabs(dot(luma.right(),luma.up()))<.001f);
    assert(std::fabs(dot(luma.forward(),luma.up()))<.001f);
    in.overZipper=false;for(int i=0;i<20;++i)luma.step(in);
    assert(luma.forward().z>.999f);
    std::puts("Luma hop, 10000-tick travel, cannon, respawn and halfpipe checks passed");
}
