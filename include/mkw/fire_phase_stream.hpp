#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
namespace mkw {
// Seekable cooked fireBPlane phases. Never loads the full animation into RAM.
class FirePhaseStream {
    FILE* file_=nullptr;
public:
    uint32_t width=0,height=0,frames=0,frameBytes=0;
    float uvScale=0,uvBias=0;
    FirePhaseStream()=default;
    FirePhaseStream(const FirePhaseStream&)=delete;
    FirePhaseStream& operator=(const FirePhaseStream&)=delete;
    ~FirePhaseStream(){if(file_)std::fclose(file_);}
    bool open(const char* path){
        if(file_)return false;
        FILE* f=std::fopen(path,"rb");if(!f)return false;
        uint32_t h[10]{};
        bool ok=std::fread(h,1,sizeof h,f)==sizeof h;
        float scale,bias;std::memcpy(&scale,h+8,4);std::memcpy(&bias,h+9,4);
        ok=ok&&!std::memcmp(h,"FTAN",4)&&h[1]==1&&h[2]==256&&h[3]==256&&h[4]==5&&h[5]==100&&h[6]==131072&&h[7]==40;
        ok=ok&&std::isfinite(scale)&&std::isfinite(bias)&&std::fabs(scale-16.f/17.f)<1e-7f&&std::fabs(bias-1.f/34.f)<1e-7f;
        if(ok)ok=std::fseek(f,0,SEEK_END)==0&&std::ftell(f)==long(40+h[5]*h[6]);
        if(!ok){std::fclose(f);return false;}
        file_=f;width=h[2];height=h[3];frames=h[5];frameBytes=h[6];uvScale=scale;uvBias=bias;return true;
    }
    bool read(unsigned phase,void* target,size_t capacity){
        if(!file_||!target||phase>=frames||capacity<frameBytes)return false;
        return std::fseek(file_,long(40+phase*frameBytes),SEEK_SET)==0&&std::fread(target,1,frameBytes,file_)==frameBytes;
    }
    void mapUv(float* uv)const{
        for(unsigned i=0;i<6;++i)uv[i]*=uvScale;
        uv[2]+=uvBias;uv[5]+=uvBias;
    }
};
}
