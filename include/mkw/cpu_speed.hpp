#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mkw {
// AIManager::init (USA8072c374..450): GP takes difficulty from engine
// class, including Mirror/150 as hard; VS takes the explicit CPU setting.
inline unsigned cpuModeForRace(bool grandPrix,unsigned engineClass,unsigned selected){
 return grandPrix?std::min(engineClass,2u):std::min(selected,2u);
}
// RMCU AISpeedRace: section1+227f80/22801c/2280b8; see CPU_SOURCE.md.
struct CpuSpeedParam { float advantage=0, bias=1, minimum=50; };
inline bool readCpuSpeedParam(const void* data,size_t size,unsigned course,unsigned engineClass,unsigned difficulty,CpuSpeedParam& out,bool grandPrix=false,bool mirror=false){
    // Retail VS: 128 GP records, then 3 engine classes * 32 courses * 3 difficulties.
    if(!data||size!=4992||course>=32||engineClass>=3||difficulty>=3)return false;
    const unsigned record=grandPrix?course*4+(mirror?3:engineClass):128+(engineClass*32+course)*3+difficulty;
    const auto* p=static_cast<const uint8_t*>(data)+12*record;
    float v[3];for(unsigned i=0;i<3;++i){uint32_t w=uint32_t(p[4*i])<<24|uint32_t(p[4*i+1])<<16|uint32_t(p[4*i+2])<<8|p[4*i+3];std::memcpy(v+i,&w,4);if(!std::isfinite(v[i]))return false;}
    if(v[1]<=0||v[2]<=0)return false;out={v[0],v[1],v[2]};return true;
}
inline float cpuTargetGap(unsigned targetRank,float rankOffset,float bias){
    constexpr float gaps[]={-1.f,-.5f,1.f,2.f,3.1f,4.3f,5.5f,6.8f,8.1f,9.4f,10.8f,12.2f,13.6f,15.f};
    targetRank=std::clamp(targetRank,1u,12u);rankOffset=std::clamp(rankOffset,-1.f,1.f);
    const float base=gaps[targetRank];
    const float adjacent=gaps[rankOffset<0?targetRank-1:targetRank+1];
    return 4500.f*bias*(base+std::fabs(rankOffset)*(adjacent-base));
}
// AISpeedRace's 70-unit result is a controller offset, not a replacement for
// the kart's character/vehicle stat. Keep each CPU's native speed and apply
// the recovered course adjustment on top of it.
inline float cpuBaseSpeed(float nativeSpeed,float policySpeed){
    if(!std::isfinite(nativeSpeed))return policySpeed;
    return nativeSpeed+(policySpeed-70.f);
}
struct CpuSpeedState {
    float slowdownRamp=0,openingScale=1;
    float step(const CpuSpeedParam& p,unsigned rank,unsigned targetRank,float distanceBehindLeader,
               unsigned lap,unsigned engineClass=2,unsigned difficulty=1,float rankOffset=0){
        constexpr float opening[]={2.5f,4.f,5.5f,3.5f,5.f,6.5f,4.5f,6.f,7.5f};
        slowdownRamp=std::min(1.f,slowdownRamp+.0003f);
        if(lap>1)openingScale=std::max(0.f,openingScale-.0005f);
        const float base=70.f+p.advantage+opening[std::min(engineClass,2u)*3+std::min(difficulty,2u)]*openingScale;
        if(rank==1)return base;
        const float error=cpuTargetGap(targetRank,rankOffset,p.bias)-distanceBehindLeader;
        if(error>0)return base+std::clamp(error/4800.f,0.f,1.f)*slowdownRamp*(p.minimum-base);
        return base+std::clamp(-error/4800.f,0.f,1.f)*10.f;
    }
};
}
