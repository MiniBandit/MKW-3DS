#include <mkw/license_save.hpp>
#include <mkw/roster.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace mkw {
namespace {
// Retail RKSD0006 / RKPD. Byte offsets rather than native structs keep the
// Wii big-endian bitfields independent of ARM alignment and host endianness.
constexpr unsigned CoreSize=0x28000,FileSize=0x2bc000,LicenseSize=0x8cc0,GhostSize=0xa5000;
using Core=std::array<unsigned char,CoreSize>;
constexpr unsigned SaveCourse[]={0,1,2,3,4,5,6,7,8,9,11,10,13,12,14,15,24,28,17,21,20,16,31,26,29,19,22,25,18,30,27,23};
constexpr unsigned BattleStage[]={1,0,3,2,4,7,8,9,5,6};
constexpr unsigned RaceCourse[]={8,1,2,4,0,5,6,7,9,15,11,3,14,10,12,13,16,20,25,26,27,31,23,18,21,30,29,17,24,22,19,28};
unsigned be16(const unsigned char* p){return unsigned(p[0])<<8|p[1];}
std::uint32_t be32(const unsigned char* p){return std::uint32_t(p[0])<<24|std::uint32_t(p[1])<<16|unsigned(p[2])<<8|p[3];}
void put16(unsigned char* p,unsigned n){p[0]=n>>8;p[1]=n;}
void put32(unsigned char* p,std::uint32_t n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
// Same retail reflected CRC32, one byte lookup instead of eight bit steps.
constexpr auto CrcTable=[](){std::array<std::uint32_t,256> table{};for(unsigned i=0;i<256;++i){auto c=std::uint32_t(i);for(unsigned k=0;k<8;++k)c=(c>>1)^(0xedb88320u&-(c&1));table[i]=c;}return table;}();
std::uint32_t crc(const Core& b){std::uint32_t c=~0u;for(unsigned i=0;i<CoreSize-4;++i)c=(c>>8)^CrcTable[(c^b[i])&255];return ~c;}
bool exists(const std::string& p){FILE* f=std::fopen(p.c_str(),"rb");if(!f)return false;std::fclose(f);return true;}
bool read(const std::string& p,Core& b){
 FILE* f=std::fopen(p.c_str(),"rb");if(!f)return false;
 std::setvbuf(f,nullptr,_IONBF,0);
 bool ok=std::fread(b.data(),1,b.size(),f)==b.size();
 if(std::fseek(f,0,SEEK_END)||std::ftell(f)!=FileSize)ok=false;
 std::fclose(f);
 if(!ok||std::memcmp(b.data(),"RKSD0006",8)||std::memcmp(b.data()+0x23308,"RKGD",4)||be32(b.data()+CoreSize-4)!=crc(b))return false;
 for(unsigned s=0;s<4;++s){const auto* p=b.data()+8+s*LicenseSize;if(be32(p)&&std::memcmp(p,"RKPD",4))return false;}
 return true;
}
// Port-only options never consume unknown bytes in the retail save. Bind each
// slot companion to the stored Mii ID, so importing a different license does
// not silently inherit the previous license's settings.
using Options=std::array<unsigned char,24>;
std::uint32_t optionsCrc(const Options& b){std::uint32_t c=~0u;for(unsigned i=0;i<20;++i)c=(c>>8)^CrcTable[(c^b[i])&255];return ~c;}
std::string optionsPath(const char* root,unsigned slot){return std::string(root)+"/license-options-"+std::to_string(slot)+".dat";}
bool readOptions(const std::string& path,Options& b){
 FILE* f=std::fopen(path.c_str(),"rb");if(!f)return false;
 const bool ok=std::fread(b.data(),1,b.size(),f)==b.size()&&std::fgetc(f)==EOF;std::fclose(f);
 return ok&&!std::memcmp(b.data(),"MKWO0001",8)&&b[16]<4&&b[17]<2&&b[18]>=50&&b[18]<=100&&b[18]%10==0&&be32(b.data()+20)==optionsCrc(b);
}
void loadOptions(const char* root,unsigned slot,LicenseSave& r){
 if(!r.occupied)return;
 Options b{};const auto file=optionsPath(root,slot);
 if(!(readOptions(file,b)||readOptions(file+".bak",b))||!std::equal(r.miiId.begin(),r.miiId.end(),b.begin()+8))return;
 r.settings.ledMode=b[16];r.settings.dynamicShadows=b[17]!=0;r.settings.ledBrightness=b[18];
}
bool writeOptions(const char* root,unsigned slot,const LicenseSave& r,const unsigned char* identity){
 Options b{},old{};std::memcpy(b.data(),"MKWO0001",8);std::copy_n(identity,8,b.begin()+8);
 b[16]=r.occupied?r.settings.ledMode:0;b[17]=r.occupied?r.settings.dynamicShadows:true;b[18]=r.occupied?r.settings.ledBrightness:100;
 put32(b.data()+20,optionsCrc(b));const auto file=optionsPath(root,slot),tmp=file+".tmp",bak=file+".bak";
 if(readOptions(file,old)&&b==old)return true;
 FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
 bool ok=std::fwrite(b.data(),1,b.size(),f)==b.size();if(std::fflush(f))ok=false;if(std::fclose(f))ok=false;
 if(!ok){std::remove(tmp.c_str());return false;}
 if(exists(file)){if(exists(bak)&&std::remove(bak.c_str()))return false;if(std::rename(file.c_str(),bak.c_str()))return false;}
 if(std::rename(tmp.c_str(),file.c_str()))return false;
 // Erased preferences must not be resurrected from the old backup.
 if(!r.occupied&&exists(bak)&&std::remove(bak.c_str()))return false;
 return true;
}
void resetLicense(unsigned char* p){
 std::memset(p,0,LicenseSize);put16(p+0x80,0x5008);put16(p+0x82,0x1000);put16(p+0x84,0x5008);put16(p+0x86,0x1000);
 put16(p+0xb0,5000);put16(p+0xb2,5000);p[0xea]=0x80;
}
void initialize(Core& b){b.fill(0);std::memcpy(b.data(),"RKSD0006",8);std::memcpy(b.data()+0x23308,"RKGD",4);for(unsigned i=0;i<4;++i)resetLicense(b.data()+8+i*LicenseSize);}
std::uint32_t time(const unsigned char* p){
 if(!(p[0x50]&0x80))return 0;
 const auto t=be32(p+0x4c);const unsigned m=t>>25,s=(t>>18)&127,ms=(t>>8)&1023;
 return s<60&&ms<1000?(m*60+s)*1000+ms:0;
}
void decode(const unsigned char* p,unsigned slot,LicenseSave& r){
 r={};r.settings.license=slot;r.occupied=!std::memcmp(p,"RKPD",4);if(!r.occupied)return;
 for(unsigned i=0;i<r.name.size();++i)r.name[i]=be16(p+0x14+2*i);
 std::copy_n(p+0x28,r.miiId.size(),r.miiId.begin());
 for(unsigned i=0;i<r.unlockFlags.size();++i)r.unlockFlags[i]=be32(p+0x30+4*i);
 const unsigned rules=be16(p+0x80);auto& s=r.settings;
 s.vsClass=rules>>14;s.vsCpu=(rules>>12)&3;s.vsVehicles=(rules>>10)&3;s.vsCourses=(rules>>8)&3;s.vsItems=(rules>>6)&3;
 const unsigned races=(rules>>2)&15;s.vsRaces=races<std::size(UiVsRaceCounts)?UiVsRaceCounts[races]:4;
 s.vsCpu=std::min(s.vsCpu,2u);s.vsVehicles=std::min(s.vsVehicles,2u);s.vsCourses=std::min(s.vsCourses,2u);
 // Retail rule slots: single-player VS, single-player Battle, multiplayer
 // VS, multiplayer Battle. 8083bddc selects slot1 for offline one-player Battle.
 const unsigned battle=be16(p+0x82),machine=(battle>>10)&3,wins=(battle>>2)&15;
 s.battleCpu=std::min((battle>>12)&3,2u);
 // Retail Machine0/1/2 = all/karts/bikes; the existing UI uses karts/bikes/all.
 s.battleVehicles=machine<3?(machine+2)%3:2;
 s.battleCourses=std::min((battle>>8)&3,2u);s.battleItems=std::min((battle>>6)&3,2u);
 s.battleWins=wins<std::size(UiBattleWinCounts)?UiBattleWinCounts[wins]:2;
 s.automatic=(p[0xea]>>6)==2;
 // Retail stores aggregate counters, not separate GP/VS/TT counters.
 r.records.finishes[0]=be32(p+0xb4);r.records.firstPlaces[0]=be32(p+0xdc);
 auto& records=r.records;
 records.battles=be32(p+0xb8);records.wheelRaces=be32(p+0xbc);records.wheelBattles=be32(p+0xc0);
 records.tricks=be32(p+0xd8);records.hitsDelivered=be32(p+0xd0);records.hitsReceived=be32(p+0xd4);
 records.ghostWins=be32(p+0xa8);records.ghostLosses=be32(p+0xac);records.tournaments=be16(p+0xe8);
 for(unsigned i=0;i<10;++i)records.battleStages[i]=be16(p+0x1a6+2*BattleStage[i]);
 for(unsigned i=0;i<24;++i)r.records.characters[i]=be16(p+0xec+2*StarterDrivers[i].character);
 for(unsigned i=0;i<36;++i)r.records.vehicles[i]=be16(p+0x11e +2*i);
 for(unsigned i=0;i<32;++i){
  r.records.courses[i]=be16(p+0x166+2*RaceCourse[i]);r.times[i]=time(p+0xdc0+0x60*SaveCourse[i]);
  const auto* c=p+0x1c0+0x60*i;
  if(c[0x52]&0x80){const unsigned rank=c[0x51]&15;r.cups[i]=((c[0x4f]>>6)+1)|((rank<8?rank+1:0)<<8);}
 }
}
template<class A>std::uint32_t sum(const A& a){std::uint64_t n=0;for(auto v:a)n+=v;return std::min(n,std::uint64_t(0xffffffff));}
bool valid(const LicenseSave& r,unsigned slot){
 for(auto c:r.cups)if(c&&(!(c&255)||(c&255)>12||(c>>8)>8))return false;
 const auto& s=r.settings;return slot<4&&s.license==slot&&s.ledMode<4&&s.ledBrightness>=50&&s.ledBrightness<=100&&s.ledBrightness%10==0&&s.driver<26&&s.vehicle<12&&s.vsClass<4&&s.vsCpu<3&&s.vsVehicles<3&&s.vsCourses<3&&s.vsItems<4&&std::find(std::begin(UiVsRaceCounts),std::end(UiVsRaceCounts),s.vsRaces)!=std::end(UiVsRaceCounts)
  &&s.battleCpu<3&&s.battleVehicles<3&&s.battleCourses<3&&s.battleItems<3
  &&std::find(std::begin(UiBattleWinCounts),std::end(UiBattleWinCounts),s.battleWins)!=std::end(UiBattleWinCounts);}
void encode(unsigned char* p,unsigned slot,const LicenseSave& r){
 if(!r.occupied){resetLicense(p);return;}
 LicenseSave previous;decode(p,slot,previous);
 if(!previous.occupied){
  resetLicense(p);std::memcpy(p,"RKPD",4);
  const std::string name="License "+std::to_string(slot+1);for(unsigned i=0;i<name.size();++i)put16(p+0x14+i*2,name[i]);
  // Local fallback identity; no online account or foreign Mii is fabricated.
  put32(p+0x28,0x80000000u+slot+1);
 }
 if(previous.occupied?r.name!=previous.name:std::any_of(r.name.begin(),r.name.end(),[](auto c){return c!=0;}))
  for(unsigned i=0;i<r.name.size();++i)put16(p+0x14+2*i,r.name[i]);
 if(previous.occupied?r.miiId!=previous.miiId:std::any_of(r.miiId.begin(),r.miiId.end(),[](auto c){return c!=0;}))
  std::copy(r.miiId.begin(),r.miiId.end(),p+0x28);
 for(unsigned i=0;i<r.unlockFlags.size();++i)
  if(r.unlockFlags[i]!=previous.unlockFlags[i])put32(p+0x30+4*i,r.unlockFlags[i]);
 const auto& s=r.settings;const unsigned raceIndex=std::find(std::begin(UiVsRaceCounts),std::end(UiVsRaceCounts),s.vsRaces)-std::begin(UiVsRaceCounts);
 if(!previous.occupied||s.vsClass!=previous.settings.vsClass||s.vsCpu!=previous.settings.vsCpu||s.vsVehicles!=previous.settings.vsVehicles||s.vsCourses!=previous.settings.vsCourses||s.vsItems!=previous.settings.vsItems||s.vsRaces!=previous.settings.vsRaces)
  put16(p+0x80,(s.vsClass<<14)|(s.vsCpu<<12)|(s.vsVehicles<<10)|(s.vsCourses<<8)|(s.vsItems<<6)|(raceIndex<<2)|(be16(p+0x80)&3));
 // Change only the requested fields, preserving the unused Battle class bits,
 // low reserved bits and untouched malformed/unknown fields in imported saves.
 unsigned battle=be16(p+0x82);const auto& old=previous.settings;
 auto field=[&](unsigned value,unsigned before,unsigned raw,unsigned shift,unsigned mask){
  if(!previous.occupied||value!=before)battle=(battle&~(mask<<shift))|((raw&mask)<<shift);
 };
 field(s.battleCpu,old.battleCpu,s.battleCpu,12,3);
 field(s.battleVehicles,old.battleVehicles,(s.battleVehicles+1)%3,10,3);
 field(s.battleCourses,old.battleCourses,s.battleCourses,8,3);
 field(s.battleItems,old.battleItems,s.battleItems,6,3);
 const unsigned winIndex=std::find(std::begin(UiBattleWinCounts),std::end(UiBattleWinCounts),s.battleWins)-std::begin(UiBattleWinCounts);
 field(s.battleWins,old.battleWins,winIndex,2,15);put16(p+0x82,battle);
 if(!previous.occupied||s.automatic!=previous.settings.automatic)p[0xea]=(p[0xea]&63)|((s.automatic?2:1)<<6);
 put32(p+0xb4,sum(r.records.finishes));put32(p+0xdc,sum(r.records.firstPlaces));
 const auto& records=r.records;
 put32(p+0xb8,records.battles);put32(p+0xbc,records.wheelRaces);put32(p+0xc0,records.wheelBattles);
 put32(p+0xd8,records.tricks);put32(p+0xd0,records.hitsDelivered);put32(p+0xd4,records.hitsReceived);
 put32(p+0xa8,records.ghostWins);put32(p+0xac,records.ghostLosses);put16(p+0xe8,records.tournaments);
 for(unsigned i=0;i<10;++i)put16(p+0x1a6+2*BattleStage[i],std::min<std::uint32_t>(records.battleStages[i],65535u));
 for(unsigned i=0;i<24;++i)put16(p+0xec+2*StarterDrivers[i].character,std::min<std::uint32_t>(r.records.characters[i],65535u));
 for(unsigned i=0;i<36;++i)put16(p+0x11e +2*i,std::min<std::uint32_t>(r.records.vehicles[i],65535u));
 for(unsigned i=0;i<32;++i){
  put16(p+0x166+2*RaceCourse[i],std::min<std::uint32_t>(r.records.courses[i],65535u));
  if(r.cups[i]!=previous.cups[i]&&r.cups[i]){
   auto* c=p+0x1c0+0x60*i;const unsigned place=r.cups[i]&255,grade=r.cups[i]>>8;
   c[0x4f]=(c[0x4f]&63)|(std::min(place-1,3u)<<6);if(grade)c[0x51]=(c[0x51]&0xf0)|std::min(grade-1,7u);c[0x52]|=0x80;
  }
  if(r.times[i]&&r.times[i]!=previous.times[i]&&(!previous.times[i]||r.times[i]<previous.times[i])&&r.times[i]<128u*60000){
   auto* entry=p+0xdc0+0x60*SaveCourse[i];
   // Keep the displaced top four records and all their Mii metadata.
   for(unsigned rank=4;rank>0;--rank)std::memcpy(entry+rank*0xc00,entry+(rank-1)*0xc00,0x60);
   // Use an existing StoreData when available; Mii creation remains separate.
   std::memset(entry+0x4c,0,0x14);const unsigned ms=r.times[i],types[]={0,1,2,6,7,8,3,4,5,9,10,11};
   const unsigned driver=std::min(s.driver,23u),vehicle=types[s.vehicle]*3+StarterDrivers[driver].vehicle;
   put32(entry+0x4c,((ms/60000)<<25)|(((ms/1000)%60)<<18)|((ms%1000)<<8)|(vehicle<<2));
   entry[0x50]=0x80|StarterDrivers[driver].character;entry[0x51]=2<<5;
   // New PB has no matching recorded ghost. Keep its bytes, clear availability.
   put32(p+4,be32(p+4)&~(1u<<SaveCourse[i]));
  }
 }
}
}
std::vector<LicenseUnlockNotice> LicenseSave::availableUnlocks() const{
 std::vector<LicenseUnlockNotice> notices;
 if(!occupied)return notices;
 std::array<bool,61> conditions{};
 const auto grant=[&](unsigned condition,bool achieved){conditions[condition]=achieved;};
 const auto place=[&](unsigned cc,unsigned cup,unsigned maximum){const auto p=cups[cc*8+cup]&255;return p&&p<=maximum;};
 const auto stars=[&](unsigned cc,unsigned first,unsigned count){
  for(unsigned c=first;c<first+count;++c){const auto r=cups[cc*8+c];if(!(r&255)||!(r>>8)||(r>>8)>3)return false;}return true;
 };
 const auto gold=[&](unsigned cc){for(unsigned c=0;c<8;++c)if(!place(cc,c,1))return false;return true;};
 // Condition IDs preserve retail Unlock::OnActivate priority, including
 // different explanations for alternative conditions of the same reward.
 for(unsigned cc=0;cc<4;++cc){
  grant(cc,place(cc,0,3)&&place(cc,1,3));grant(4+cc,place(cc,2,3));
  grant(8+cc,place(cc,4,3)&&place(cc,5,3));grant(12+cc,place(cc,6,3));
 }
 grant(16,gold(2));
 grant(17,place(1,6,1));grant(20,stars(0,0,4));grant(22,place(0,7,1));
 grant(23,stars(1,4,4));grant(24,place(2,3,1));grant(28,stars(2,0,4));
 grant(31,place(0,2,1));grant(26,stars(3,0,8));grant(32,place(1,3,1));
 grant(37,stars(0,4,4));grant(40,place(3,7,1));grant(41,place(0,6,1));
 grant(42,place(2,6,1));grant(44,place(0,3,1));grant(45,stars(2,4,4));
 grant(46,place(2,7,1));grant(47,place(2,2,1));grant(50,place(3,6,1));
 grant(51,place(1,7,1));grant(52,stars(1,0,4));grant(53,place(3,1,1));
 grant(54,place(1,2,1));grant(57,place(3,3,1));
 grant(58,place(0,3,3)&&place(0,7,3));grant(59,place(1,3,3)&&place(1,7,3));
 unsigned timeCourses=0;for(auto time:times)timeCourses+=time!=0;
 grant(18,timeCourses==32);grant(25,timeCourses>=16);grant(48,timeCourses>=8);
 // USA 8085ea98, SetTitleBMG 808592a4. Unsupported ghost/online/
 // Galaxy conditions remain false rather than selecting their messages.
 constexpr unsigned bits[]={14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,52,
  0,1,1,2,3,4,5,6,7,11,11,8,10,10,9,12,13,13,3,7,34,35,35,36,37,38,39,
  40,41,42,43,44,44,45,46,47,48,49,50,50,51,30,31,39};
 static_assert(std::size(bits)==61, "One completion bit per retail unlock condition");
 auto completed=unlockFlags;
 for(unsigned id=0;id<conditions.size();++id){
  const unsigned bit=bits[id];
  if(!conditions[id]||licenseCompleted(completed,bit))continue;
  const unsigned message=id<16?0x1fa4+id/4:id==16?0x1fa8:id<37?id+0x1f9d:id+0x1fa7;
  notices.push_back({bit,id,message,id<16?0xd53+id%4:0,0});
  completed[bit/32]|=std::uint32_t(1)<<(bit%32);
 }
 // USA 8085e884/8085e968: race + battle count, each capped at 99999.
 // This preserves the retail alternative for rewards involving ghost modes.
 constexpr int races[]={1050,2550,1950,3150,450,3450,2850,1350,4350,750,2250,4950,1650,5100,
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 1800,1200,4200,300,2700,4650,600,4500,3000,2400,900,3900,2100,3300,3750,1500,3600,4050,4800};
 const auto count=std::min<std::uint32_t>(sum(records.finishes),99999u)+std::min<std::uint32_t>(records.battles,99999u);
 for(unsigned bit=0;bit<std::size(races);++bit){
  if(races[bit]<0||count<unsigned(races[bit])||licenseCompleted(completed,bit))continue;
  notices.push_back({bit,61,0x1fe4u,0,unsigned(races[bit])});
 }
 return notices;
}
bool LicenseSave::refreshUnlocks(){
 const auto notices=availableUnlocks();
 for(const auto& notice:notices)unlockFlags[notice.completionBit/32]|=std::uint32_t(1)<<(notice.completionBit%32);
 return !notices.empty();
}
void LicenseSave::cupResult(unsigned cc,unsigned cup,unsigned place,int grade){
 if(cc>=4||cup>=8||place<1||place>12||grade< -1||grade>7)return;
 auto& value=cups[cc*8+cup];
 const unsigned oldPlace=value&255,oldGrade=value>>8;
 value=(oldPlace?std::min(oldPlace,place):place)|((grade<0?oldGrade:oldGrade?std::min(oldGrade,unsigned(grade+1)):unsigned(grade+1))<<8);
}
LicenseLoad loadLicenseSaves(const char* root,std::array<LicenseSave,4>& saves){
 for(unsigned slot=0;slot<saves.size();++slot){saves[slot]={};saves[slot].settings.license=slot;}
 auto b=std::make_unique<Core>();const std::string file=std::string(root)+"/rksys.dat";
 if(read(file,*b)||read(file+".bak",*b)){
  for(unsigned slot=0;slot<saves.size();++slot){
   auto& r=saves[slot];decode(b->data()+8+slot*LicenseSize,slot,r);loadOptions(root,slot,r);
   if(r.occupied&&!loadNativeMii(root,r.miiId,r.nativeMii))r.miiId={};
  }
  return LicenseLoad::Loaded;
 }
 return exists(file)||exists(file+".bak")?LicenseLoad::Damaged:LicenseLoad::Missing;
}
LicenseLoad loadLicenseSave(const char* root,unsigned slot,LicenseSave& r){
 r={};r.settings.license=slot;if(slot>=4)return LicenseLoad::Damaged;
 auto b=std::make_unique<Core>();const std::string file=std::string(root)+"/rksys.dat";
 if(read(file,*b)||read(file+".bak",*b)){decode(b->data()+8+slot*LicenseSize,slot,r);loadOptions(root,slot,r);
  // Imported Wii IDs have no native companion. Discard only that association
  // in memory; the menu requires native selection before opening the license.
  // Reading a save never rewrites its source file or removes Mii sidecars.
  if(r.occupied&&!loadNativeMii(root,r.miiId,r.nativeMii))r.miiId={};
  return LicenseLoad::Loaded;}
 return exists(file)||exists(file+".bak")?LicenseLoad::Damaged:LicenseLoad::Missing;
}
bool writeLicenseSave(const char* root,unsigned slot,const LicenseSave& r){
 if(!valid(r,slot))return false;
 const std::string file=std::string(root)+"/rksys.dat",backup=file+".bak",temporary=file+".tmp";
 auto b=std::make_unique<Core>();std::string source;
 if(read(file,*b))source=file;else if(read(backup,*b))source=backup;
 else if(exists(file)||exists(backup))return false;else initialize(*b);
 // Persist appearance before publishing a license that references it. A
 // failed core save may leave an unreferenced Mii, never a partial license.
 if(r.occupied&&r.nativeMii[0]&&(licenseMiiId(r.nativeMii)!=r.miiId||!writeNativeMii(root,r.nativeMii)))return false;
 auto old=std::make_unique<std::array<unsigned char,LicenseSize>>();std::memcpy(old->data(),b->data()+8+slot*LicenseSize,LicenseSize);
 encode(b->data()+8+slot*LicenseSize,slot,r);
 if(source==file&&!std::memcmp(old->data(),b->data()+8+slot*LicenseSize,LicenseSize))return writeOptions(root,slot,r,b->data()+8+slot*LicenseSize+0x28);
 put32(b->data()+CoreSize-4,crc(*b));
 FILE* in=source.empty()?nullptr:std::fopen(source.c_str(),"rb");if(!source.empty()&&!in)return false;
 if(in)std::setvbuf(in,nullptr,_IONBF,0);
 if(in&&std::fseek(in,CoreSize,SEEK_SET)){std::fclose(in);return false;}
 FILE* out=std::fopen(temporary.c_str(),"wb");if(!out){if(in)std::fclose(in);return false;}
 // newlib's small default buffer fragments each run into SD commands.
 std::setvbuf(out,nullptr,_IONBF,0);
 bool ok=std::fwrite(b->data(),1,b->size(),out)==b->size();
 // Preserve every ghost/unknown byte and the atomic tmp/backup publication,
 // but copy in SD-friendly 128 KiB runs rather than ~660 tiny 4 KiB writes.
 constexpr unsigned ChunkSize=128*1024;
 auto chunk=std::make_unique<unsigned char[]>(ChunkSize);
 for(unsigned offset=CoreSize;ok&&offset<FileSize;offset+=ChunkSize){
  const unsigned n=std::min(ChunkSize,FileSize-offset);
  if(in&&std::fread(chunk.get(),1,n,in)!=n){ok=false;break;}
  // Erasing a license also erases its ghosts, without touching other slots.
  if(!r.occupied){
   const unsigned begin=std::max(offset,CoreSize+slot*GhostSize);
   const unsigned end=std::min(offset+n,CoreSize+(slot+1)*GhostSize);
   if(end>begin)std::fill_n(chunk.get()+begin-offset,end-begin,0);
  }
  ok=std::fwrite(chunk.get(),1,n,out)==n;
 }
 if(in)std::fclose(in);
 if(std::fflush(out))ok=false;
 if(std::fclose(out))ok=false;
 if(!ok){std::remove(temporary.c_str());return false;}
 if(source==file){if(exists(backup)&&std::remove(backup.c_str()))return false;if(std::rename(file.c_str(),backup.c_str()))return false;}
 else if(exists(file)&&std::remove(file.c_str()))return false;
 if(std::rename(temporary.c_str(),file.c_str()))return false;
 if(!r.occupied&&exists(backup)&&std::remove(backup.c_str()))return false;
 return writeOptions(root,slot,r,b->data()+8+slot*LicenseSize+0x28);
}
LicenseUnlockActivation activateNextLicenseUnlock(const char* root,unsigned slot,
 LicenseSave& save,LicenseUnlockNotice& notice){
 if(slot>=4||save.settings.license!=slot)return LicenseUnlockActivation::SaveFailed;
 const auto available=save.availableUnlocks();
 if(available.empty())return LicenseUnlockActivation::None;
 // Retail OnActivate sets the completion bit before preparing its controls.
 // Keep the reward eligible on a failed SD write, rather than hiding the
 // notification and losing the unlock when the license is read again.
 auto candidate=save;
 const auto& next=available.front();
 candidate.unlockFlags[next.completionBit/32]|=std::uint32_t(1)<<(next.completionBit%32);
 if(!writeLicenseSave(root,slot,candidate))return LicenseUnlockActivation::SaveFailed;
 save.unlockFlags=candidate.unlockFlags;
 notice=next;
 return LicenseUnlockActivation::Ready;
}
}
