#pragma once
#include <algorithm>
#include <cmath>
namespace mkw {
// Rear-facing projection has the opposite horizontal basis to the driving
// view. Adapt it once at the warning boundary, after mirror projection.
inline float chaseDrivingScreenX(float projectedX,float width,bool lookBack){
    return lookBack?width*.5f:width-projectedX;
}
// CtrlRaceChase, USA807e8c5c..807e8e2c; constants808a45b0..45cc.
// Native projected X uses .96 old + .04 new; camera Z drives the two
// animation playback rates, not a separate world-distance size multiplier.
struct ChaseWarningMotion {
 float x=0,lightFrame=0,zoomFrame=0;
 unsigned age=0;
 bool advance(float projectedX,float cameraZ,unsigned nextAge){
  if(cameraZ>=0||!std::isfinite(cameraZ)||!std::isfinite(projectedX))return false;
  const unsigned steps=nextAge>=age?nextAge-age:nextAge;
  const float speed=std::clamp(-2000.f/cameraZ,.01f,2.f);
  x=std::pow(.96f,float(steps))*x+(1-std::pow(.96f,float(steps)))*projectedX;
  lightFrame=std::fmod(lightFrame+steps*speed,20.f);
  zoomFrame=std::fmod(zoomFrame+steps*speed*1.5f,20.f);
  age=nextAge;return true;
 }
 float zoom()const{
  // chase_icon_item_zoom.brlan: Hermite 1 -> 1.2 -> 1, period20.
  const float t=(zoomFrame<=10?zoomFrame:20-zoomFrame)/10.f;
  return 1+.2f*t*t*(3-2*t);
 }
 float lightAngle()const{return lightFrame*18.f*.01745329252f;}
};
}
