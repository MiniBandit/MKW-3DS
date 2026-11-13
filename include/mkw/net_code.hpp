// SPDX-License-Identifier: LicenseRef-MKW3DS-Online
// Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-ONLINE-LICENSE.txt.
#pragma once
#include <cstdint>
#include <cstddef>

namespace mkw::net {
// A connection code: a console's internet address and port as the other
// players type them on the number pad, like a Wii friend code:
// "1234-5678-9012-3456". The 48 bits of address << 16 | port as 15 decimal
// digits, then a Luhn check digit, so one mistyped or swapped digit is caught.
constexpr size_t CodeDigits=16;
constexpr size_t CodeTextSize=CodeDigits+3+1; // with the three hyphens and the end

inline unsigned luhnCheck(const char digits[15]){
    // Doubling every other digit from the right of the full 16 (the check last).
    unsigned sum=0;
    for(unsigned i=0;i<15;++i){
        unsigned d=unsigned(digits[14-i]-'0');
        if(i%2==0){d*=2;if(d>9)d-=9;}
        sum+=d;
    }
    return (10-sum%10)%10;
}

// address: IPv4 in host byte order (192.168.1.2 is 0xC0A80102).
// out: the 16 digits alone (digits) or grouped with hyphens (grouped).
inline void encodeCode(uint32_t address,uint16_t port,char out[CodeTextSize],bool grouped=true){
    uint64_t value=(uint64_t(address)<<16)|port;
    char digits[CodeDigits+1]{};
    for(int i=14;i>=0;--i){digits[i]=char('0'+value%10);value/=10;}
    digits[15]=char('0'+luhnCheck(digits));
    unsigned n=0;
    for(unsigned i=0;i<CodeDigits;++i){
        if(grouped&&i&&i%4==0)out[n++]='-';
        out[n++]=digits[i];
    }
    out[n]=0;
}

// Accepts the digits with or without spaces and hyphens. False when it
// isn't a whole, valid code.
inline bool decodeCode(const char* text,uint32_t& address,uint16_t& port){
    char digits[CodeDigits]{};unsigned n=0;
    for(const char* c=text;*c;++c){
        if(*c==' '||*c=='-')continue;
        if(*c<'0'||*c>'9'||n==CodeDigits)return false;
        digits[n++]=*c;
    }
    if(n!=CodeDigits||unsigned(digits[15]-'0')!=luhnCheck(digits))return false;
    uint64_t value=0;
    for(unsigned i=0;i<15;++i)value=value*10+unsigned(digits[i]-'0');
    if(value>>48)return false;
    address=uint32_t(value>>16);port=uint16_t(value);
    return address!=0&&port!=0;
}
}
