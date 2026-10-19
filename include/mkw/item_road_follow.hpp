#pragma once
#include "mkw/math.hpp"
namespace mkw {
// Retail TwistedWay CalcOtherEntityDistance/UpdateOtherEntityPosWithRot,
// USA8080167c/8080182c. Keep the material point relative to the rolling pivot.
inline Vec3 bindTwistedItem(Vec3 relative,float sine,float cosine){
    return {0,relative.x/cosine+sine*(1000.f-relative.y),relative.z};
}
inline Vec3 followTwistedItem(Vec3 local,float sine,float cosine){
    return {local.y*cosine-1000.f*sine,
            1000.f-1000.f*cosine-local.y*sine,local.z};
}
}
