#pragma once
#include <algorithm>
#include <cstdint>
namespace mkw {
// USA807c3000..807c3104. State numbers are retail descriptors, not DSKM
// clip indices. +f4 is the last accepted base state, +f6 the requested one.
inline float driverBaseBlendRate(uint16_t previous,uint16_t requested,bool handleRelativeBike){
 float rate=.1f;
 if((requested>=8&&requested<=10)||(!handleRelativeBike&&(requested==2||requested==3)))rate=.05f;
 else if(requested==1)rate=handleRelativeBike?.1f:.07f;
 else if(previous==1&&(requested==0||requested==5))rate=.05f;
 if(requested==6||requested==7)rate=.07f;
 return rate;
}
// USA80555eac..80555ee8 initializes weights (1,0), 80556280..8055632c
// adds the configured rate to child 1, clamps, and detaches child 0 at 1.
// Update once from the model tick; render/stereo queries must not advance it.
class DriverMotionBlend {
public:
 void reset(){weight_=1;rate_=0;}
 void start(float rate,bool previousPresent){rate_=rate;weight_=previousPresent?0.f:1.f;}
 void advance(){if(active())weight_=std::clamp(weight_+rate_,0.f,1.f);}
 bool active()const{return weight_<1.f;}
 float weight()const{return weight_;}
private:
 float weight_=1,rate_=0;
};
}
