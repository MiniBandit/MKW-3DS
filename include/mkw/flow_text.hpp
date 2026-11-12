#pragma once
#include <cstdint>
namespace mkw {
// BRFNT maps UTF-16 BMP glyphs; BMG-derived runtime strings are UTF-8.
inline char16_t nextFlowGlyph(const char*& text){
 const unsigned lead=uint8_t(*text++);
 if(lead<0x80)return char16_t(lead);
 const unsigned bytes=lead>=0xc2&&lead<=0xdf?2:lead>=0xe0&&lead<=0xef?3:lead>=0xf0&&lead<=0xf4?4:0;
 if(!bytes)return 0xfffd;
 unsigned code=lead&((1u<<(7-bytes))-1);
 for(unsigned i=1;i<bytes;++i){
  const unsigned c=uint8_t(*text);
  if((c&0xc0)!=0x80)return 0xfffd;
  ++text;code=(code<<6)|(c&0x3f);
 }
 if(code<(bytes==2?0x80u:bytes==3?0x800u:0x10000u)||code>0xffffu||(code>=0xd800&&code<=0xdfff))return 0xfffd;
 return char16_t(code);
}
}
