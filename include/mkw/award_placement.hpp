#pragma once
#include <mkw/award_cast.hpp>
#include <mkw/math.hpp>

namespace mkw {
struct AwardPlacement {Vec3 position{};float yaw=0;};
struct AwardStandPlacement {Vec3 position{};float scale=1;};
inline AwardStandPlacement placeAwardStand(bool teams,Vec3 center,Vec3 groundAnchor){
 // Original SetPositions uses scale 1.25 for team stands. The exported
 // vertices already contain the world ground anchor (0,1224,27850).
 // Their authored top is +80, while the original team feet are +100:
 // pivot at the ground so 80*1.25=100. Adding another +100 to this folded
 // world anchor applied the vertical compensation twice (25 units afloat).
 if(!teams)return {center,1};
 return {center-groundAnchor*.25f,1.25f};
}
struct AwardPlacements {
 std::array<AwardPlacement,12> podium{},audience{};
};
inline AwardPlacements placeAwardCast(const AwardCast& cast,bool teams,Vec3 center){
 // Original AwardsMgr position functions 807d2e9c..807d3ad4; constants
 // 808a2a48. Keep authored world axes; no camera-relative rearrangement.
 AwardPlacements out;
 const unsigned count=cast.podiumCount;
 constexpr float pairSpacing[]={120,135,150},tripleSpacing[]={190,205,220};
 constexpr float outerSpacing[]={170,210,180},frontSpacing[]={140,185,170};
 constexpr float radius[]={60,80,100};
 for(unsigned i=0;i<count;++i){
  const auto w=awardWeight(cast.podium[i].player.character);Vec3 p{};
  if(!teams)p={i==1?-225.f:i==2?225.f:0,i==0?140.f:70.f,0};
  else if(count<=2)p={count==2?(i==0?pairSpacing[w]:-pairSpacing[w]):0,100,-100};
  else if(count==3)p={i==1?-tripleSpacing[w]:i==2?tripleSpacing[w]:0,100,-100};
  else{
   const Vec3 right{50,100,-145},left{-50,100,-145};
   if(count==4){
    p=(i&1)?left:right;
    if(i>=2)p.x+=(i&1)?-outerSpacing[w]:outerSpacing[w];
   }else{
    p=(i&1)?left:right;
    if(i<2){p.x+=(i&1)?-70.f:70.f;p.z+=frontSpacing[w];}
    else if(i>=4)p.x+=(i&1)?-outerSpacing[w]:outerSpacing[w];
    if(count==6&&i==2){
     const float sum=radius[w]+radius[awardWeight(cast.podium[5].player.character)];
     const float overlap=sum*sum-100.f*100.f;
     if(overlap>0)p.z-=std::sqrt(overlap);
    }
   }
  }
  out.podium[i].position=center+p;
 }
 const unsigned stand=teams?(count>=4?3:count==3?2:1):0;
 constexpr float side[]={200,250,250,380},step[]={100,100,250};
 Vec3 columns[]={{side[stand],0,700},{-side[stand],0,700}};
 for(unsigned i=0;i<cast.audienceCount;++i){
  const auto w=awardWeight(cast.audience[i].player.character);
  auto& column=columns[i&1];column.z+=step[w];auto p=column;
  if(i&2)p.x+=(i&1)?-150.f:150.f;
  out.audience[i]={center+p,(i&1)?1.5707963705f:-1.5707963705f};
  column.z+=step[w];
 }
 return out;
}
}
