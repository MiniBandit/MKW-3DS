#pragma once
#include <mkw/driver_look.hpp>
namespace mkw {
// USA807bf810..807bfc2c. Inputs are the unscaled physical pose's X.y/Z.y;
// response contains the six procedural angles at +308..+31c, in degrees.
// This layer is separate from CHR0 and the four item-target look angles.
class DriverVehicleResponse {
public:
 struct Angles {std::array<float,3> pitch{},roll{};float pitchSum()const{return pitch[0]+pitch[1]+pitch[2];}};
 void reset(){angles_={};rootResponse_=upperResponse_=0;}
 const Angles& angles()const{return angles_;}
 void step(unsigned state,unsigned previousState,bool transitioning,float blend,
           bool retailType1,float xAxisY,float zAxisY,const DriverLookParameters& parameters){
  if(!parameters.vehicleResponseEnabled)return;
  const auto& p=parameters.vehicleResponse;
  const float pitch=zAxisY*57.295780181884766f,roll=-xAxisY*57.295780181884766f;
  const bool normal=state==0||state==1||state==5||state==7||state==21||state==22||state==27||state==29;
  if(!normal&&state!=2&&state!=3){
   for(unsigned i=0;i<3;++i){angles_.pitch[i]*=p[12];angles_.roll[i]*=p[12];}
   rootResponse_=upperResponse_=0;return;
  }
  if(!normal){
   angles_.pitch[0]*=p[13];angles_.roll[0]*=p[13];rootResponse_=0;
   upperResponse_=std::min(upperResponse_+p[11],1.f);
  }else if(state==7){rootResponse_=upperResponse_=p[10];}
  else{rootResponse_=std::min(rootResponse_+p[11],1.f);upperResponse_=std::min(upperResponse_+p[11],1.f);}
  float extra=0;
  if(normal&&retailType1){
   if(state==7)extra+=p[9]*(transitioning?blend:1.f);
   if(previousState==7&&transitioning)extra+=p[9]*(1.f-blend);
  }
  for(unsigned i=normal?0:1;i<3;++i){
   const float response=i==0?rootResponse_:upperResponse_;
   const float wantPitch=pitch*p[i*3+(pitch>0?0:1)];
   const float wantRoll=roll*(p[i*3+2]+extra);
   angles_.pitch[i]+=(wantPitch-angles_.pitch[i])*response;
   angles_.roll[i]+=(wantRoll-angles_.roll[i])*response;
  }
 }
private:
 Angles angles_{};float rootResponse_=0,upperResponse_=0;
};
}
