#include <mkw/ghost_file.hpp>
#include <algorithm>
#include <cstring>

namespace mkw {
namespace {
// Retail run limits: face and stick tuples hold 255 frames; trick tuples
// keep a 12-bit duration, its upper four bits beside the trick value.
constexpr uint16_t RunLimit[3]={0xff,0xff,0xfff};
void put16(uint8_t* p,uint16_t v){p[0]=uint8_t(v>>8);p[1]=uint8_t(v);}
void put32(uint8_t* p,uint32_t v){p[0]=uint8_t(v>>24);p[1]=uint8_t(v>>16);p[2]=uint8_t(v>>8);p[3]=uint8_t(v);}
uint16_t get16(const uint8_t* p){return uint16_t(p[0]<<8|p[1]);}
uint32_t get32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
// Minutes 7 bits, seconds 7 bits, milliseconds 10 bits.
uint32_t packTime(RkgTime t){return uint32_t(t.min&0x7f)<<17|uint32_t(t.sec&0x7f)<<10|(t.ms&0x3ffu);}
uint16_t tuple(unsigned stream,uint8_t value,uint16_t length){
    return stream==2?uint16_t((value|(length>>8))<<8|(length&0xff)):uint16_t(value<<8|length);
}
// CRC-16-CCITT (XModem) over the Mii data, as the header stores it.
uint16_t crc16(const uint8_t* p,std::size_t n){
    uint16_t crc=0;
    while(n--){crc^=uint16_t(*p++)<<8;for(int b=0;b<8;++b)crc=crc&0x8000?uint16_t(crc<<1^0x1021):uint16_t(crc<<1);}
    return crc;
}
}

uint32_t rkgCrc32(const uint8_t* p,std::size_t n){
    uint32_t crc=0xffffffffu;
    while(n--){crc^=*p++;for(int b=0;b<8;++b)crc=crc&1?crc>>1^0xedb88320u:crc>>1;}
    return ~crc;
}

void GhostRecorder::reset(){
    for(auto& s:streams_){s.tuples.clear();s.tuples.reserve(1024);s.value=0;s.length=0;}
    frames_=0;full_=false;
}
void GhostRecorder::append(Stream& s,uint8_t value,uint16_t limit){
    if(s.length&&s.value==value&&s.length<limit){++s.length;return;}
    if(s.length)s.tuples.push_back(tuple(unsigned(&s-streams_.data()),s.value,s.length));
    s.value=value;s.length=1;
}
void GhostRecorder::push(const GhostFrame& f){
    if(full_)return;
    const uint8_t values[3]={uint8_t(f.buttons&0xf),uint8_t((f.stickX&0xf)<<4|(f.stickY&0xf)),uint8_t((f.trick&7)<<4)};
    for(unsigned i=0;i<3;++i)append(streams_[i],values[i],RunLimit[i]);
    ++frames_;
    full_=bytes()>RkgInputCapacity;
}
std::size_t GhostRecorder::bytes() const{
    std::size_t tuples=0;
    for(const auto& s:streams_)tuples+=s.tuples.size()+(s.length?1:0);
    return 8+2*tuples;
}
std::size_t GhostRecorder::write(uint8_t* out,std::size_t capacity) const{
    const std::size_t size=bytes();
    if(full_||size>capacity)return 0;
    std::memset(out,0,8);
    uint8_t* p=out+8;
    for(unsigned i=0;i<3;++i){
        const auto& s=streams_[i];
        put16(out+2*i,uint16_t(s.tuples.size()+(s.length?1:0)));
        for(uint16_t t:s.tuples){put16(p,t);p+=2;}
        if(s.length){put16(p,tuple(i,s.value,s.length));p+=2;}
    }
    return size;
}

bool buildRkg(const GhostRecorder& inputs,const GhostInfo& info,uint8_t* out){
    std::memset(out,0,RkgSize);
    const std::size_t length=inputs.write(out+RkgHeaderSize,RkgInputCapacity);
    if(!length)return false;
    std::memcpy(out,"RKGD",4);
    put32(out+0x4,packTime(info.finish)<<8|uint32_t(info.course&0x3f)<<2);
    put32(out+0x8,uint32_t(info.vehicle&0x3f)<<26|uint32_t(info.character&0x3f)<<20|uint32_t(info.year&0x7f)<<13|
                  uint32_t(info.month&0xf)<<9|uint32_t(info.day&0x1f)<<4|(info.controller&0xfu));
    put32(out+0xc,uint32_t(info.type&0x7f)<<18|uint32_t(info.automaticDrift)<<17|uint32_t(length));
    out[0x10]=uint8_t(info.laps.size());
    for(unsigned i=0;i<info.laps.size();++i){const uint32_t t=packTime(info.laps[i]);out[0x11+3*i]=uint8_t(t>>16);out[0x12+3*i]=uint8_t(t>>8);out[0x13+3*i]=uint8_t(t);}
    // Location sharing off.
    out[0x34]=out[0x35]=out[0x36]=out[0x37]=0xff;
    put16(out+0x86,crc16(out+0x3c,0x4a));
    put32(out+RkgSize-4,rkgCrc32(out,RkgSize-4));
    return true;
}

bool peekRkgHeader(const uint8_t* d,std::size_t size,GhostHeader& h){
    if(size<0x10||std::memcmp(d,"RKGD",4))return false;
    const uint32_t a=get32(d+0x4),b=get32(d+0x8),c=get32(d+0xc);
    h.finish={uint16_t(a>>25),uint8_t(a>>18&0x7f),uint16_t(a>>8&0x3ff)};
    h.course=uint8_t(a>>2&0x3f);
    h.vehicle=uint8_t(b>>26);h.character=uint8_t(b>>20&0x3f);
    h.compressed=c>>27&1;h.automaticDrift=c>>17&1;
    return (b>>13&0x7f)<100&&(b>>9&0xf)<=12&&(b>>4&0x1f)<32;
}

bool readRkgHeader(const uint8_t* d,std::size_t size,GhostHeader& h){
    if(size<RkgHeaderSize+4||!peekRkgHeader(d,size,h))return false;
    if(!h.compressed&&(size<RkgSize||(get32(d+0xc)&0xffff)>RkgInputCapacity))return false;
    // The CRC-32 closes the file, compressed or not.
    return get32(d+size-4)==rkgCrc32(d,size-4);
}

bool rkgInputSection(const uint8_t* d,std::size_t size,std::vector<uint8_t>& out){
    GhostHeader h;
    if(!readRkgHeader(d,size,h))return false;
    if(!h.compressed){out.assign(d+RkgHeaderSize,d+RkgHeaderSize+RkgInputCapacity);return true;}
    // u32 compressed length, then Yaz1 (EGG::Decomp::DecodeSZS).
    if(size<RkgHeaderSize+4+16+4)return false;
    const uint8_t* src=d+RkgHeaderSize+4;const std::size_t n=std::min<std::size_t>(get32(d+RkgHeaderSize),size-RkgHeaderSize-8);
    if(n<16||std::memcmp(src,"Yaz1",4))return false;
    const uint32_t expanded=get32(src+4);
    if(!expanded||expanded>RkgInputCapacity)return false;
    out.assign(expanded,0);
    std::size_t s=16,o=0;uint8_t code=0;int bits=0;
    while(o<expanded){
        if(!bits){if(s>=n)return false;code=src[s++];bits=8;}
        if(code&0x80){if(s>=n)return false;out[o++]=src[s++];}
        else{
            if(s+1>=n)return false;
            const uint8_t b1=src[s++],b2=src[s++];
            const std::size_t back=((b1&0xfu)<<8|b2)+1;std::size_t length=b1>>4;
            if(!length){if(s>=n)return false;length=src[s++]+0x12u;}else length+=2;
            if(back>o)return false;
            for(;length&&o<expanded;--length,++o)out[o]=out[o-back];
        }
        code<<=1;--bits;
    }
    return true;
}

bool decodeGhostInputs(const uint8_t* section,std::size_t size,std::vector<GhostFrame>& frames){
    frames.clear();
    if(size<8)return false;
    const unsigned counts[3]={get16(section),get16(section+2),get16(section+4)};
    if(8+2u*(counts[0]+counts[1]+counts[2])>size)return false;
    // KPadGhostButtonsStream: each tuple lasts max(duration,1) frames; a
    // stream past its last tuple reads 0.
    std::array<std::vector<uint8_t>,3> values;
    const uint8_t* p=section+8;
    for(unsigned i=0;i<3;++i)for(unsigned n=0;n<counts[i];++n,p+=2){
        const unsigned length=i==2?(p[0]&0xfu)<<8|p[1]:p[1];
        values[i].insert(values[i].end(),std::max(length,1u),i==2?uint8_t(p[0]&0x70):p[0]);
    }
    const std::size_t n=std::max({values[0].size(),values[1].size(),values[2].size()});
    frames.resize(n);
    for(std::size_t f=0;f<n;++f){
        auto at=[&](unsigned i){return f<values[i].size()?values[i][f]:uint8_t(0);};
        const uint8_t trick=at(2)>>4;
        frames[f]={at(0),uint8_t(at(1)>>4),uint8_t(at(1)&0xf),uint8_t(trick<=4?trick:0)};
    }
    return true;
}
}
