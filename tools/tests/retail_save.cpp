#include <mkw/license_save.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <cstdio>
namespace fs=std::filesystem;
std::vector<unsigned char> bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
unsigned word(const std::vector<unsigned char>& b,unsigned p){return unsigned(b[p])<<24|unsigned(b[p+1])<<16|unsigned(b[p+2])<<8|b[p+3];}
int main(int argc,char** argv){
 assert(argc==2);char root[]="/tmp/mkw-retail-save-XXXXXX";assert(mkdtemp(root));fs::path dir=root;
 fs::copy_file(argv[1],dir/"rksys.dat");auto original=bytes(dir/"rksys.dat");
 // Native saves require their identity sidecars just as the game does.
 for(const auto& entry:fs::directory_iterator(fs::path(argv[1]).parent_path()))
  if(entry.is_regular_file()&&entry.path().extension()==".3dsmii")
   fs::copy_file(entry.path(),dir/entry.path().filename());
 for(unsigned i=0;i<4;++i){mkw::LicenseSave s;assert(mkw::loadLicenseSave(root,i,s)==mkw::LicenseLoad::Loaded);assert(mkw::writeLicenseSave(root,i,s));assert(bytes(dir/"rksys.dat")==original);}
 mkw::LicenseSave a,b;assert(mkw::loadLicenseSave(root,0,a)==mkw::LicenseLoad::Loaded&&a.occupied);
 for(unsigned i=0;i<4;++i)assert(a.unlockFlags[i]==word(original,8+0x30+4*i));
 // Exercise all four raw words without altering identity, records or other slots.
 const auto oldFlags=a.unlockFlags;
 a.unlockFlags={1u,0x80000000u,0x12345678u,0xabcdef01u};
 assert(mkw::writeLicenseSave(root,0,a));assert(mkw::loadLicenseSave(root,0,b)==mkw::LicenseLoad::Loaded);
 assert(a.unlockFlags==b.unlockFlags);
 const auto flagsOnly=bytes(dir/"rksys.dat");
 for(unsigned i=0;i<original.size();++i)
  if(!(i>=8+0x30&&i<8+0x40)&&!(i>=0x27ffc&&i<0x28000))assert(flagsOnly[i]==original[i]);
 fs::copy_file(argv[1],dir/"rksys.dat",fs::copy_options::overwrite_existing);a.unlockFlags=oldFlags;
 a.name={0x7231,0x82ae,0x20,0x0041,0x0062,0x0063,0x0031,0x0032,0x0033,0x0034};
 a.nativeMii[0]=3;a.nativeMii[12]=0x90;a.nativeMii[15]=24;a.nativeMii[26]=0x41;
 a.miiId=mkw::licenseMiiId(a.nativeMii);
 assert(mkw::writeLicenseSave(root,0,a));assert(mkw::loadLicenseSave(root,0,b)==mkw::LicenseLoad::Loaded);
 assert(a.name==b.name&&a.miiId==b.miiId&&a.nativeMii==b.nativeMii);
 const auto identityOnly=bytes(dir/"rksys.dat");
 for(unsigned i=0;i<original.size();++i)
  if(!(i>=8+0x14&&i<8+0x30)&&!(i>=0x27ffc&&i<0x28000))assert(identityOnly[i]==original[i]);
 // Restore the fixture before exercising unrelated fields/backup semantics.
 fs::copy_file(argv[1],dir/"rksys.dat",fs::copy_options::overwrite_existing);
 const unsigned testTime=a.times[31]?std::min(123456u,a.times[31]-1):123456u;
 a.records.hitsDelivered+=3;a.records.tricks+=2;a.records.battleStages[2]+=1;a.records.tournaments+=1;
 a.records.finishes[1]++;a.records.characters[22]++;a.records.courses[10]++;a.cupResult(3,7,2,1);a.settings.automatic=!a.settings.automatic;a.times[31]=testTime;
 assert(mkw::writeLicenseSave(root,0,a));assert(mkw::loadLicenseSave(root,0,b)==mkw::LicenseLoad::Loaded);
 assert(b.name==a.name&&b.miiId==a.miiId);
 assert(b.records.hitsDelivered==a.records.hitsDelivered&&b.records.tricks==a.records.tricks&&b.records.battleStages[2]==a.records.battleStages[2]&&b.records.tournaments==a.records.tournaments);
 assert(b.times[31]==testTime&&b.cups[31]==a.cups[31]&&b.settings.automatic==a.settings.automatic);
 auto updated=bytes(dir/"rksys.dat");assert(updated.size()==0x2bc000);
 assert(word(updated,8+0xb4)==word(original,8+0xb4)+1);
 assert(std::equal(original.begin()+8+0x8cc0,original.begin()+0x27ffc,updated.begin()+8+0x8cc0));
 assert(std::equal(original.begin()+0x28000,original.end(),updated.begin()+0x28000));
 assert(bytes(dir/"rksys.dat.bak")==original);
 {std::ofstream f(dir/"rksys.dat",std::ios::binary);f<<"broken";}
 assert(mkw::loadLicenseSave(root,0,b)==mkw::LicenseLoad::Loaded);assert(b.times[31]!=testTime);
 assert(mkw::writeLicenseSave(root,0,a));
 mkw::LicenseSave empty;assert(mkw::writeLicenseSave(root,0,empty));assert(mkw::loadLicenseSave(root,0,b)==mkw::LicenseLoad::Loaded&&!b.occupied);
 assert(!fs::exists(dir/"rksys.dat.bak"));
 {std::ofstream f(dir/"rksys.dat",std::ios::binary);f<<"broken";}
 assert(mkw::loadLicenseSave(root,0,b)==mkw::LicenseLoad::Damaged);assert(!mkw::writeLicenseSave(root,0,a));
 fs::remove(dir/"rksys.dat");a.settings.license=3;assert(mkw::writeLicenseSave(root,3,a));assert(mkw::loadLicenseSave(root,3,b)==mkw::LicenseLoad::Loaded&&b.occupied&&b.times[31]==testTime);
 auto fresh=bytes(dir/"rksys.dat");assert(fresh.size()==0x2bc000);assert(word(fresh,8+3*0x8cc0)==0x524b5044);
 fs::remove_all(dir);puts("Retail save: untouched roundtrip, field edits, other licenses/ghost preservation, CRC, backup recovery, erasure and new file passed");
}
