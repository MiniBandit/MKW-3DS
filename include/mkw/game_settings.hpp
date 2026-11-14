#pragma once
// Whole-game options from the Settings page (the license screen's corner
// button): sd:/3ds/MKWiiDS/settings.dat. Version 2, 20 bytes: "MKWS0002",
// the five values, three spare bytes, then a CRC-32 of the first 16 bytes.
// Version 1 (16 bytes: "MKWS0001", four values, a CRC-32 of the first 12) is
// still read, with 800px Mode off.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace mkw {
struct UiSettings {
 // 0 High: every texture at its cooked size. 1 Low: textures of
 // 256x256 texels or more load from their first mip level (Texture::loadMemory).
 unsigned textureQuality=0;
 bool dynamicShadows=false,bloom=false;
 // 0 Full: every opponent keeps full detail. 1 Low: opponents switch to
 // the Wii's split-screen bodies with camera distance (RenderQuality::racerLod).
 unsigned opponentDetail=0;
 // 800px Mode (New 3DS family): the top screen shows 800 columns while the
 // 3D slider is down, and 3D's two 400-column eyes while it is up.
 bool wide800=false;
 bool operator==(const UiSettings&)const=default;
};
// Old 3DS (268 MHz, one core, 43 MB heap): every option stays at its light
// value (MenuState::old3dsSettings refuses to switch them). Bloom too: its ~20
// blur passes cost about 1 ms of CPU a frame there (user, Nov 6).
inline void lockOld3dsSettings(UiSettings& s){s.textureQuality=1;s.dynamicShadows=false;s.bloom=false;s.opponentDetail=1;s.wide800=false;}
namespace settings_file {
inline std::uint32_t crc(const unsigned char* b,unsigned n){
 std::uint32_t c=~0u;
 for(unsigned i=0;i<n;++i){c^=b[i];for(unsigned k=0;k<8;++k)c=(c>>1)^(0xedb88320u&-(c&1));}
 return ~c;
}
inline bool read(const char* path,UiSettings& out){
 std::array<unsigned char,20> b{};
 FILE* f=std::fopen(path,"rb");if(!f)return false;
 const size_t n=std::fread(b.data(),1,b.size(),f);const bool end=std::fgetc(f)==EOF;std::fclose(f);
 const bool v2=n==20&&!std::memcmp(b.data(),"MKWS0002",8),v1=n==16&&!std::memcmp(b.data(),"MKWS0001",8);
 const unsigned at=v2?16:12;
 const std::uint32_t stored=std::uint32_t(b[at])|std::uint32_t(b[at+1])<<8|std::uint32_t(b[at+2])<<16|std::uint32_t(b[at+3])<<24;
 if(!end||!(v1||v2)||b[8]>1||b[9]>1||b[10]>1||b[11]>1||(v2&&b[12]>1)||stored!=crc(b.data(),at))return false;
 out.textureQuality=b[8];out.dynamicShadows=b[9];out.bloom=b[10];out.opponentDetail=b[11];out.wide800=v2&&b[12];
 return true;
}
}
// The saved settings, or the backup of the last good file. False when there
// is neither (first boot): the caller keeps its defaults.
inline bool loadGameSettings(const char* path,UiSettings& out){
 char bak[160];std::snprintf(bak,sizeof bak,"%s.bak",path);
 return settings_file::read(path,out)||settings_file::read(bak,out);
}
// Written to a temporary file and renamed, keeping the previous file as .bak.
inline bool saveGameSettings(const char* path,const UiSettings& s){
 std::array<unsigned char,20> b{};std::memcpy(b.data(),"MKWS0002",8);
 b[8]=std::uint8_t(s.textureQuality!=0);b[9]=s.dynamicShadows;b[10]=s.bloom;b[11]=std::uint8_t(s.opponentDetail!=0);b[12]=s.wide800;
 const auto c=settings_file::crc(b.data(),16);for(unsigned i=0;i<4;++i)b[16+i]=std::uint8_t(c>>(8*i));
 char tmp[160],bak[160];std::snprintf(tmp,sizeof tmp,"%s.tmp",path);std::snprintf(bak,sizeof bak,"%s.bak",path);
 FILE* f=std::fopen(tmp,"wb");if(!f)return false;
 bool ok=std::fwrite(b.data(),1,b.size(),f)==b.size();if(std::fclose(f))ok=false;
 if(!ok){std::remove(tmp);return false;}
 if(FILE* old=std::fopen(path,"rb")){std::fclose(old);std::remove(bak);if(std::rename(path,bak))return false;}
 return std::rename(tmp,path)==0;
}
}
