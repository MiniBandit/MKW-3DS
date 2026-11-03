#pragma once
#include <algorithm>
#include <cstdint>
namespace mkw {
// The accepted five-tick interpolation has only six weights. Positive byte
// channels never hit a half-integer: round(n / 5) is exactly (n + 2) / 5.
inline uint8_t lightTextureBlendByte(uint8_t from,uint8_t to,unsigned tick){
    tick=std::min(tick,5u);
    return uint8_t((unsigned(from)*(5-tick)+unsigned(to)*tick+2)/5);
}
inline void blendLightTexture32(uint8_t* out,const uint8_t* from,const uint8_t* to,unsigned tick){
    constexpr unsigned morton[]={0,1,4,5,16,17,20,21};
    tick=std::min(tick,5u);
    for(unsigned y=0;y<32;++y){
        const unsigned row=((31-y)/8)*256+2*morton[(31-y)&7];
        for(unsigned x=0;x<32;++x){
            const unsigned src=(y*32+x)*4,dst=(row+(x/8)*64+morton[x&7])*4;
            for(unsigned c=0;c<4;++c)out[dst+3-c]=lightTextureBlendByte(from[src+c],to[src+c],tick);
        }
    }
}
}
