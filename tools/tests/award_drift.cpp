#include <mkw/award_drift.hpp>
#include <mkw/math.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){
 using namespace mkw;
 for(int side:{-1,1}){
  float angle=0;
  for(int t=0;t<100;++t){angle=awardDriftAngle(angle,side,false,24,.02f,1.5f);assert(std::fabs(angle)<=24);}
  assert(angle==-side*24.f);
  // A turn change must settle on the opposite target without overshoot.
  for(int t=0;t<100;++t)angle=awardDriftAngle(angle,-side,false,24,.02f,1.5f);
  assert(angle==side*24.f);
  for(int t=0;t<16;++t)angle=awardDriftAngle(angle,0,false,24,.02f,1.5f);
  assert(angle==0);
 }
 assert(awardDriftAngle(0,1,true,24,.02f,1.5f)==0);
 // Heading inversion preserves floor tangency and unit length on slopes.
 const Vec3 up=normalized(Vec3{0,1,1}),travel{1,0,0};
 for(float angle:{-24.f,24.f}){
  const float a=-angle*.0174532924f;
  const Vec3 heading=travel*std::cos(a)+cross(up,travel)*std::sin(a);
  const Vec3 recovered=heading*std::cos(-a)+cross(up,heading)*std::sin(-a);
  assert(std::fabs(dot(heading,up))<1e-6f&&std::fabs(length(heading)-1)<1e-6f);
  assert(length(recovered-travel)<1e-6f);
 }
 puts("PASS left/right angle targets, reversal, release, inside-bike exclusion and sloped-floor heading inversion");
}
