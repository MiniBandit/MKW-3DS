#pragma once
#include <mkw/race_effects.hpp>
#include <array>
namespace mkw {
// USA80694f44/80695180: inside-drift negates hopStickX. The local
// 8068efe0 table has two sustained roots per side.
class BikeDriftEffects {
public:
 static constexpr unsigned FirstSlot=1703,Slots=15;
 bool bind(RaceEffects& effects){
  constexpr const char* sides[]={"LB","RB","CB"};
  constexpr const char* tails[]={"_Spark00","_Spark01"};
  for(unsigned side=0;side<3;++side)for(unsigned part=0;part<2;++part){
   char name[64];std::snprintf(name,sizeof name,"rk_driftSpark1%s%s",sides[side],tails[part]);
   specs_[side*2+part]=effects.find(name);if(specs_[side*2+part]<0)return false;
  }
  constexpr const char* pulseTails[]={"_Chip00","_Spark00","_Spark01"};
  for(unsigned side=0;side<3;++side)for(unsigned part=0;part<3;++part){
   char name[64];std::snprintf(name,sizeof name,"rk_driftSpark1%s1T%s",sides[side],pulseTails[part]);
   pulses_[side*3+part]=effects.find(name);if(pulses_[side*3+part]<0)return false;
  }reset();return true;
 }
 void reset(){airborne_=10;previousDrift_=0;}
 void step(RaceEffects& e,const EffectFrame& chassis,unsigned drift,int direction,bool inside,bool floor,bool enabled){
  airborne_=floor?0:std::min(airborne_+1,10u);
  const int selected=inside?-direction:direction;
  const unsigned side=selected<0?0:selected>0?1:2;
  const bool charge=enabled&&drift>=2&&airborne_<10;
  for(unsigned i=0;i<3;++i)for(unsigned part=0;part<2;++part){
   const unsigned slot=FirstSlot+i*2+part;const bool active=charge&&i==side;
   e.attach(slot,specs_[i*2+part],chassis,active);
   if(!enabled)e.kill(slot);
  }
  // Retail 80694f44/80695220: entering charged MT from state 1 starts
  // the side-specific 1T chip/core/flash trio, independently of sustained roots.
  for(unsigned i=0;i<3;++i)for(unsigned part=0;part<3;++part){
   const unsigned slot=FirstSlot+6+i*3+part;
   if(charge&&previousDrift_==1&&i==side)e.restart(slot,pulses_[i*3+part],chassis);
   else e.move(slot,chassis);
   if(!enabled)e.kill(slot);
  }
  previousDrift_=enabled?drift:0;
 }
private:
 std::array<int,6> specs_{};std::array<int,9> pulses_{};unsigned airborne_=10,previousDrift_=0;
};
}
