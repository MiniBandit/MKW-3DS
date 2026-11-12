#pragma once
#include <array>
#include <algorithm>

namespace mkw {
// Clip before screen-space subdivision, including eye-plane crossings. The
// interpolator preserves source attributes in homogeneous-edge parameter t;
// dividing UVs first would break both their footprint and the indirect map.
template<class Vertex,class Distance,class Interpolate,class Emit>
void clipWaterTriangle(const std::array<Vertex,3>& triangle,Distance distance,Interpolate interpolate,Emit emit){
    std::array<Vertex,12> polygon{},next{};
    std::copy(triangle.begin(),triangle.end(),polygon.begin());unsigned count=3;
    for(unsigned plane=0;plane<7&&count;++plane){
        unsigned out=0;
        for(unsigned i=0;i<count;++i){
            const Vertex& a=polygon[i];const Vertex& b=polygon[(i+1)%count];
            const float da=distance(a,plane),db=distance(b,plane);
            if(da>=0)next[out++]=a;
            if((da>=0)!=(db>=0))next[out++]=interpolate(a,b,std::clamp(da/(da-db),0.f,1.f));
        }
        polygon=next;count=out;
    }
    for(unsigned i=1;i+1<count;++i)emit(std::array<Vertex,3>{polygon[0],polygon[i],polygon[i+1]});
}
}
