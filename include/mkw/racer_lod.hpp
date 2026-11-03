#pragma once
#include <algorithm>
#include <cmath>
namespace mkw {
// Approved 400x240 / 60-degree preview. Thresholds are camera distances,
// not kart-to-kart distances. Only presentation changes, never simulation.
struct RacerLod {
 unsigned stage=1;bool initialized=false;
 unsigned update(float distanceSquared,float fov,bool fullDetail){
  if(fullDetail){stage=1;initialized=false;return stage;}
  const float ratio=std::tan(std::clamp(fov,1.f,170.f)*0.00872664626f)/0.57735026919f;
  const float d2=distanceSquared*ratio*ratio;
  constexpr float limit[]={1750.f,3000.f,10000.f};
  if(!initialized){stage=1;for(float t:limit)if(d2>=t*t)++stage;initialized=true;return stage;}
  // Keep the chosen lower-detail stage until 5% nearer, and vice versa.
  while(stage<4&&d2>limit[stage-1]*limit[stage-1]*1.1025f)++stage;
  while(stage>1&&d2<limit[stage-2]*limit[stage-2]*.9025f)--stage;
  return stage;
 }
};
}
