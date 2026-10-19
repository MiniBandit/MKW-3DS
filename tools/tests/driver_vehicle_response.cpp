#include <mkw/driver_vehicle_response.hpp>
#include <mkw/driver_look_data.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
int main(){
 unsigned cases=0;
 for(unsigned c=0;c<24;++c)for(unsigned v=0;v<36;++v){
  DriverLookParameters p;if(!driverLookParameters(c,v,p))continue;
  DriverVehicleResponse r;
  for(unsigned t=0;t<100;++t)r.step(0,0,false,1,false,0,0,p);
  assert(r.angles().pitchSum()==0);
  // A constant slope converges to the source's sign-specific gain.
  for(unsigned t=0;t<400;++t)r.step(0,0,false,1,false,.2f,-.3f,p);
  for(unsigned i=0;i<3;++i){
   assert(std::abs(r.angles().pitch[i]-(-.3f*57.295780181884766f*p.vehicleResponse[i*3+1]))<.002f);
   assert(std::abs(r.angles().roll[i]-(-.2f*57.295780181884766f*p.vehicleResponse[i*3+2]))<.002f);
  }
  const auto before=r.angles();r.step(2,0,false,1,false,.2f,-.3f,p);
  assert(std::abs(r.angles().pitch[0]-before.pitch[0]*p.vehicleResponse[13])<.0001f);
  // Unsupported motions decay every component, including the upper body.
  const auto drift=r.angles();r.step(6,2,false,1,false,.2f,-.3f,p);
  for(unsigned i=0;i<3;++i)assert(std::abs(r.angles().pitch[i]-drift.pitch[i]*p.vehicleResponse[12])<.0001f);
  const auto saved=r.angles();p.vehicleResponseEnabled=false;
  r.step(0,0,false,1,false,1,1,p);
  for(unsigned i=0;i<3;++i){assert(r.angles().pitch[i]==saved.pitch[i]);assert(r.angles().roll[i]==saved.roll[i]);}
  r.reset();assert(r.angles().pitchSum()==0);++cases;
 }
 assert(cases==288);printf("vehicle response: %u combinations flat/slope/drift/decay/reset PASS\n",cases);
}
