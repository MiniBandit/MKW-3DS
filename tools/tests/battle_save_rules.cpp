#include <mkw/license_save.hpp>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>
namespace fs=std::filesystem;
using Bytes=std::vector<unsigned char>;
Bytes read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void write(const fs::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());assert(f.good());}
unsigned half(const Bytes& b,unsigned p){return unsigned(b[p])<<8|b[p+1];}
void checksum(Bytes& b){unsigned crc=~0u;for(unsigned i=0;i<0x27ffc;++i){crc^=b[i];for(unsigned k=0;k<8;++k)crc=(crc>>1)^(0xedb88320u&-(crc&1));}crc=~crc;for(unsigned i=0;i<4;++i)b[0x27ffc+i]=crc>>(24-8*i);}
void unchanged(const Bytes& before,const Bytes& after,unsigned slot){
 assert(before.size()==after.size());const unsigned at=8+slot*0x8cc0+0x82;
 for(unsigned i=0;i<before.size();++i)if(i!=at&&i!=at+1&&!(i>=0x27ffc&&i<0x28000))assert(before[i]==after[i]);
}
int main(){
 char root[]="/tmp/mkw-battle-save-XXXXXX";assert(mkdtemp(root));fs::path file=fs::path(root)/"rksys.dat";
 for(unsigned slot=0;slot<4;++slot){mkw::LicenseSave s;s.occupied=true;s.settings.license=slot;assert(mkw::writeLicenseSave(root,slot,s));}
 auto fixture=read(file);
 // Imported reserved bits, unsupported fields, other three rule words and
 // every ghost byte must survive an unrelated or single-field edit.
 for(unsigned slot=0;slot<4;++slot){const unsigned base=8+slot*0x8cc0,at=base+0x82;fixture[at]=0xea;fixture[at+1]=0xfb;
  std::fill_n(fixture.begin()+base+0x28,8,0);}
 for(unsigned i=0x28000;i<fixture.size();++i)fixture[i]=static_cast<unsigned char>((i*37u+13u)&255u);
 checksum(fixture);write(file,fixture);
 for(unsigned slot=0;slot<4;++slot){mkw::LicenseSave s;assert(mkw::loadLicenseSave(root,slot,s)==mkw::LicenseLoad::Loaded);
  assert(s.settings.battleCpu==2&&s.settings.battleVehicles==1&&s.settings.battleCourses==2&&s.settings.battleItems==2&&s.settings.battleWins==2);
  assert(mkw::writeLicenseSave(root,slot,s));assert(read(file)==fixture);
 }
 mkw::LicenseSave s;assert(mkw::loadLicenseSave(root,0,s)==mkw::LicenseLoad::Loaded);
 s.settings.battleCpu=1;assert(mkw::writeLicenseSave(root,0,s));auto updated=read(file);assert(half(updated,8+0x82)==0xdafb);unchanged(fixture,updated,0);
 s.settings.battleWins=10;assert(mkw::writeLicenseSave(root,0,s));auto next=read(file);assert(half(next,8+0x82)==0xdad7);unchanged(updated,next,0);
 struct Case{unsigned cpu,vehicle,course,items,wins,raw;};
 // Literal retail words use All0/Karts1/Bikes2, not the UI enumeration.
 const Case cases[]={{0,0,0,0,2,0xc403},{1,1,1,1,3,0xd947},{2,2,2,2,4,0xe28b},
                     {0,0,0,0,5,0xc40f},{1,1,1,1,8,0xd953},{2,2,2,2,10,0xe297}};
 for(const auto& c:cases){auto before=read(file);auto& v=s.settings;v.battleCpu=c.cpu;v.battleVehicles=c.vehicle;v.battleCourses=c.course;v.battleItems=c.items;v.battleWins=c.wins;
  assert(mkw::writeLicenseSave(root,0,s));auto after=read(file);assert(half(after,8+0x82)==c.raw);unchanged(before,after,0);
  mkw::LicenseSave restored;assert(mkw::loadLicenseSave(root,0,restored)==mkw::LicenseLoad::Loaded);const auto& r=restored.settings;
  assert(r.battleCpu==c.cpu&&r.battleVehicles==c.vehicle&&r.battleCourses==c.course&&r.battleItems==c.items&&r.battleWins==c.wins);
 }
 const auto before=read(file);for(unsigned field=0;field<5;++field){auto bad=s;
  switch(field){case 0:bad.settings.battleCpu=3;break;case 1:bad.settings.battleVehicles=3;break;case 2:bad.settings.battleCourses=3;break;case 3:bad.settings.battleItems=3;break;case 4:bad.settings.battleWins=7;break;}
  assert(!mkw::writeLicenseSave(root,0,bad));assert(read(file)==before);
 }
 fs::remove_all(root);puts("PASS Battle RKSD slot1 native fields, machine mapping, six win targets, lossless unknown fields, four licenses/ghost preservation, CRC reload and invalid atomic refusal; target menu flow pending");
}
