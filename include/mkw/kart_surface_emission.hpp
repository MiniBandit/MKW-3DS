#pragma once
#include <algorithm>
#include <cstdint>

namespace mkw {
// Sand Create: USA806a9b84..806a9c5c; parameter init 806ad120..806ad170.
// speed is the magnitude of KartObjectProxy::velocity, NOT engine speed.
// softLimit is KartMove +0x18, clamped to 50 by 8068fb74..8068fb94.
struct KartSandEmission {
    float ratio;
    uint16_t life;
    static constexpr float InheritedVelocity = 0.9200000166893005f;
    static KartSandEmission sample(float speed,float softLimit,bool fullLocal,
                                  bool reducedEffects,bool halfDensity,
                                  bool chargingOrStartSmoke,unsigned colorIndex) {
        const float reference=std::max(softLimit,50.f);
        float selected=std::clamp(speed,0.f,reference);
        if(colorIndex==14)selected=reference;
        if(chargingOrStartSmoke&&!reducedEffects)selected=reference*.5f;
        // Preserve the retail single-precision operation order.
        const float maximumRatio=fullLocal?.5f:.375f;
        const float maximumLife=fullLocal?30.f:20.f;
        const float minimumLife=fullLocal?15.f:10.f;
        float ratio=selected*(maximumRatio/reference);
        const float life=maximumLife-selected*((maximumLife-minimumLife)/reference);
        if(halfDensity)ratio*=.5f;
        return {ratio,static_cast<uint16_t>(life)};
    }
};
}
