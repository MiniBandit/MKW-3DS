#include <mkw/license_times.hpp>
#include <cstdio>
#include <cstring>
#include <string>

namespace mkw {
namespace {
using Bytes=std::array<unsigned char,140>;
std::string path(const char* root,unsigned slot){return std::string(root)+"/license-"+std::to_string(slot)+"-times.bin";}
void put(Bytes& b,unsigned p,std::uint32_t n){for(unsigned i=0;i<4;++i)b[p+i]=n>>(8*i);}
std::uint32_t get(const Bytes& b,unsigned p){std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=std::uint32_t(b[p+i])<<(8*i);return n;}
std::uint32_t checksum(const Bytes& b){std::uint32_t h=2166136261u;for(unsigned i=0;i<136;++i)h=(h^b[i])*16777619u;return h;}
bool exists(const std::string& p){FILE* f=std::fopen(p.c_str(),"rb");if(!f)return false;std::fclose(f);return true;}
bool read(const std::string& p,unsigned slot,CourseBestTimes& times){
 FILE* f=std::fopen(p.c_str(),"rb");if(!f)return false;
 Bytes b{};const bool full=std::fread(b.data(),1,b.size(),f)==b.size();
 const bool end=std::fgetc(f)==EOF&&!std::ferror(f);std::fclose(f);
 if(!full||!end||std::memcmp(b.data(),"MKWT",4)||b[4]!=1||b[5]!=slot||b[6]||b[7]||get(b,136)!=checksum(b))return false;
 for(unsigned i=0;i<32;++i)times[i]=get(b,8+4*i);
 return true;
}
}
bool loadLicenseTimes(const char* root,unsigned slot,CourseBestTimes& times){
 times={};if(slot>=4)return false;
 const auto file=path(root,slot);
 if(read(file,slot,times)||read(file+".bak",slot,times))return true;
 // A damaged new-format record must not silently resurrect older legacy data.
 if(slot||exists(file)||exists(file+".bak"))return false;
 FILE* f=std::fopen((std::string(root)+"/best-times.bin").c_str(),"rb");if(!f)return false;
 std::array<unsigned char,128> bytes{};
 const bool full=std::fread(bytes.data(),1,bytes.size(),f)==bytes.size();
 const bool end=std::fgetc(f)==EOF&&!std::ferror(f);std::fclose(f);
 if(!full||!end)return false;
 for(unsigned i=0;i<32;++i)for(unsigned j=0;j<4;++j)times[i]|=std::uint32_t(bytes[4*i+j])<<(8*j);
 return true;
}
bool saveLicenseTimes(const char* root,unsigned slot,const CourseBestTimes& times){
 if(slot>=4)return false;
 const auto file=path(root,slot),temporary=file+".tmp",backup=file+".bak";
 Bytes b{};std::memcpy(b.data(),"MKWT",4);b[4]=1;b[5]=slot;
 for(unsigned i=0;i<32;++i)put(b,8+4*i,times[i]);put(b,136,checksum(b));
 FILE* f=std::fopen(temporary.c_str(),"wb");if(!f)return false;
 bool ok=std::fwrite(b.data(),1,b.size(),f)==b.size();
 if(std::fflush(f))ok=false;if(std::fclose(f))ok=false;
 if(!ok){std::remove(temporary.c_str());return false;}
 CourseBestTimes previous{};
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
