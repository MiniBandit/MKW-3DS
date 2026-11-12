// SPDX-License-Identifier: LicenseRef-MKW3DS-Online
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-ONLINE-LICENSE.txt.
#include <mkw/ghost_share.hpp>
#include <algorithm>
#include <cstring>

namespace mkw {
std::vector<uint8_t> yazCompress(const uint8_t* data,size_t size,const char magic[4]){
    std::vector<uint8_t> out(16,0);std::memcpy(out.data(),magic,4);
    for(unsigned i=0;i<4;++i)out[4+i]=uint8_t(size>>(24-8*i));
    // Hash chains over three-byte prefixes, window 4 KiB.
    constexpr size_t Window=0x1000,MaxLength=0x111;constexpr unsigned HashBits=12;
    std::vector<int32_t> head(1u<<HashBits,-1),prev(size,-1);
    auto hash=[&](size_t i){return ((unsigned(data[i])<<8)^(unsigned(data[i+1])<<4)^data[i+2])&((1u<<HashBits)-1);};
    size_t codeAt=0;unsigned bit=8;size_t i=0;
    auto insert=[&](size_t at){if(at+2<size){const unsigned h=hash(at);prev[at]=head[h];head[h]=int32_t(at);}};
    while(i<size){
        if(bit==8){codeAt=out.size();out.push_back(0);bit=0;}
        size_t bestLength=0,bestDistance=0;
        if(i+2<size){
            unsigned chain=0;
            for(int32_t c=head[hash(i)];c>=0&&i-size_t(c)<=Window&&chain<64;c=prev[size_t(c)],++chain){
                size_t length=0;const size_t limit=std::min(MaxLength,size-i);
                while(length<limit&&data[size_t(c)+length]==data[i+length])++length;
                if(length>bestLength){bestLength=length;bestDistance=i-size_t(c);if(length==limit)break;}
            }
        }
        if(bestLength>=3){
            const unsigned d=unsigned(bestDistance-1);
            if(bestLength<0x12){out.push_back(uint8_t(((bestLength-2)<<4)|(d>>8)));out.push_back(uint8_t(d));}
            else{out.push_back(uint8_t(d>>8));out.push_back(uint8_t(d));out.push_back(uint8_t(bestLength-0x12));}
            for(size_t k=0;k<bestLength;++k)insert(i+k);
            i+=bestLength;
        }else{
            out[codeAt]|=uint8_t(0x80>>bit);out.push_back(data[i]);insert(i);++i;
        }
        ++bit;
    }
    return out;
}
}
