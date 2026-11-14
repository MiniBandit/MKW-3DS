// SPDX-License-Identifier: LicenseRef-MKW3DS
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
#pragma once
// LZ4 block format (sequences of literals and back references, no frame),
// written from the format's public description. The game decodes packed
// assets with lz4Decode; the builder's tools/mpk2.cpp encodes them.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mkw {
// Decodes src into exactly dstSize bytes (src and dst must not overlap).
// Safe against malformed input.
inline bool lz4Decode(const uint8_t* src,std::size_t srcSize,uint8_t* dst,std::size_t dstSize){
    const uint8_t* ip=src;const uint8_t* const iend=src+srcSize;
    uint8_t* op=dst;uint8_t* const oend=dst+dstSize;
    auto length=[&](std::size_t& n)->bool{
        for(unsigned b=255;b==255;){if(ip>=iend)return false;b=*ip++;n+=b;}
        return true;
    };
    while(ip<iend){
        const unsigned token=*ip++;
        std::size_t literals=token>>4;
        if(literals==15&&!length(literals))return false;
        if(std::size_t(iend-ip)<literals||std::size_t(oend-op)<literals)return false;
        std::memmove(op,ip,literals);op+=literals;ip+=literals;
        if(ip==iend)break; // the last sequence has no match
        if(iend-ip<2)return false;
        const std::size_t offset=std::size_t(ip[0])|std::size_t(ip[1])<<8;ip+=2;
        std::size_t match=(token&15)+4;
        if((token&15)==15&&!length(match))return false;
        if(!offset||std::size_t(op-dst)<offset||std::size_t(oend-op)<match)return false;
        const uint8_t* from=op-offset;
        for(std::size_t i=0;i<match;++i)op[i]=from[i]; // overlapping copies repeat
        op+=match;
    }
    return op==oend;
}
// Greedy encoder (4-byte hashes, 64 KiB window). Ends with at least five
// literals and starts no match within the last twelve bytes, as the format asks.
inline std::vector<uint8_t> lz4Encode(const uint8_t* in,std::size_t n){
    std::vector<uint8_t> out;out.reserve(n/2+16);
    auto read32=[&](std::size_t at){uint32_t v;std::memcpy(&v,in+at,4);return v;};
    auto putLength=[&](std::size_t v){while(v>=255){out.push_back(255);v-=255;}out.push_back(uint8_t(v));};
    auto emit=[&](std::size_t anchor,std::size_t literals,std::size_t offset,std::size_t match){
        const std::size_t ml=match?match-4:0;
        out.push_back(uint8_t((literals>=15?15:literals)<<4|(match?(ml>=15?15:ml):0)));
        if(literals>=15)putLength(literals-15);
        out.insert(out.end(),in+anchor,in+anchor+literals);
        if(!match)return;
        out.push_back(uint8_t(offset));out.push_back(uint8_t(offset>>8));
        if(ml>=15)putLength(ml-15);
    };
    std::size_t anchor=0;
    if(n>=13){
        std::vector<int32_t> table(1u<<16,-1);
        const std::size_t limit=n-12,matchEnd=n-5;
        std::size_t i=0;
        while(i<limit){
            const uint32_t sequence=read32(i);
            const uint32_t h=(sequence*2654435761u)>>16;
            const int32_t ref=table[h];table[h]=int32_t(i);
            if(ref>=0&&i-std::size_t(ref)<=65535&&read32(std::size_t(ref))==sequence){
                std::size_t match=4;
                while(i+match<matchEnd&&in[std::size_t(ref)+match]==in[i+match])++match;
                emit(anchor,i-anchor,i-std::size_t(ref),match);
                i+=match;anchor=i;
                if(i>=2&&i-2<limit)table[(read32(i-2)*2654435761u)>>16]=int32_t(i-2);
            }else ++i;
        }
    }
    emit(anchor,n-anchor,0,0);
    return out;
}
}
