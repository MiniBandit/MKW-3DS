#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
namespace mkw {
// USA StaticR REL: GessoManager 0x2c9eb4/0x2ca03c; start CHR0 is70
// frames, then30 pause, three frames per player order, attack contact at148.
inline constexpr std::array<uint8_t,12> BlooperPlayerOrder{3,1,4,11,5,9,2,6,10,8,7,0};
inline constexpr unsigned BlooperInkDuration=450; // applyInk 0x6f1c0, .data+0x2dd4
inline constexpr unsigned BlooperSelectionDelay=100;
// The screen's attack animation (SE_ITM_GESO_ATK) starts this long before the ink lands.
inline constexpr unsigned BlooperAttackLead=148;
// Original GessoScreen::UpdateModel, USA 807d5510; data 808a2d38.
inline constexpr unsigned BlooperStartFrames=70,BlooperAttackFrames=176,BlooperEndFrames=80;
inline constexpr std::array<float,3> BlooperDistance{85.f,100.f,140.f};
inline constexpr std::array<float,3> BlooperScale{.39f,.41f,.44f};
struct BlooperVisual {const char* clip=nullptr;unsigned frame=0;float up=0,forward=0,scale=1;unsigned player=0;};
inline BlooperVisual blooperVisual(unsigned frame,unsigned start,unsigned attack,unsigned end,unsigned weight){
 const unsigned w=std::min(weight,2u);
 if(end&&frame>=end){if(frame-end>=BlooperEndFrames)return {};return {"attack_end",frame-end,180.f,BlooperDistance[w],BlooperScale[w]};}
 if(attack&&frame>=attack){if(frame-attack>=BlooperAttackFrames)return {};return {"attack",frame-attack,180.f,BlooperDistance[w],BlooperScale[w]};}
 if(frame>=start&&frame-start<BlooperStartFrames)return {"start",frame-start,150.f,100.f,.4f};
 return {};
}
inline constexpr unsigned BlooperManagerDuration=100+3*11+176;
inline constexpr unsigned blooperHitDelay(unsigned player){return 70+30+BlooperAttackLead+3*BlooperPlayerOrder[player%12];}
}
