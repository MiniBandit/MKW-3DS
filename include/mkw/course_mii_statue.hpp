#pragma once
#include <cstdint>
namespace mkw {
// MiiManager::CreateMii USA80524384..80524414. All ordinary statue
// creation groups set requireMiiRacer=1. CPU/ghost racers do not qualify.
inline constexpr bool miiStatuePlayerEligible(unsigned type,unsigned character){
 return (type==0||type==2)&&character>=24&&character<=44;
}
template<class Players> bool miiStatuePlayersEligible(const Players& players,unsigned count){
 for(unsigned i=0;i<count&&i<players.size();++i)
  if(miiStatuePlayerEligible(unsigned(players[i].type),unsigned(players[i].character)))return true;
 return false;
}
// RMCE01 OnStart/static initializer: see mii-statue-bindings-retail.json.
struct MiiStatueBinding {std::uint16_t id;float scale,x,y;};
inline constexpr MiiStatueBinding MiiStatueBindings[]={
 {358,0.8500000238418579f,7.0f,12.0f}, // MiiStatueM1
 {735,0.8999999761581421f,9.0f,9.0f}, // MiiStatueM2
 {380,0.8999999761581421f,9.0f,11.0f}, // MiiStatueL1
 {381,0.699999988079071f,3.0f,1.5f}, // MiiStatueD1
 {382,0.699999988079071f,1.5f,8.0f}, // MiiSphinxY1
 {383,0.75f,-2.0f,9.0f}, // MiiSphinxY2
 {390,0.8500000238418579f,7.0f,12.5f}, // MiiStatueL2
 {391,0.699999988079071f,5.0f,4.0f}, // MiiStatueD2
 {392,0.699999988079071f,3.0f,1.0f}, // MiiStatueP1
 {397,0.8999999761581421f,0.0f,6.0f}, // MiiStatueDK1
 {737,0.8500000238418579f,7.0f,11.0f}, // MiiStatueL3
 {739,0.75f,5.5f,4.0f}, // MiiStatueBL1
 {740,0.75f,3.0f,3.0f}, // MiiStatueBD1
};
inline const MiiStatueBinding* miiStatueBinding(unsigned id){
 for(const auto& b:MiiStatueBindings)if(b.id==id)return &b;return nullptr;
}
}
