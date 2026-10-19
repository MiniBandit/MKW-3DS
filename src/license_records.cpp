#include <mkw/license_records.hpp>
#include <limits>
#include <cstdio>
#include <cstring>
#include <string>

namespace mkw {
namespace {
constexpr unsigned WordCount=3+2+24+36+32;
constexpr unsigned ChecksumOffset=8+4*WordCount;
using Bytes=std::array<unsigned char,ChecksumOffset+4>;
template<class F>void fields(LicenseRecords& r,F visit){for(auto& v:r.finishes)visit(v);for(auto& v:r.firstPlaces)visit(v);for(auto& v:r.characters)visit(v);for(auto& v:r.vehicles)visit(v);for(auto& v:r.courses)visit(v);}
std::string path(const char* root,unsigned slot){return std::string(root)+"/license-"+std::to_string(slot)+"-records.bin";}
void put(Bytes& b,unsigned p,std::uint32_t n){for(unsigned i=0;i<4;++i)b[p+i]=n>>(8*i);}
std::uint32_t get(const Bytes& b,unsigned p){std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=std::uint32_t(b[p+i])<<(8*i);return n;}
std::uint32_t checksum(const Bytes& b){std::uint32_t h=2166136261u;for(unsigned i=0;i<ChecksumOffset;++i)h=(h^b[i])*16777619u;return h;}
bool exists(const std::string& p){FILE* f=std::fopen(p.c_str(),"rb");if(!f)return false;std::fclose(f);return true;}
bool read(const std::string& p,unsigned slot,LicenseRecords& records){
 FILE* f=std::fopen(p.c_str(),"rb");if(!f)return false;
 Bytes b{};const bool full=std::fread(b.data(),1,b.size(),f)==b.size();
 const bool end=std::fgetc(f)==EOF&&!std::ferror(f);std::fclose(f);
 if(!full||!end||std::memcmp(b.data(),"MKWR",4)||b[4]!=1||b[5]!=slot||b[6]||b[7]||get(b,ChecksumOffset)!=checksum(b))return false;
 unsigned offset=8;fields(records,[&](auto& value){value=get(b,offset);offset+=4;});
 return true;
}
}
bool LicenseRecords::finish(unsigned mode,unsigned character,unsigned retailVehicle,unsigned course,unsigned rank){
 if(mode>=3||character>=characters.size()||retailVehicle>=vehicles.size()||course>=courses.size()||rank<1||rank>12)return false;
 auto increment=[](auto& value){if(value!=std::numeric_limits<std::uint32_t>::max())++value;};
 increment(finishes[mode]);if(mode<2&&rank==1)increment(firstPlaces[mode]);
 increment(characters[character]);increment(vehicles[retailVehicle]);increment(courses[course]);return true;
}
bool loadLicenseRecords(const char* root,unsigned slot,LicenseRecords& records){
 records={};if(slot>=4)return false;
 const auto file=path(root,slot);return read(file,slot,records)||read(file+".bak",slot,records);
}
bool saveLicenseRecords(const char* root,unsigned slot,const LicenseRecords& records){
 if(slot>=4)return false;
 const auto file=path(root,slot),temporary=file+".tmp",backup=file+".bak";
 Bytes b{};std::memcpy(b.data(),"MKWR",4);b[4]=1;b[5]=slot;
 unsigned offset=8;auto copy=records;fields(copy,[&](auto& value){put(b,offset,value);offset+=4;});put(b,ChecksumOffset,checksum(b));
 FILE* f=std::fopen(temporary.c_str(),"wb");if(!f)return false;
 bool ok=std::fwrite(b.data(),1,b.size(),f)==b.size();
 if(std::fflush(f))ok=false;if(std::fclose(f))ok=false;
 if(!ok){std::remove(temporary.c_str());return false;}
 LicenseRecords previous{};
 if(read(file,slot,previous)){
  // FAT rename cannot replace an existing destination. Keep a known-good
  // generation until the new file is installed, including across interruption.
  if(exists(backup)&&std::remove(backup.c_str()))return false;
  if(std::rename(file.c_str(),backup.c_str()))return false;
 }else if(exists(file)&&std::remove(file.c_str()))return false;
 if(std::rename(temporary.c_str(),file.c_str()))return false;
 return true;
}
}
