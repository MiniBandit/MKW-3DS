// Host regression for the presentation state; no emulator or input automation.
#include "mkw/wheel_motion.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using mkw::WheelMotion;
void near(float a,float b){assert(std::fabs(a-b)<.00002f);}
int main(){
    WheelMotion motion;
    WheelMotion::Inputs in;in.stage=2;in.ground=true;in.accelerate=in.brake=in.ssmt=true;
    near(motion.step(in),3.f); // Physical speed is zero; retail target is ten.
    near(motion.step(in),5.1f);
    for(unsigned frame=2;frame<60;++frame)motion.step(in);
    near(motion.speed,10.f);
    in.ssmt=in.accelerate=in.brake=false;
    near(motion.step(in),7.f); // Release transitions smoothly back to chassis speed.
    for(unsigned frame=0;frame<60;++frame)motion.step(in);
    assert(motion.speed<.000001f);
    motion={};in.speed=80;
    near(motion.step(in),24.f);
    motion={};in.speed=-10;
    near(motion.step(in),-3.f);
    motion={};in.speed=0;in.brake=true;in.wall=true;
    near(motion.step(in),-1.5f);
    motion={};in.accelerate=true;
    near(motion.step(in),1.5f);
    motion={};in.action=true;
    near(motion.step(in),0.f);
    in.action=false;in.ground=false;motion={};
    near(motion.step(in),0.f);
    in={};in.stage=1;in.startCharge=.5f;in.chargeStart=true;motion={};
    near(motion.step(in),1.5f);
    in.chargeStart=false;near(motion.step(in),.45f);
    in={};in.stage=2;in.burnout=true;motion={};
    near(motion.step(in),3.f);
    in.burnout=false;in.speed=80;in.maxSpeed=15;motion={};
    near(motion.step(in),15.f);near(motion.speed,24.f); // Clamp only body output.
    in.speed=-80;in.minSpeed=-6;motion={};near(motion.step(in),-6.f);
    in={};motion={};near(motion.step(in),0.f);
    std::cout<<"wheel motion: stationary charge, release, forward/reverse, wall gates, countdown, burnout and display bounds passed\n";
}
