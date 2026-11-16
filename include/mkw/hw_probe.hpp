#pragma once
// DIAG hardware probes for Old 3DS planning (Nov 6), run once at start-up when
// the game behaves as an Old 3DS (a real one or the o3dstest build):
//  - memory: a pointer chase over 8 KiB (L1-resident) and 8 MiB (FCRAM, no
//    L2 on Old 3DS), 32-byte lines in a random cycle: ns per dependent load.
//  - core 1: a thread on the system core spins for 1 s at the app's 30% time
//    limit (as audio uses it); the share of a core-0 spin it reaches, and the
//    longest gap between its clock reads (how long the system preempts it).
// Codiak540's two-core kart step and packed-prism plan both depend on these.
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
namespace mkw::hwprobe {
inline double chaseNs(size_t bytes,unsigned loads){
    const size_t lines=bytes/32;if(lines<2)return 0;
    auto* mem=static_cast<uint32_t*>(std::malloc(bytes+32));if(!mem)return -1;
    auto* base=reinterpret_cast<uint32_t*>((reinterpret_cast<uintptr_t>(mem)+31)&~uintptr_t(31));
    // A random single cycle through every line (Sattolo's shuffle).
    auto* order=static_cast<uint32_t*>(std::malloc(lines*4));if(!order){std::free(mem);return -1;}
    for(size_t i=0;i<lines;++i)order[i]=uint32_t(i);
    uint32_t seed=0x9e3779b9u;
    for(size_t i=lines-1;i>0;--i){seed=seed*1664525u+1013904223u;const size_t j=seed%i;const uint32_t t=order[i];order[i]=order[j];order[j]=t;}
    for(size_t i=0;i<lines;++i)base[order[i]*8]=order[(i+1)%lines]*8;
    std::free(order);
    uint32_t at=0;for(unsigned i=0;i<lines;++i)at=base[at]; // warm (and TLB) pass
    const u64 t0=svcGetSystemTick();
    for(unsigned i=0;i<loads;++i)at=base[at];
    const u64 t1=svcGetSystemTick();
    volatile uint32_t sink=at;(void)sink;
    std::free(mem);
    return double(t1-t0)*1e9/SYSCLOCK_ARM11/loads;
}
struct SpinResult {volatile u64 count=0;volatile u64 maxGap=0;volatile bool stop=false;};
inline void spinEntry(void* arg){
    auto& r=*static_cast<SpinResult*>(arg);u64 last=svcGetSystemTick();u64 n=0,gap=0;
    while(!r.stop){const u64 now=svcGetSystemTick();if(now-last>gap)gap=now-last;last=now;++n;}
    r.count=n;r.maxGap=gap;
}
// Writes one line into text: "hw probe: ...".
inline void run(char* text,size_t size){
    const double l1=chaseNs(8u<<10,200000),fcram=chaseNs(8u<<20,200000);
    // Core 0 reference: the same loop for 200 ms on this thread.
    u64 ref=0;{const u64 t0=svcGetSystemTick();u64 last=t0;while(true){const u64 now=svcGetSystemTick();last=now;++ref;if(now-t0>=SYSCLOCK_ARM11/5)break;}(void)last;}
    const double refPerSec=double(ref)*5;
    u32 limit=0;APT_GetAppCpuTimeLimit(&limit);if(limit<30)APT_SetAppCpuTimeLimit(30);APT_GetAppCpuTimeLimit(&limit);
    SpinResult r;s32 prio=0x30;svcGetThreadPriority(&prio,CUR_THREAD_HANDLE);
    double share=-1,maxGapMs=-1;
    if(Thread t=threadCreate(spinEntry,&r,8*1024,prio+1,1,false)){
        svcSleepThread(1000000000LL);r.stop=true;threadJoin(t,U64_MAX);threadFree(t);
        share=refPerSec>0?double(r.count)/refPerSec:-1;maxGapMs=double(r.maxGap)*1000.0/SYSCLOCK_ARM11;
    }
    std::snprintf(text,size,"hw probe: load ns L1-resident %.1f, FCRAM 8 MiB %.1f; core 1 at %lu%% limit: %.1f%% of a core-0 spin over 1 s, longest preemption %.2f ms",
                  l1,fcram,(unsigned long)limit,share*100,maxGapMs);
}
}
