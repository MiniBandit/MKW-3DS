#include <mkw/driver_look_target.hpp>
#include <mkw/driver_look_palette.hpp>
#include <mkw/driver_look.hpp>
#include <mkw/driver_look_gate.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace mkw;
int main(){
 for(const char* clip:{"drive","dash","drift_l","drift_r","wheelie","wait2","wait"})assert(driverLookClipAllowed(clip));
 for(const char* clip:{"back","throw_f","throw_b","throw_f_ed","gut","damage","jump","sjump_a","bad"})assert(!driverLookClipAllowed(clip,true));
 for(const char* clip:{"1st","good"}){assert(!driverLookClipAllowed(clip));assert(driverLookClipAllowed(clip,true));}
 assert(!driverLookClipAllowed(nullptr));
 for(unsigned flags=0;flags<4;++flags){
  assert(driverLookAllowed("drive",flags&1,flags&2)==(flags==0));
  assert(!driverLookAllowed("back",flags&1,flags&2));
  assert(!driverLookAllowed("good",flags&1,flags&2));
  assert(driverLookAllowed("good",flags&1,flags&2,true)==(flags==0));
 }
 const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0};
 const float head[]={0,0,1,0,0,-1,0,100,1,0,0,0,0,0,1,0,-1,0,1,0,0};
 auto check=[](float a,float b){assert(std::abs(a-b)<.001f);};
 auto a=driverLookTargetAngles(identity,head,{100,100,100});assert(a.valid);check(a.yaw,45);check(a.pitch,0);
 a=driverLookTargetAngles(identity,head,{0,200,100});check(a.pitch,-45);check(a.yaw,0);
 // Rotate the whole kart and target 90 degrees, scale and translate.
 const float world[]={0,0,2,300,0,2,0,-50,-2,0,0,400};
 a=driverLookTargetAngles(world,head,{500,150,200});check(a.yaw,45);check(a.pitch,0);
 assert(!driverLookTargetAngles(identity,head,{0,100,0}).valid);
 DriverLookMotion motion;DriverLookParameters p;uint32_t slot=0;
 for(unsigned i=0;i<300;++i){
  float posed[21];std::memcpy(posed,head,sizeof head);const auto old=motion.angles();
  applyDriverLookPalette(posed,0,&slot,1,old.yaw,old.pitch);
  a=driverLookTargetAngles(identity,posed,{100,100,100});
  const auto current=motion.step(true,false,a.yaw,a.pitch,a.kartYaw,0,0,p);
  assert(std::isfinite(current.yaw)&&std::abs(current.yaw)<70);
 }
 assert(motion.angles().yaw>30&&motion.angles().yaw<50);
 for(unsigned i=0;i<300;++i)motion.step(false,false,0,0,0,0,0,p);
 assert(std::abs(motion.angles().yaw)<.001f);
 puts("world/head target transform, scale, feedback and restore PASS");
}
