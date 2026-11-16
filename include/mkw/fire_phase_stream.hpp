#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <mkw/asset_pack.hpp>
#include <3ds.h>
namespace mkw {
// Course-local cache. File I/O ends before open returns; read is memcpy only.
// Memory pressure reduces temporal samples, never resolution or loop speed.
class FirePhaseStream {
    unsigned char* cache_=nullptr;unsigned char* block_=nullptr;unsigned stride_=1;size_t bytes_=0;bool blockLinear_=false;
    // Heap first, then LINEAR (the Old 3DS heap can't hold the stream).
    static unsigned char* allocate(size_t bytes,bool& linear){
        if(auto* p=static_cast<unsigned char*>(std::malloc(bytes))){linear=false;return p;}
        linear=true;return static_cast<unsigned char*>(linearAlloc(bytes));
    }
    static void release(unsigned char* p,bool linear){if(linear)linearFree(p);else std::free(p);}
    bool accept(const uint32_t* h){
        float scale,bias;std::memcpy(&scale,h+8,4);std::memcpy(&bias,h+9,4);
        if(std::memcmp(h,"FTAN",4)||h[1]!=1||h[2]!=256||h[3]!=256||h[4]!=5||h[5]!=100||h[6]!=131072||h[7]!=40)return false;
        if(!std::isfinite(scale)||!std::isfinite(bias)||std::fabs(scale-16.f/17.f)>=1e-7f||std::fabs(bias-1.f/34.f)>=1e-7f)return false;
        width=h[2];height=h[3];frames=h[5];frameBytes=h[6];uvScale=scale;uvBias=bias;return true;
    }
public:
    uint32_t width=0,height=0,frames=0,frameBytes=0;
    float uvScale=0,uvBias=0;
    FirePhaseStream()=default;
    FirePhaseStream(const FirePhaseStream&)=delete;
    FirePhaseStream& operator=(const FirePhaseStream&)=delete;
    ~FirePhaseStream(){if(block_)release(block_,blockLinear_);}
    unsigned phase(unsigned tick)const{return (tick%frames)/stride_*stride_;}
    unsigned stride()const{return stride_;}
    size_t bytes()const{return bytes_;}
    bool open(const char* path,size_t budget=13107200){
        if(cache_)return false;
        // A repacked race.pak (tools/pakpack.c) holds the stream compressed:
        // read it whole when every frame fits, header included.
        constexpr long fileBytes=40+100*131072;
        if(pack::size(path)==fileBytes){
            if(size_t(fileBytes-40)<=budget){
                bool wholeLinear=false;
                if(auto* whole=allocate(fileBytes,wholeLinear)){
                    uint32_t h[10];
                    if(pack::readInto(path,whole,fileBytes)&&(std::memcpy(h,whole,sizeof h),accept(h))){
                        block_=whole;blockLinear_=wholeLinear;cache_=whole+40;stride_=1;bytes_=size_t(fileBytes-40);return true;
                    }
                    release(whole,wholeLinear);
                }
                return false;
            }
            // Over budget (Old 3DS): only every stride-th phase, never the whole
            // 13 MB stream. A compressed entry decodes through a 64 KiB window
            // straight into the kept phases; a raw one is read phase by phase.
            constexpr unsigned allFrames=100;constexpr size_t oneFrame=131072;
            const long stored=pack::storedSize(path);
            for(unsigned step:{2u,4u,10u,20u,100u}){
                const size_t bytes=size_t(allFrames/step)*oneFrame;
                if(bytes>budget)continue;
                if(!(block_=allocate(bytes,blockLinear_)))continue;
                bool ok=false;uint32_t h[10]{};
                if(stored>0&&stored<fileBytes){
                    // Streamed from the card: the 6.2 MB entry never sits in memory whole.
                    ok=pack::decodeLz4Frames(path,size_t(fileBytes),reinterpret_cast<unsigned char*>(h),sizeof h,oneFrame,step,allFrames/step,block_);
                }else if(stored==fileBytes){
                    ok=pack::readRange(path,0,h,sizeof h);
                    for(unsigned i=0;ok&&i<allFrames/step;++i)ok=pack::readRange(path,sizeof h+size_t(i)*step*oneFrame,block_+size_t(i)*oneFrame,oneFrame);
                }
                if(ok&&accept(h)&&frames==allFrames&&frameBytes==oneFrame){cache_=block_;stride_=step;bytes_=bytes;return true;}
                release(block_,blockLinear_);block_=nullptr;
                return false;
            }
            return false;
        }
        FILE* f=std::fopen(path,"rb");if(!f)return false;
        uint32_t h[10]{};
        bool ok=std::fread(h,1,sizeof h,f)==sizeof h&&accept(h);
        if(ok)ok=std::fseek(f,0,SEEK_END)==0&&std::ftell(f)==long(40+h[5]*h[6]);
        if(!ok){std::fclose(f);return false;}
        for(unsigned step:{1u,2u,4u,10u,20u,100u}){
            const size_t bytes=size_t(frames/step)*frameBytes;
            if(bytes>budget)continue;
            cache_=block_=allocate(bytes,blockLinear_);
            if(cache_){stride_=step;bytes_=bytes;break;}
        }
        if(!cache_){std::fclose(f);return false;}
        for(unsigned i=0;i<frames/stride_;++i){
            if(std::fseek(f,long(40+size_t(i)*stride_*frameBytes),SEEK_SET)!=0||
               std::fread(cache_+size_t(i)*frameBytes,1,frameBytes,f)!=frameBytes){
                std::fclose(f);release(block_,blockLinear_);block_=cache_=nullptr;bytes_=0;return false;
            }
        }
        std::fclose(f);return true;
    }
    // The bytes read() copies (frameBytes), in the cache; null if read() would fail.
    const void* frame(unsigned phase)const{return cache_&&phase<frames?cache_+size_t(phase/stride_)*frameBytes:nullptr;}
    bool read(unsigned phase,void* target,size_t capacity){
        if(!cache_||!target||phase>=frames||capacity<frameBytes)return false;
        std::memcpy(target,cache_+size_t(phase/stride_)*frameBytes,frameBytes);return true;
    }
    void mapUv(float* uv)const{
        for(unsigned i=0;i<6;++i)uv[i]*=uvScale;
        uv[2]+=uvBias;uv[5]+=uvBias;
    }
};
}
