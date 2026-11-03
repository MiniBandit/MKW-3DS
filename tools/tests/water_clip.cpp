#include "mkw/water_clip.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
struct V {float x,y,z,w,u;};
int main(){
    auto distance=[](V v,unsigned plane){const float d[]={v.w-.001f,v.w+v.x,v.w-v.x,v.w+v.y,v.w-v.y,v.w+v.z,-v.z};return d[plane];};
    auto mix=[](V a,V b,float t){return V{a.x*(1-t)+b.x*t,a.y*(1-t)+b.y*t,a.z*(1-t)+b.z*t,a.w*(1-t)+b.w*t,a.u*(1-t)+b.u*t};};
    float area=0;unsigned count=0;
    mkw::clipWaterTriangle(std::array<V,3>{{{-.5,-.5,-.5,1,0},{.5,-.5,-.5,1,0},{0,.5,.5,1,0}}},distance,mix,[&](const auto& t){
        ++count;area+=std::abs((t[1].x-t[0].x)*(t[2].y-t[0].y)-(t[2].x-t[0].x)*(t[1].y-t[0].y))*.5f;
        for(auto v:t)assert(v.z<=0);
    });
    assert(count==2&&std::abs(area-.375f)<1.e-6f);
    std::mt19937 rng(13);std::uniform_real_distribution<float> d(-10,10);
    for(unsigned n=0;n<10000;++n){
        std::array<V,3> tri;
        for(auto& v:tri){v={d(rng),d(rng),d(rng),d(rng),0};v.u=2*v.x+3*v.y-v.z+v.w;}
        mkw::clipWaterTriangle(tri,distance,mix,[&](const auto& t){
            for(auto v:t){for(unsigned p=0;p<7;++p)assert(distance(v,p)>-1.e-4f);
                assert(std::abs(v.u-(2*v.x+3*v.y-v.z+v.w))<1.e-4f);}
        });
    }
    std::puts("PASS: near-plane quad area and 10000 eye/frustum crossing triangles with attribute continuity");
}
