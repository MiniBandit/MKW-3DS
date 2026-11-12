#pragma once
#include <algorithm>
namespace mkw {
// Grounded outside-drift yaw, KartMove::controlOutsideDriftAngle (8057EAB8)
// and calcManualDrift's release branch. Scene motion supplies the travel
// direction; this angle separates chassis heading from that direction.
inline float awardDriftAngle(float angle,int direction,bool inside,float target,float tightness,float decrement){
 if(!direction)return angle>0?std::max(0.f,angle-decrement):std::min(0.f,angle+decrement);
 if(inside)return angle;
 if(direction==-1){
  if(angle>target)return std::max(angle-2.f,target);
  if(angle<target)return std::min(angle+150.f*tightness,target);
 }else if(direction==1){
  if(-target>angle)return std::min(angle+2.f,-target);
  if(-target<angle)return std::max(angle-150.f*tightness,-target);
 }
 return angle;
}
}
