#pragma once
#include <mkw/award_results.hpp>
#include <mkw/award_model_assets.hpp>
#include <cstring>

namespace mkw {
inline unsigned awardWeight(unsigned character){
 // Original character size lookup 80809f90 (retail ids).
 constexpr uint8_t weights[]={1,0,2,2,0,0,0,1,0,2,1,2,0,0,0,1,1,1,1,2,1,2,2,2};
 return character<std::size(weights)?weights[character]:character<42?(character-24)/6:1;
}
struct AwardPerformer {
 AwardResults::Player player{};
 const AwardModelAsset* asset=nullptr;
 const char* clip=nullptr;
};
struct AwardCast {
 std::array<AwardPerformer,12> podium{},audience{};
 unsigned podiumCount=0,audienceCount=0;
 const AwardModelAsset* stand=nullptr;
 bool valid=false;
};
inline const char* awardCharacterArchive(unsigned character,unsigned vehicle){
 // Retail character ids, using the original Demo/Award archive names.
 constexpr const char* archives[]={"mr","bpc","wl","kp","bds","ka","bmr","lg",
  "ko","dk","ys","wr","blg","kk","nk","ds","pc","ca","dd","kt","jr","bk","fk","rs"};
 if(character>=24&&character<42){
  constexpr const char* mii[]={"sa_mii_m","sa_mii_f","sb_mii_m","sb_mii_f",nullptr,nullptr,"ma_mii_m","ma_mii_f","mb_mii_m","mb_mii_f",nullptr,nullptr,"la_mii_m","la_mii_f","lb_mii_m","lb_mii_f",nullptr,nullptr};
  return mii[character-24];
 }
 if(character>=std::size(archives))return nullptr;
 if(vehicle>=18){
  if(character==15)return "ds3";
  if(character==16)return "pc3";
  if(character==23)return "rs3";
 }
 return archives[character];
}
inline const AwardModelAsset* awardActorAsset(const char* stem){
 if(!stem)return nullptr;
 constexpr const char* prefix="unpacked/Award/";
 for(const auto& asset:AwardModelAssets){
  if(std::strncmp(asset.archive,prefix,std::strlen(prefix)))continue;
  const auto* name=asset.archive+std::strlen(prefix);
  if(!std::strncmp(name,stem,std::strlen(stem))&&!std::strcmp(name+std::strlen(stem),".brres")&&
     !std::strcmp(asset.model,"model"))return &asset;
 }
 return nullptr;
}
// winner is provided by the ceremony outcome selector (including the retail
// tie policy), not guessed from a character's individual ranking.
inline AwardCast makeAwardCast(const AwardResults& results,RaceTeam winner=RaceTeam::None){
 AwardCast cast;
 if(!results.valid||!results.count||results.count>12)return cast;
 if(results.teams&&winner==RaceTeam::None)return cast;
 for(unsigned i=0;i<results.count;++i){
  const auto& player=results.players[i];
  const bool onStand=results.teams?player.team==winner:i<3;
  const auto* asset=awardActorAsset(awardCharacterArchive(player.character,player.vehicle));
  // Every performer must resolve to its own original body archive.
  if(!asset)return {};
  AwardPerformer performer{player,asset,onStand?"demowin":"demooth"};
  if(onStand)cast.podium[cast.podiumCount++]=performer;
  else cast.audience[cast.audienceCount++]=performer;
 }
 if(!cast.podiumCount)return {};
 // Dispatch's original insertion sort places smaller spectators first.
 std::stable_sort(cast.audience.begin(),cast.audience.begin()+cast.audienceCount,
  [](const auto& a,const auto& b){return awardWeight(a.player.character)<awardWeight(b.player.character);});
 if(results.teams){
  // USA AwardsMgr::Init 807d0ce8: local winners precede CPUs;
  // each group retains total-ranking order.
  std::stable_partition(cast.podium.begin(),cast.podium.begin()+cast.podiumCount,
   [](const auto& actor){return actor.player.human;});
 }
 // USA AwardsMgr::SetParams 807d2770: team stands widen for 3 / 4+ winners.
 const unsigned stand=results.teams?(cast.podiumCount>=4?2:cast.podiumCount==3?3:4):1;
 constexpr const char* names[]={"v_stand1","v_stand2","v_stand3","v_stand4"};
 for(const auto& asset:AwardModelAssets)
  if(!std::strcmp(asset.archive,"unpacked/Award/v_stand1.brres")&&!std::strcmp(asset.model,names[stand-1])){
   cast.stand=&asset;break;
  }
 cast.valid=cast.stand!=nullptr;
 return cast;
}
}
