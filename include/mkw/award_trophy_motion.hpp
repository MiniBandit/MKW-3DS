#pragma once
#include <mkw/math.hpp>
#include <mkw/award_board_motion.hpp>
#include <cmath>

namespace mkw {
// USA Trophy::Show / Update, 8076d854..8076db84. One step is one
// original 60 Hz simulation tick, independent of display refresh rate.
class AwardTrophyMotion {
public:
 static constexpr unsigned AppearSound=0x21a;
 void reset(){visible_=moving_=leaving_=false;exit_.reset();phase_=yaw_=frame_=0;x_=700;exitStart_=0;speed_=-30;}
 // The result page leaves left over ten ticks (AwardBoardMotion). Convert
 // one full viewport to this overlay's eyeZ1000/FOV45/aspect5:3 world units.
 static constexpr float LeaveDistance=2.f*1000.f*0.414213562373095f*(5.f/3.f);
 void leave(){if(visible_&&!leaving_){leaving_=true;moving_=false;exitStart_=x_;exit_.enter();exit_.leave();}}
 unsigned show(){
  reset();visible_=moving_=true;yaw_=3.1415927410125732f;
  return AppearSound;
 }
 void step(bool paused=false){
  if(!visible_||paused)return;
  constexpr float radians=0.01745329238474369f;
  if(leaving_){
   exit_.step();x_=exitStart_+LeaveDistance*(exit_.offset(0)/832.f);
   if(exit_.finished(0))visible_=false;
  }else if(moving_){
   phase_+=1.7999999523162842f;
   const float t=std::sin(phase_*radians);
   x_=700.f-700.f*t;
   speed_=-30.f+29.799999237060547f*t;
   if(x_<=0){x_=0;moving_=false;}
  }
  yaw_+=speed_*radians;
  ++frame_;
 }
 bool visible()const{return visible_;}
 bool entering()const{return moving_;}
 bool leaving()const{return leaving_;}
 Vec3 position()const{return {x_,0,0};}
 float yaw()const{return yaw_;}
 float frame()const{return frame_;}
private:
 bool visible_=false,moving_=false,leaving_=false;
 AwardBoardMotion exit_;
 float phase_=0,yaw_=0,frame_=0,x_=700,exitStart_=0,speed_=-30;
};
}
