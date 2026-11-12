#include <mkw/mii_body.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace mkw;
int main(){
 MiiAppearance a;MiiBodySpec result;
 // Original strict comparisons at the two boundaries, including very
 // different height/build combinations with an equal sum.
 for(unsigned h=0;h<128;++h)for(unsigned w=0;w<128;++w){
  a.height=h;a.weight=w;
  for(unsigned sex=0;sex<2;++sex)for(unsigned outfit=0;outfit<2;++outfit){
   a.female=sex;assert(miiBodySpec(a,outfit,result));
   unsigned retailGroup=1;
   if(int(w)>150-int(h))retailGroup=2;
   else if(int(w)>104-int(h))retailGroup=0;
   const unsigned originalBase=retailGroup==1?24:retailGroup==0?30:36;
   assert(result.retailCharacter==originalBase+outfit*2+sex);
   assert(result.stem[0]==(originalBase==24?'s':originalBase==30?'m':'l'));
   assert(result.stem[1]==(outfit?'b':'a')&&result.stem[7]==(sex?'f':'m'));
  }
 }
 a.height=52;a.weight=52;a.female=false;assert(miiBodySpec(a,0,result)&&result.headScale==.8f&&result.headX==7&&result.headY==10);
 a.weight=53;assert(miiBodySpec(a,0,result)&&result.headScale==.85f&&result.retailCharacter==30);
 a.height=75;a.weight=75;assert(miiBodySpec(a,1,result)&&result.retailCharacter==32);
 a.weight=76;a.female=true;assert(miiBodySpec(a,1,result)&&result.retailCharacter==39&&result.headScale==.95f&&result.headX==6&&result.headY==13);
 const auto old=result;a.height=128;assert(!miiBodySpec(a,0,result)&&result.stem==old.stem);a.height=0;
 a.favoriteColor=12;assert(!miiBodySpec(a,0,result));a.favoriteColor=0;assert(!miiBodySpec(a,2,result));
 puts("Mii body: 65536 body/sex/outfit combinations match original comparison branches; head offsets and invalid input checks passed");
}
