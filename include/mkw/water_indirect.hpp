#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>

namespace mkw {
// Same integer stages as Wiicompiled aurora-main/lib/gx/shader.cpp:
// sampled .abg bytes -> STU bias -> signed 2x3 dot -> >>3 -> exponent.
// Keep signed S17.7 texel offsets; normalize for each destination texture only
// after adding to its own base coordinates (glow and wave may differ in size).
struct WaterIndirectMatrix {
    std::array<int16_t,6> coefficient{}; // sA,sB,sG,tA,tB,tG
    int shift=0;
    static int32_t shiftRight(int32_t value,unsigned count){
        if(!count)return value;
        const int32_t divisor=int32_t(1u<<count);
        return value>=0?value/divisor:-int32_t((int64_t(-value)+divisor-1)/divisor);
    }
    std::array<int32_t,2> offset(uint8_t alpha,uint8_t blue,uint8_t green) const {
        const int32_t input[3]={int(alpha)-128,int(blue)-128,int(green)-128};
        std::array<int32_t,2> result{};
        for(unsigned r=0;r<2;++r){
            int32_t sum=0;
            for(unsigned c=0;c<3;++c)sum+=coefficient[r*3+c]*input[c];
            sum=shiftRight(sum,3);
            result[r]=shift>=0?shiftRight(sum,unsigned(shift)):sum*int32_t(1u<<unsigned(-shift));
        }
        return result;
    }
};
struct WaterIndirectField {
    struct Projected {float x,y,q,u,v;}; // pixel coordinates and reciprocal W
    WaterIndirectMatrix matrix;
    uint32_t width=0,height=0,frames=0;
    float uv[6]{},velocity[2]{};
    float lodBias=0,minLOD=0,maxLOD=0;uint32_t levels=1;
    std::vector<uint8_t> abg;
    float lodAt(const std::array<Projected,3>& p,unsigned at)const{
        const float dx1=p[1].x-p[0].x,dy1=p[1].y-p[0].y,dx2=p[2].x-p[0].x,dy2=p[2].y-p[0].y;
        const float det=dx1*dy2-dx2*dy1;
        if(std::abs(det)<1.e-8f||p[at].q<=0)return -100.f;
        auto gradient=[&](float a,float b,float c){return std::array<float,2>{((b-a)*dy2-(c-a)*dy1)/det,(dx1*(c-a)-dx2*(b-a))/det};};
        const auto dq=gradient(p[0].q,p[1].q,p[2].q);
        const auto duq=gradient(p[0].u*p[0].q,p[1].u*p[1].q,p[2].u*p[2].q);
        const auto dvq=gradient(p[0].v*p[0].q,p[1].v*p[1].q,p[2].v*p[2].q);
        float rho2=0;
        for(unsigned axis=0;axis<2;++axis){
            const float du=(duq[axis]-p[at].u*dq[axis])/p[at].q;
            const float dv=(dvq[axis]-p[at].v*dq[axis])/p[at].q;
            const float ds=(uv[0]*du+uv[1]*dv)*width,dt=(uv[3]*du+uv[4]*dv)*height;
            rho2=std::max(rho2,ds*ds+dt*dt);
        }
        return .5f*std::log2(std::max(rho2,1.e-20f));
    }
    template<class Reader> bool read(Reader& reader){
        int32_t shift;
        if(!reader.read(&width,4)||!reader.read(&height,4)||!reader.read(&frames,4)||!reader.read(&shift,4)||
           !reader.read(matrix.coefficient.data(),12)||!reader.read(uv,sizeof uv)||!reader.read(velocity,sizeof velocity)||
           !reader.read(&lodBias,4)||!reader.read(&minLOD,4)||!reader.read(&maxLOD,4)||!reader.read(&levels,4))return false;
        if(!width||width>256||!height||height>256||frames>65535||shift<0||shift>17)return false;
        for(auto c:matrix.coefficient)if(c< -1024||c>1023)return false;
        for(float x:uv)if(!std::isfinite(x))return false;
        for(float x:velocity)if(!std::isfinite(x))return false;
        if(!levels||levels>9||!std::isfinite(lodBias)||!std::isfinite(minLOD)||!std::isfinite(maxLOD)||minLOD<0||maxLOD<minLOD||maxLOD>levels-1)return false;
        size_t bytes=0;for(unsigned l=0;l<levels;++l)bytes+=size_t(std::max<uint32_t>(1,width>>l))*std::max<uint32_t>(1,height>>l)*3;
        matrix.shift=shift;abg.resize(bytes);
        return reader.read(abg.data(),abg.size());
    }
    std::array<float,3> filtered(float s,float t,unsigned level)const{
        size_t base=0;for(unsigned i=0;i<level;++i)base+=size_t(std::max<uint32_t>(1,width>>i))*std::max<uint32_t>(1,height>>i)*3;
        const unsigned w=std::max<uint32_t>(1,width>>level),h=std::max<uint32_t>(1,height>>level);
        s=(s-std::floor(s))*w-.5f;t=(t-std::floor(t))*h-.5f;
        const int x=int(std::floor(s)),y=int(std::floor(t));const float fx=s-x,fy=t-y;
        auto pixel=[&](int px,int py,unsigned c){return abg[base+((unsigned(py+int(h))%h)*w+unsigned(px+int(w))%w)*3+c];};
        std::array<float,3> value{};
        for(unsigned c=0;c<3;++c){
            const float top=pixel(x,y,c)*(1-fx)+pixel(x+1,y,c)*fx;
            const float bottom=pixel(x,y+1,c)*(1-fx)+pixel(x+1,y+1,c)*fx;
            value[c]=top*(1-fy)+bottom*fy;
        }
        return value;
    }
    std::array<int32_t,2> sample(float waveU,float waveV,uint32_t frame,float lod=-100.f)const{
        const float tick=frames?float(frame%frames):0;
        const float s=uv[0]*waveU+uv[1]*waveV+uv[2]+velocity[0]*tick;
        const float t=uv[3]*waveU+uv[4]*waveV+uv[5]+velocity[1]*tick;
        // LINEAR_MIP_LINEAR: interpolate the original mip texels before GX's
        // byte rounding and matrix, never the resulting displacement vectors.
        lod=std::clamp(lod+lodBias,minLOD,maxLOD);
        const unsigned lo=unsigned(lod),hi=std::min<uint32_t>(lo+1,levels-1);const float blend=lod-lo;
        const auto a=filtered(s,t,lo),b=filtered(s,t,hi);uint8_t value[3];
        for(unsigned c=0;c<3;++c)value[c]=uint8_t(std::nearbyint(a[c]*(1-blend)+b[c]*blend));
        return matrix.offset(value[0],value[1],value[2]);
    }
};
}
