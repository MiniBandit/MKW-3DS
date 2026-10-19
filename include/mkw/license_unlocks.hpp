#pragma once
#include <array>
#include <cstdint>

namespace mkw {
using LicenseUnlockFlags=std::array<std::uint32_t,4>;
constexpr bool licenseCompleted(const LicenseUnlockFlags& flags,unsigned bit){
 return bit<128&&(flags[bit/32]&(std::uint32_t(1)<<(bit%32)));
}
// Retail USA 80550c74/80550e68, tables 8088cda8/8088ce10.
// Arguments are retail IDs, not the menu's display-order indices.
constexpr bool licenseCharacterUnlocked(const LicenseUnlockFlags& flags,unsigned character){
 constexpr int bits[]={-1,-1,-1,-1,2,0,-1,-1,-1,-1,-1,-1,3,1,-1,6,-1,7,4,9,5,8,10,11,12,13};
 if(character>=std::size(bits))return false;
 return bits[character]<0||licenseCompleted(flags,unsigned(bits[character]));
}
constexpr bool licenseVehicleUnlocked(const LicenseUnlockFlags& flags,unsigned vehicle){
 constexpr int bits[]={-1,-1,-1,-1,-1,-1,-1,-1,-1,34,37,40,35,38,41,36,39,42,
                     -1,-1,-1,-1,-1,-1,-1,-1,-1,43,46,49,44,47,50,45,48,51};
 if(vehicle>=std::size(bits))return false;
 return bits[vehicle]<0||licenseCompleted(flags,unsigned(bits[vehicle]));
}
// Retail USA 805509e0: Mushroom/Flower/Shell/Banana start available;
// Star/Special/Leaf/Lightning read four per-engine completion groups.
constexpr bool licenseCupUnlocked(const LicenseUnlockFlags& flags,unsigned cup,unsigned engine){
 if(cup>=8||engine>=4)return false;
 constexpr int base[]={-1,-1,14,18,-1,-1,22,26};
 return base[cup]<0||licenseCompleted(flags,unsigned(base[cup])+engine);
}
}
