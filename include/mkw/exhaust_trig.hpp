#pragma once
#include <mkw/vehicle_exhaust.hpp>
#include <array>
#include <cmath>
#include <cstddef>

namespace mkw {
// Sines and cosines of each exhaust pipe's pitch (q[3]) and yaw (q[4]), built
// once instead of four calls per pipe per step. Exact: the same float inputs
// through the same runtime sinf/cosf (volatile stops compile-time folding,
// which rounds differently). Form 0 converts degrees as x*ToRadians (exhaust
// smoke, opponents' boost flames); form 1 as x*float(M_PI)/180 (the local
// boost flame). They may differ in the last bit, so each keeps its own.
struct PipeTrig {float sp,cp,sy,cy;};
template<int Form> const PipeTrig& exhaustTrig(unsigned vehicle,unsigned pipe){
    static const auto table=[]{
        std::array<std::array<PipeTrig,4>,VehicleExhausts.size()> t{};
        for(std::size_t v=0;v<VehicleExhausts.size();++v)
            for(unsigned p=0;p<4&&p<VehicleExhausts[v].count;++p){
                const float* q=VehicleExhausts[v].pipes[p];
                constexpr float ToRadians=0.01745329251994329577f;
                volatile float pitch=Form==0?q[3]*ToRadians:q[3]*float(M_PI)/180.f;
                volatile float yaw=Form==0?q[4]*ToRadians:q[4]*float(M_PI)/180.f;
                const float a=pitch,b=yaw;
                t[v][p]={std::sin(a),std::cos(a),std::sin(b),std::cos(b)};
            }
        return t;
    }();
    return table[vehicle<VehicleExhausts.size()?vehicle:VehicleExhausts.size()-1][pipe<4?pipe:3];
}
}
