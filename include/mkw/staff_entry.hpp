#pragma once
#include <mkw/license_save.hpp>
#include <mkw/award_results.hpp>
namespace mkw {
enum class StaffEnding {None,Normal,True};
// AwardResult Next -> USA80550f60..80551200. r3=cup, r4=engine,
// r5=mirror, r6=human finishing place. Completion flag 56 is the true photo.
inline StaffEnding staffEnding(const LicenseSave& save,const AwardResults& results){
 if(!results.valid||results.mode!=0||results.teams)return StaffEnding::None;
 unsigned place=0;for(unsigned i=0;i<results.count;++i)if(results.players[i].human){place=i+1;break;}
 if(place<1||place>3)return StaffEnding::None;
 const bool finalCup=results.cup==3||results.cup==7;
 if(place==1){
  if(licenseCompleted(save.unlockFlags,56)){if(finalCup)return StaffEnding::True;}
  else{bool gold=true;for(auto cup:save.cups)gold&=(cup&255)==1;if(gold)return StaffEnding::True;}
 }
 if(finalCup){const unsigned cc=results.mirror?3:std::min(results.engineClass,2u);
  const auto a=save.cups[cc*8+3]&255,b=save.cups[cc*8+7]&255;
  if(a>=1&&a<=3&&b>=1&&b<=3)return StaffEnding::Normal;
 }
 return StaffEnding::None;
}
}
