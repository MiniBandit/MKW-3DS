// SPDX-License-Identifier: LicenseRef-MKW3DS-Online
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-ONLINE-LICENSE.txt.
#pragma once
#include <cstdint>
#include <cstddef>

namespace mkw::net {
// A connection code: a console's internet address and port as the other
// players type it, e.g. "7K2D-QX9M-4PA". 48 bits of address and port plus a
// 7-bit check, in Crockford base 32 (no I, L, O or U; those read as 1, 1, 0, V).
constexpr size_t CodeChars=11;
constexpr char CodeAlphabet[]="0123456789ABCDEFGHJKMNPQRSTVWXYZ";

inline uint8_t codeCheck(uint64_t value){return uint8_t(((value+1)*0x9E3779B97F4A7C15ull)>>57);}

// address: IPv4 in host byte order (192.168.1.2 is 0xC0A80102).
inline void encodeCode(uint32_t address,uint16_t port,char out[16]){
    const uint64_t value=(uint64_t(address)<<16)|port,packed=(value<<7)|codeCheck(value);
    unsigned n=0;
    for(unsigned i=0;i<CodeChars;++i){
        if(i==4||i==8)out[n++]='-';
        out[n++]=CodeAlphabet[(packed>>(5*(CodeChars-1-i)))&31];
    }
    out[n]=0;
}

// Accepts any case, spaces and dashes. False when it isn't a valid code.
inline bool decodeCode(const char* text,uint32_t& address,uint16_t& port){
    uint64_t packed=0;unsigned digits=0;
    for(const char* c=text;*c;++c){
        char ch=*c;if(ch==' '||ch=='-')continue;
        if(ch>='a'&&ch<='z')ch=char(ch-'a'+'A');
        if(ch=='O')ch='0';else if(ch=='I'||ch=='L')ch='1';else if(ch=='U')ch='V';
        unsigned v=32;for(unsigned i=0;i<32;++i)if(CodeAlphabet[i]==ch){v=i;break;}
        if(v==32||++digits>CodeChars)return false;
        packed=(packed<<5)|v;
    }
    if(digits!=CodeChars)return false;
    const uint64_t value=packed>>7;
    if(codeCheck(value)!=(packed&127)||(value>>48))return false;
    address=uint32_t(value>>16);port=uint16_t(value);
    return address!=0&&port!=0;
}
}
