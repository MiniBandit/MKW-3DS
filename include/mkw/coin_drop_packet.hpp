#pragma once
#include <mkw/math.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace mkw {
// CoinManager encodes even offline drops through80528954/805289e4.
// The exponent field is unsigned IEEE exponent minus127; the mantissa is
// truncated, not rounded. Original coordinate offsets keep valid arenas in
// that format's positive range. No extra coordinate clamp is introduced.
struct CoinDropPacket {
    Vec3 position{};float yaw=0;
    static float quantize(float value,unsigned exponentBits,unsigned mantissaBits){
        uint32_t word;std::memcpy(&word,&value,4);
        const uint32_t exponent=(((word>>23)&255u)-127u)&((1u<<exponentBits)-1);
        const uint32_t mantissa=word&(((1u<<mantissaBits)-1)<<(23-mantissaBits));
        word=((exponent+127u)<<23)|mantissa;
        std::memcpy(&value,&word,4);return value;
    }
    //80528954 callers80870818/860/89c/8e4; decoder80872f30..73098.
    static CoinDropPacket capture(Vec3 at,float yaw){
        return {{quantize((at.x+35000.f)+1.f,5,8)-35001.f,
                 quantize((at.y+5000.f)+1.f,4,6)-5001.f,
                 quantize((at.z+35000.f)+1.f,5,8)-35001.f},
                quantize((yaw+4.f)+1.f,3,7)-5.f};
    }
    //80876008..76274: horizontal true velocity first, then the body's
    // Z/front axis, then worldZ. Preserve the native squared-length gate.
    static Vec3 direction(Vec3 velocity,Vec3 front){
        velocity.y=0;
        if(dot(velocity,velocity)<=1.1920928955078125e-7f){
            velocity={front.x,0,front.z};
            if(dot(velocity,velocity)<=1.1920928955078125e-7f)velocity={0,0,1};
        }
        velocity=normalized(velocity,{0,0,1});
        velocity.x=std::clamp(velocity.x,-1.f,1.f);velocity.z=std::clamp(velocity.z,-1.f,1.f);
        return velocity;
    }
};
}
