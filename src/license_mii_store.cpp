#include <mkw/license_identity.hpp>
#include <algorithm>
#include <cstdio>
#include <string>
#include <cerrno>
#include <sys/stat.h>

namespace mkw {
namespace {
std::uint32_t checksum(const NativeMii& m){
 std::uint32_t crc=~0u;
 for(auto c:m){crc^=c;for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&-(crc&1));}
 return ~crc;
}
std::string path(const char* root,const LicenseMiiId& id){
 std::string p=std::string(root)+"/mii-";
 constexpr char hex[]="0123456789abcdef";
 for(auto c:id){p+=hex[c>>4];p+=hex[c&15];}
 return p+".3dsmii";
}
}
LicenseMiiId licenseMiiId(const NativeMii& m){
 // IDs are opaque big-endian bytes even in the little-endian native struct.
 const std::uint32_t native=(std::uint32_t(m[12])<<24)|(std::uint32_t(m[13])<<16)|(std::uint32_t(m[14])<<8)|m[15];
 const std::uint32_t ticks=31557600u+((native&0x0fffffffu)>>1);
 const std::uint32_t legacy=(native&0x80000000u?0x80000000u:0x40000000u)|ticks;
 return {std::uint8_t(legacy>>24),std::uint8_t(legacy>>16),std::uint8_t(legacy>>8),std::uint8_t(legacy),m[4],m[5],m[6],m[7]};
}
static bool readNativeMii(const std::string& file,const LicenseMiiId& id,NativeMii& out){
 FILE* f=std::fopen(file.c_str(),"rb");if(!f)return false;
 std::array<unsigned char,8> header{};NativeMii candidate{};unsigned char stored[4]{};
 bool ok=std::fread(header.data(),1,header.size(),f)==header.size()&&
  std::fread(candidate.data(),1,candidate.size(),f)==candidate.size()&&
  std::fread(stored,1,4,f)==4&&std::fgetc(f)==EOF&&!std::ferror(f);
 std::fclose(f);
 const std::array<unsigned char,8> magic={'M','K','W','M','I','I',1,0};
 const auto crc=checksum(candidate);
 ok=ok&&header==magic&&candidate[0]==3&&licenseMiiId(candidate)==id&&
  stored[0]==(crc>>24)&&stored[1]==((crc>>16)&255)&&stored[2]==((crc>>8)&255)&&stored[3]==(crc&255);
 if(ok)out=candidate;
 return ok;
}
bool loadNativeMii(const char* root,const LicenseMiiId& id,NativeMii& out){
 const auto file=path(root,id);
 return readNativeMii(file,id,out)||readNativeMii(file+".bak",id,out);
}
bool writeNativeMii(const char* root,const NativeMii& m){
 if(m[0]!=3)return false;
 const auto id=licenseMiiId(m);NativeMii previous;
 const auto file=path(root,id),temporary=file+".tmp",backup=file+".bak";
 const bool validPrimary=readNativeMii(file,id,previous);
 if(validPrimary&&previous==m)return true;
 FILE* f=std::fopen(temporary.c_str(),"wb");if(!f)return false;
 const unsigned char magic[]={'M','K','W','M','I','I',1,0};const auto crc=checksum(m);
 const unsigned char stored[]={std::uint8_t(crc>>24),std::uint8_t(crc>>16),std::uint8_t(crc>>8),std::uint8_t(crc)};
 bool ok=std::fwrite(magic,1,sizeof magic,f)==sizeof magic&&std::fwrite(m.data(),1,m.size(),f)==m.size()&&std::fwrite(stored,1,4,f)==4;
 if(std::fflush(f))ok=false;if(std::fclose(f))ok=false;
 if(!ok){std::remove(temporary.c_str());return false;}
 // Publish only to an absent destination. Keep the last valid appearance
 // recoverable across a failed rename or interruption, as for rksys.dat.
 struct stat info{};bool moved=false;
 if(!::stat(file.c_str(),&info)){
  if(validPrimary){
   if(std::remove(backup.c_str())&&errno!=ENOENT){std::remove(temporary.c_str());return false;}
   if(std::rename(file.c_str(),backup.c_str())){std::remove(temporary.c_str());return false;}
   moved=true;
  }else if(std::remove(file.c_str())){std::remove(temporary.c_str());return false;}
 }else if(errno!=ENOENT){std::remove(temporary.c_str());return false;}
 if(std::rename(temporary.c_str(),file.c_str())){
  if(moved)std::rename(backup.c_str(),file.c_str());
  std::remove(temporary.c_str());return false;
 }
 return true;
}
}
