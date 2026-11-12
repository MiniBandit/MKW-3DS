#include <mkw/license_save.hpp>
#include <cassert>
#include <mkw/menu_unlock_texts.hpp>
#include <cstdio>
using namespace mkw;
int main(){
 LicenseSave s;s.occupied=true;
 for(unsigned cc=0;cc<4;++cc)for(unsigned cup=0;cup<8;++cup)s.cupResult(cc,cup,1,2);
 for(auto& t:s.times)t=60000;
 s.records.finishes[0]=5100;
 const auto before=s.unlockFlags;
 const auto notices=s.availableUnlocks();
 assert(s.unlockFlags==before);assert(notices.size()==51);
 std::array<bool,128> seen{};unsigned previous=0;bool counts=false;
 for(const auto& n:notices){
  bool display=false;for(const auto& row:UiUnlockTexts)if(row.condition==n.condition&&row.bit==n.completionBit)display=true;
  assert(display); // every emitted reward must have its original text/model identity
  assert(!seen[n.completionBit]);seen[n.completionBit]=true;
  if(n.condition==61){counts=true;assert(n.raceRequirement);}
  else {assert(!counts);assert(n.condition>=previous);previous=n.condition;}
 }
 for(unsigned i=0;i<16;++i){
  const auto& n=notices[i];assert(n.condition==i&&n.completionBit==14+i);
  assert(n.messageId==0x1fa4+i/4&&n.engineMessageArgument==0xd53+i%4);
 }
 assert(notices[16].condition==16&&notices[16].completionBit==52);
 assert(s.refreshUnlocks());assert(s.availableUnlocks().empty());assert(!s.refreshUnlocks());
 LicenseSave tt;tt.occupied=true;
 for(unsigned i=0;i<8;++i)tt.times[i]=60000;
 auto n=tt.availableUnlocks();assert(n.size()==1&&n[0].condition==48&&n[0].messageId==8151);
 for(unsigned i=8;i<16;++i)tt.times[i]=60000;
 n=tt.availableUnlocks();assert(n.size()==2&&n[0].condition==25&&n[1].condition==48);
 for(auto& t:tt.times)t=60000;
 n=tt.availableUnlocks();assert(n.size()==3&&n[0].condition==18&&n[0].messageId==8111);
 LicenseSave count;count.occupied=true;count.records.battles=300;
 n=count.availableUnlocks();assert(n.size()==1&&n[0].condition==61&&n[0].completionBit==37&&n[0].raceRequirement==300&&n[0].messageId==0x1fe4);
 count.cupResult(0,6,1,-1);
 n=count.availableUnlocks();
 bool found=false;for(const auto& e:n)if(e.completionBit==37){assert(!found&&e.condition==41&&e.raceRequirement==0);found=true;}assert(found);
 assert(tt.unlockFlags==before&&count.unlockFlags==before);
 LicenseSave mirrorCount;mirrorCount.occupied=true;mirrorCount.records.finishes[0]=4800;
 bool mirrorFound=false;for(const auto& e:mirrorCount.availableUnlocks())if(e.completionBit==52){assert(e.condition==61&&e.messageId==0x1fe4&&e.raceRequirement==4800);mirrorFound=true;}assert(mirrorFound);
 LicenseSave star150;star150.occupied=true;star150.cupResult(2,2,1,-1);
 n=star150.availableUnlocks();assert(n.size()==2);
 assert(n[0].condition==6&&n[0].completionBit==20);
 assert(n[1].condition==47&&n[1].completionBit==43);
 star150.refreshUnlocks();assert(star150.availableUnlocks().empty());
 star150.cupResult(2,3,1,-1);n=star150.availableUnlocks();
 assert(n.size()==1&&n[0].condition==24&&n[0].completionBit==6);
 LicenseSave empty;empty.cups=s.cups;assert(empty.availableUnlocks().empty());
 puts("Unlock notices: retail priority, original messages, TT alternatives, deduplication, non-consuming query, occupied slots passed");
}
