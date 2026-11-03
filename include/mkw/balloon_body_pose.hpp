#pragma once
#include <mkw/math.hpp>
#include <cstdint>
namespace mkw {
// Kart/BikeBody80568048/80568c38: unscaled QT followed by the
// authored BSP rumble along localY. Full model scale is applied later.
struct BalloonBodyPose {
    uint16_t phase=0;
    template<class SinFIdx> Vec3 step(Vec3 position,Vec3 up,float scaleY,float height,uint16_t speed,SinFIdx sin){
        phase=uint16_t(phase+speed);
        const float displacement=(sin(float(phase)*(1.f/256.f))*height)*scaleY;
        return position+up*displacement;
    }
};
}
