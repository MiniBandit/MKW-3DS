#include <mkw/mii_part_bank.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace mkw {namespace {
std::uint32_t u32(const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
std::int16_t s16(const std::uint8_t* p){unsigned v=p[0]|(unsigned(p[1])<<8);return std::int16_t(v<32768?int(v):int(v)-65536);}
float f32(const std::uint8_t* p){auto bits=u32(p);float v;std::memcpy(&v,&bits,4);return v;}
}
bool MiiPartBank::open(const char* path){
 auto* f=std::fopen(path,"rb");if(!f)return false;
 auto fail=[&](){std::fclose(f);return false;};
 if(std::fseek(f,0,SEEK_END))return fail();
 long length=std::ftell(f);
 if(length<16||length>16*1024*1024||std::fseek(f,0,SEEK_SET))return fail();
 std::uint8_t h[16];if(std::fread(h,1,16,f)!=16||std::memcmp(h,"MPR1",4)||u32(h+4)!=1||u32(h+8)!=472||u32(h+12)!=16)return fail();
 std::vector<Entry> entries;entries.reserve(472);std::uint32_t end=16+472*20;
 for(unsigned i=0;i<472;++i){
  std::uint8_t b[20];if(std::fread(b,1,20,f)!=20)return fail();Entry e{u32(b),u32(b+4),u32(b+8),u32(b+12),u32(b+16)};
  constexpr unsigned counts[]={4,50,24,8,12,72,1,10,72,8,2,25,4,12,12,12,72,72};
  if(e.group>=18||e.index>=counts[e.group]||e.kind>1||e.offset!=end||e.offset>unsigned(length)||e.size>unsigned(length)-e.offset||e.size>1024*1024)return fail();
  if(!entries.empty()&&(e.group<entries.back().group||(e.group==entries.back().group&&e.index<=entries.back().index)))return fail();
  end=e.offset+e.size;entries.push_back(e);
 }
 if(end!=unsigned(length))return fail();
 if(file_)std::fclose(file_);
 file_=f;entries_.swap(entries);return true;
}
bool MiiPartBank::record(unsigned group,unsigned index,unsigned kind,std::vector<std::uint8_t>& out) const{
 if(!file_)return false;
 auto it=std::find_if(entries_.begin(),entries_.end(),[&](const Entry& e){return e.group==group&&e.index==index&&e.kind==kind;});
 if(it==entries_.end()||std::fseek(file_,it->offset,SEEK_SET))return false;
 std::vector<std::uint8_t> data(it->size);if(std::fread(data.data(),1,data.size(),file_)!=data.size())return false;out.swap(data);return true;
}
bool MiiPartBank::shape(unsigned group,unsigned index,MiiPartShape& out) const{
 std::vector<std::uint8_t> data;if(!record(group,index,0,data)||data.size()<48)return false;
 const auto* p=data.data();unsigned type=u32(p),nv=u32(p+4),ni=u32(p+8);
 constexpr unsigned groups[]={13,5,3,8,16,0,14,9,6};
 if(type>=9||groups[type]!=group||nv>65535||ni>65535||ni%3||data.size()!=48+std::size_t(nv)*16+std::size_t(ni)*2)return false;
 MiiPartShape shape;shape.type=type;
 for(unsigned i=0;i<9;++i){shape.anchors[i]=f32(p+12+i*4);if(!std::isfinite(shape.anchors[i])||std::fabs(shape.anchors[i])>127)return false;}
 shape.vertices.resize(nv);p+=48;
 for(auto& v:shape.vertices){for(unsigned i=0;i<3;++i){v.position[i]=s16(p+i*2);v.normal[i]=s16(p+6+i*2);}v.uv={s16(p+12),s16(p+14)};p+=16;}
 shape.indices.resize(ni);for(auto& i:shape.indices){i=std::uint16_t(unsigned(p[0])|(unsigned(p[1])<<8));p+=2;if(i>=nv)return false;}
 out=std::move(shape);return true;
}
bool MiiPartBank::texture(unsigned group,unsigned index,MiiPartTexture& out) const{
 std::vector<std::uint8_t> data;if(!record(group,index,1,data)||data.size()<16)return false;
 const auto* p=data.data();MiiPartTexture t;t.width=u32(p);t.height=u32(p+4);t.wrapS=u32(p+8);t.wrapT=u32(p+12);
 if(!t.width||!t.height||t.width>1024||t.height>1024||t.wrapS>2||t.wrapT>2||data.size()!=16+std::size_t(t.width)*t.height*4)return false;
 t.rgba.assign(data.begin()+16,data.end());out=std::move(t);return true;
}
}
