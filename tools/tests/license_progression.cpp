#include <mkw/license_save.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
int main(){
 LicenseSave s;s.occupied=true;assert(!s.refreshUnlocks());
 s.cupResult(0,0,3,-1);assert(!s.refreshUnlocks());
 s.cupResult(0,1,4,-1);s.refreshUnlocks();assert(!licenseCupUnlocked(s.unlockFlags,2,0));
 s.cupResult(0,1,3,-1);s.refreshUnlocks();assert(licenseCupUnlocked(s.unlockFlags,2,0));
 assert(!licenseCupUnlocked(s.unlockFlags,2,1));
 s.cupResult(0,2,3,-1);s.refreshUnlocks();assert(licenseCupUnlocked(s.unlockFlags,3,0));
 assert(!licenseCharacterUnlocked(s.unlockFlags,19)); // King Boo requires gold.
 s.cupResult(0,2,1,3);s.refreshUnlocks();assert(licenseCharacterUnlocked(s.unlockFlags,19));
 for(unsigned cc=0;cc<4;++cc){
  LicenseSave t;t.occupied=true;
  for(unsigned c=0;c<8;++c)t.cupResult(cc,c,1,3);
  t.refreshUnlocks();assert(!licenseCharacterUnlocked(t.unlockFlags,4)); // A is not a star.
  assert(licenseCompleted(t.unlockFlags,52)==(cc==2));
  for(unsigned c=0;c<8;++c)t.cupResult(cc,c,1,2);
  t.refreshUnlocks();assert(licenseCharacterUnlocked(t.unlockFlags,4)==(cc==0));
  assert(licenseCharacterUnlocked(t.unlockFlags,23)==(cc==3));
  assert(!t.refreshUnlocks());
 }
 // Gold rewards decoded from the retail case offsets, not port vehicle order.
 const unsigned rewards[][3]={{0,1,6},{4,0,7},{6,2,3},{9,0,2},{12,1,3},
  {36,3,7},{37,0,6},{38,2,6},{40,0,3},{42,2,7},{43,2,2},
  {45,3,6},{46,1,7},{48,3,1},{49,1,2},{51,3,3}};
 for(auto& r:rewards){LicenseSave t;t.occupied=true;t.cupResult(r[1],r[2],2,-1);t.refreshUnlocks();
  assert(!licenseCompleted(t.unlockFlags,r[0]));t.cupResult(r[1],r[2],1,-1);t.refreshUnlocks();assert(licenseCompleted(t.unlockFlags,r[0]));}
 // Every star-group needs all of its original cups; empty/unknown grade fails.
 const unsigned starRewards[][4]={{2,0,0,4},{5,1,4,4},{8,2,0,4},{11,3,0,8},{34,0,4,4},{41,2,4,4},{47,1,0,4}};
 for(auto& r:starRewards){LicenseSave t;t.occupied=true;
  for(unsigned i=0;i<r[3];++i){t.cupResult(r[1],r[2]+i,1,2);t.refreshUnlocks();assert(licenseCompleted(t.unlockFlags,r[0])==(i+1==r[3]));}}
 LicenseSave count;count.occupied=true;count.records.finishes[1]=299;count.refreshUnlocks();
 assert(!licenseVehicleUnlocked(count.unlockFlags,10));
 count.records.battles=1;count.refreshUnlocks();assert(licenseVehicleUnlocked(count.unlockFlags,10));
 count.records.finishes[1]=5099;count.records.battles=0;count.refreshUnlocks();
 assert(!licenseCharacterUnlocked(count.unlockFlags,25));
 count.records.battles=1;count.refreshUnlocks();assert(licenseCharacterUnlocked(count.unlockFlags,25));
 LicenseSave tt;tt.occupied=true;
 for(unsigned i=0;i<32;++i){tt.times[i]=60000;tt.refreshUnlocks();
  assert(licenseVehicleUnlocked(tt.unlockFlags,30)==(i>=7));
  assert(licenseCharacterUnlocked(tt.unlockFlags,17)==(i>=15));
  assert(licenseCharacterUnlocked(tt.unlockFlags,13)==(i==31));}
 tt.unlockFlags[3]=0x12345678;tt.refreshUnlocks();assert(tt.unlockFlags[3]==0x12345678);
 puts("Retail progress: per-class podium cups, gold/star distinctions, Mirror, total-count boundaries, TT course counts and retained flags passed");
}
