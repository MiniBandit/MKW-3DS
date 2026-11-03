#pragma once
#include <mkw/mii_appearance.hpp>
#include <array>
#include <cstdint>
namespace mkw {
struct MiiBodySpec {
 unsigned weightClass=0,retailCharacter=0;
 const char* stem=nullptr;
 // Original head local transform: R(0,-pi/2,pi), then translation
 // (entry.z,-entry.y,0), uniform entry.scale; attached to face_1.
 float headScale=1,headX=0,headY=0;
 std::uint32_t clothingRGBA=0;
};
// Mii::Load USA80521c48..80521ca4: the two seven-bit body values
// are summed; <=104 small, 105..150 medium, >150 large.
// MenuDriverModel::SetPlayerCharacter USA8084db00..8084dbf0 chooses
// the matching gender and outfit; ArchiveMgr postfix table USA808af240.
inline bool miiBodySpec(const MiiAppearance& appearance,unsigned outfit,MiiBodySpec& output){
 if(appearance.height>127||appearance.weight>127||appearance.favoriteColor>=12||outfit>1)return false;
 constexpr const char* stems[]={"sa_mii_m","sa_mii_f","sb_mii_m","sb_mii_f",
  "ma_mii_m","ma_mii_f","mb_mii_m","mb_mii_f",
  "la_mii_m","la_mii_f","lb_mii_m","lb_mii_f"};
 // common and MiiBody miiDispParam.bin agree byte for byte. A/B
 // use the same head dimensions; original unused C entries are omitted.
 constexpr float scales[3][2]={{.8f,.75f},{.85f,.8f},{1.f,.95f}};
 constexpr float ys[3][2]={{10,11},{11,12},{13,13}};
 constexpr float xs[3][2]={{7,7},{7,6},{5,6}};
 // Original clothing lookup USA80783f2c -> 808a0158 (not the
 // distinct RFL portrait shirt palette).
 constexpr std::uint32_t colors[]={0xb40000ff,0xf08228ff,0xffd200ff,0x96cd01ff,
  0x008223ff,0x053caaff,0x3c96f0ff,0xff87a5ff,0x5a0569ff,0x543708ff,0xe0e0e0ff,0x2d2d23ff};
 const unsigned sum=appearance.height+appearance.weight;
 const unsigned weight=sum>150?2:sum>104?1:0;
 const unsigned sex=appearance.female?1:0;
 MiiBodySpec value;
 value.weightClass=weight;value.retailCharacter=24+weight*6+outfit*2+sex;
 value.stem=stems[weight*4+outfit*2+sex];
 value.headScale=scales[weight][sex];value.headX=xs[weight][sex];value.headY=ys[weight][sex];
 value.clothingRGBA=colors[appearance.favoriteColor];output=value;return true;
}
}
