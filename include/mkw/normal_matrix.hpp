#pragma once
#include <mkw/math.hpp>

namespace mkw {
// Inverse transpose of the linear transform, for scaled object normals.
// A common positive factor is unnecessary: the vertex shader normalizes it.
inline void normalMatrix(const Vec3 (&rows)[3],Vec3 (&out)[3]){
    out[0]=cross(rows[1],rows[2]);out[1]=cross(rows[2],rows[0]);out[2]=cross(rows[0],rows[1]);
    const float det=dot(rows[0],out[0]);
    if(std::fabs(det)<1e-12f){for(unsigned i=0;i<3;++i)out[i]=rows[i];return;}
    if(det<0)for(auto& row:out)row=-row;
}
}
