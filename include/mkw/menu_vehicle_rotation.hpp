#pragma once
#include <algorithm>
#include <cmath>
namespace mkw {
// Retain the retail 720 half-degree units and positive turn direction.
// Confirm follows the current positive cycle to the fixed retail landing
// angle (0; the render adds 90 half-degrees). Never append an extra cycle.
// Browsing/return continue the current pose without clock resynchronisation.
class MenuVehicleRotation {
public:
 float angle=0;
 void update(bool hover,bool confirm){
  if(confirm&&!confirm_){start_=angle;travel_=720.f-angle;frame_=0;spinning_=true;}
  if(hover&&!hover_)spinning_=false;
  hover_=hover;confirm_=confirm;
 }
 void advance(unsigned ticks){while(ticks--){
  if(spinning_){
   const float t=float(++frame_)/Duration;
   const float eased=1.f-(1.f-t)*(1.f-t);
   angle=std::fmod(start_+travel_*eased,720.f);
   if(frame_>=Duration){angle=0;spinning_=false;}
  }else if(hover_)angle=std::fmod(angle+1.f,720.f);
 }}
 static constexpr unsigned Duration=36;
private:
 bool hover_=false,confirm_=false,spinning_=false;
 float start_=0,travel_=0;unsigned frame_=0;
};
}
