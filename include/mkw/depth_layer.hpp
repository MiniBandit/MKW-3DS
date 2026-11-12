#pragma once
#include <bit>
#include <cstdint>
namespace mkw {
// C3D_DepthMap submits PICA float24 (16 fraction bits), truncating seven
// float32 fraction bits. Keep the existing small layer bias, but ensure it
// survives that conversion. This affects only preclassified course layers.
inline float layerDepthScale(float scale){
    constexpr float step=32.f/16777215.f;
    float biased=scale*(1.f+step);
    const auto base=std::bit_cast<uint32_t>(scale);
    const auto next=std::bit_cast<uint32_t>(biased);
    if((base>>7)==(next>>7))
        biased=std::bit_cast<float>((base&~127u)+128u);
    return biased;
}
}
