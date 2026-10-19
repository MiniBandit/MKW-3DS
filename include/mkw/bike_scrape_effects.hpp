#pragma once
#include <mkw/race_effects.hpp>
#include <array>
namespace mkw {
// Retail KartEffect 80694b90..c20 / 80694ea8..f40: inside-drift
// body scrape (warm sparks) and its white charging trail, not blue MT sparks.
class BikeScrapeEffects {
public:
 static constexpr unsigned FirstSlot=1718,Slots=4;
 bool bind(RaceEffects& e){
  constexpr const char* names[]={"rk_hangOnL","rk_hangOnR","rk_hangOnLTail","rk_hangOnRTail"};
  for(unsigned i=0;i<Slots;++i){spec_[i]=e.find(names[i]);if(spec_[i]<0)return false;}reset();return true;
 }
 void reset(){airborne_=10;}
 void step(RaceEffects& e,const EffectFrame& body,const EffectFrame& rear,unsigned drift,int direction,
           bool autoDrifting,bool floor,bool enabled,unsigned first=FirstSlot){
  airborne_=floor?0:std::min(airborne_+1,10u);
  const bool active=enabled&&(drift>0||autoDrifting)&&airborne_<10&&direction!=0;
  // On negative hopStickX the original range is [19,20) (R); positive
  // selects [18,19) (L). Tail ranges are [21,22) and [20,21).
  const unsigned side=direction<0?1:0;
  for(unsigned i=0;i<Slots;++i){const bool tail=i>=2;
   e.attach(first+i,spec_[i],tail?rear:body,
            active&&(i%2)==side&&(!tail||drift==1||autoDrifting));
   if(!enabled)e.kill(first+i);
  }
 }
private:
 std::array<int,Slots> spec_{};unsigned airborne_=10;
};
}
