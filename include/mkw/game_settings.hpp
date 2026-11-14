#pragma once
// Whole-game options from the Settings page (the license screen's corner
// button): sd:/3ds/MKWiiDS/settings.dat, 16 bytes: "MKWS0001", the four
// values, two spare bytes, then a CRC-32 of the first 12 bytes.
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
 bool operator==(const UiSettings&)const=default;
};
// Old 3DS (268 MHz, one core, 43 MB heap): every option stays at its light
// value (MenuState::old3dsSettings refuses to switch them). Bloom too: its ~20
// blur passes cost about 1 ms of CPU a frame there (user, Nov 6).
inline void lockOld3dsSettings(UiSettings& s){s.textureQuality=1;s.dynamicShadows=false;s.bloom=false;s.opponentDetail=1;}
namespace settings_file {
inline std::uint32_t crc(const unsigned char* b,unsigned n){
 std::uint32_t c=~0u;
 for(unsigned i=0;i<n;++i){c^=b[i];for(unsigned k=0;k<8;++k)c=(c>>1)^(0xedb88320u&-(c&1));}
 return ~c;
}
inline bool read(const char* path,UiSettings& out){
 std::array<unsigned char,16> b{};
 FILE* f=std::fopen(path,"rb");if(!f)return false;
 const bool ok=std::fread(b.data(),1,b.size(),f)==b.size()&&std::fgetc(f)==EOF;std::fclose(f);
 const std::uint32_t stored=std::uint32_t(b[12])|std::uint32_t(b[13])<<8|std::uint32_t(b[14])<<16|std::uint32_t(b[15])<<24;
 if(!ok||std::memcmp(b.data(),"MKWS0001",8)||b[8]>1||b[9]>1||b[10]>1||b[11]>1||stored!=crc(b.data(),12))return false;
 out.textureQuality=b[8];out.dynamicShadows=b[9];out.bloom=b[10];out.opponentDetail=b[11];
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
 std::array<unsigned char,16> b{};std::memcpy(b.data(),"MKWS0001",8);
 b[8]=std::uint8_t(s.textureQuality!=0);b[9]=s.dynamicShadows;b[10]=s.bloom;b[11]=std::uint8_t(s.opponentDetail!=0);
 const auto c=settings_file::crc(b.data(),12);for(unsigned i=0;i<4;++i)b[12+i]=std::uint8_t(c>>(8*i));
 char tmp[160],bak[160];std::snprintf(tmp,sizeof tmp,"%s.tmp",path);std::snprintf(bak,sizeof bak,"%s.bak",path);
 FILE* f=std::fopen(tmp,"wb");if(!f)return false;
 bool ok=std::fwrite(b.data(),1,b.size(),f)==b.size();if(std::fclose(f))ok=false;
 if(!ok){std::remove(tmp);return false;}
 if(FILE* old=std::fopen(path,"rb")){std::fclose(old);std::remove(bak);if(std::rename(path,bak))return false;}
 return std::rename(tmp,path)==0;
}
}
