#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace mkw {
// The accepted five-tick interpolation has only six weights. Positive byte
// channels never hit a half-integer: round(n / 5) is exactly (n + 2) / 5.
inline uint8_t lightTextureBlendByte(uint8_t from,uint8_t to,unsigned tick){
    tick=std::min(tick,5u);
    return uint8_t((unsigned(from)*(5-tick)+unsigned(to)*tick+2)/5);
}
// The reference: row-major RGBA bytes (y from the top) to the 32x32 RGBA8
// texture's 8x8 Morton tiles, ABGR byte order, rows from the bottom.
inline void blendLightTexture32Reference(uint8_t* out,const uint8_t* from,const uint8_t* to,unsigned tick){
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
namespace lightblend {
// Texel i of the row-major source -> byte offset of its texel in the tiled texture.
struct Tiles {
    uint16_t offset[32*32]{};
    constexpr Tiles(){
        constexpr unsigned morton[]={0,1,4,5,16,17,20,21};
        for(unsigned y=0;y<32;++y){
            const unsigned row=((31-y)/8)*256+2*morton[(31-y)&7];
            for(unsigned x=0;x<32;++x)offset[y*32+x]=uint16_t((row+(x/8)*64+morton[x&7])*4);
        }
    }
};
// (n + 2) / 5 for every weighted sum n = from*(5-tick) + to*tick <= 5*255.
struct Fifths {
    uint8_t q[5*255+1]{};
    constexpr Fifths(){for(unsigned n=0;n<sizeof q;++n)q[n]=uint8_t((n+2)/5);}
};
inline constexpr Tiles tiles{};
inline constexpr Fifths fifths{};
}
// Byte-identical to blendLightTexture32Reference: the swizzle and the
// division come from tables, and weights 0 and 5 copy one endpoint.
inline void blendLightTexture32(uint8_t* out,const uint8_t* from,const uint8_t* to,unsigned tick){
    tick=std::min(tick,5u);
    const uint16_t* offset=lightblend::tiles.offset;
    if(tick==0||tick==5){
        const uint8_t* source=tick?to:from;
        for(unsigned i=0;i<32*32;++i){
            uint32_t texel;std::memcpy(&texel,source+i*4,4);texel=__builtin_bswap32(texel);
            std::memcpy(out+offset[i],&texel,4);
        }
        return;
    }
    const unsigned a=5-tick,b=tick;const uint8_t* q=lightblend::fifths.q;
    for(unsigned i=0;i<32*32;++i){
        const uint8_t* f=from+i*4;const uint8_t* t=to+i*4;uint8_t* d=out+offset[i];
        d[3]=q[f[0]*a+t[0]*b];d[2]=q[f[1]*a+t[1]*b];d[1]=q[f[2]*a+t[2]*b];d[0]=q[f[3]*a+t[3]*b];
    }
}
}
