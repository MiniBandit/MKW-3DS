#pragma once
#include <mkw/math.hpp>
namespace mkw {
// DriverCalcWorldCB saves the posed skl_root matrix (807dacec).
// GetEffectMatrix (807c4e08) corrects that cached frame by the physics
// position delta, then predicts one velocity step. Its Gekko 10020828
// instruction is ps_sub, not multiplication (some disassemblers print a
// VMX mnemonic). The caller replaces the orientation with the kart pose.
class DriverEffectAnchor {
public:
 void reset(){ready_=false;}
 void store(Vec3 rootWorld,Vec3 framePosition){root_=rootWorld;framePosition_=framePosition;ready_=true;}
 bool predict(Vec3 currentPosition,Vec3 velocity,Vec3& result)const{
  if(!ready_)return false;
  result=root_+(currentPosition-framePosition_)+velocity;return true;
 }
private:
 Vec3 root_{},framePosition_{};bool ready_=false;
};
}
