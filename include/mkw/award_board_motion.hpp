#pragma once
#include <algorithm>

namespace mkw {
// Page::AnimateControls (USA 805ddd7c): source control delays and a
// ten-tick linear translation across the 832-unit widescreen layout.
class AwardBoardMotion {
public:
 void reset(){active_=leaving_=false;frame_=0;}
 void enter(){active_=true;leaving_=false;frame_=0;}
 void leave(){if(active_){leaving_=true;frame_=0;}}
 void step(){if(active_)++frame_;}
 bool active()const{return active_;}
 bool leaving()const{return leaving_;}
 float offset(float delay)const{
  const float t=std::clamp((float(frame_)-delay)/10.f,0.f,1.f);
  return leaving_?-832.f*t:832.f*(1.f-t);
 }
 bool finished(float lastDelay)const{return active_&&float(frame_)>=lastDelay+10.f;}
private:
 bool active_=false,leaving_=false;
 unsigned frame_=0;
};
}
