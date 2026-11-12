#pragma once
#include <cmath>
namespace mkw {
// GX/PICA finite perspective: z_clip = -a*w + b, b < 0, a < 0.
// Move its affine term after perspective division: D=a+(1-a)*near/w.
// This avoids rounding a varying clip-Z once at every PICA float24 vertex.
struct PerspectiveDepth {
    float nearPlane=0,scale=1,offset=0;
    explicit operator bool()const{return nearPlane>0;}
};
inline PerspectiveDepth factorPerspectiveDepth(float a,float b){
    if(!std::isfinite(a)||!std::isfinite(b)||a>=0||a<-.01f||b>=0)return {};
    return {b/(a-1.f),1.f-a,a};
}
}
