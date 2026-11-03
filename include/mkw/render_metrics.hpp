#pragma once
#include <cstdint>
#ifdef __3DS__
#include <3ds.h>
#endif
namespace mkw {
struct RenderMetrics {
 uint32_t animationHolds=0,scenePoseSamples=0,scenePoseReuses=0,fireReads=0;
 uint32_t flushCalls=0,flushBytes=0,poseEvaluations=0,poseReuses=0,skinnedVertices=0;
 // Menu model timing split (QA profile only): skin, environment, draw.
 float skinMs=0,environmentMs=0,drawMs=0;
 // Race profile: job worker busy time (its jobs, folded in by finishAll).
 float workerMs=0;
 uint32_t contactDraws=0,contactTriangles=0;
 float contactMs=0,shadowSupportMs=0;
};
// Per thread: JobWorker::finishAll folds the worker core's counts in.
inline thread_local RenderMetrics renderMetrics;
// Call only with CPU-written GPU-visible data. Static geometry and textures
// flush when loaded; dynamic ranges flush before their first submitted draw.
inline void flushGpuData(void* data,uint32_t bytes){
 if(!bytes)return;
 ++renderMetrics.flushCalls;renderMetrics.flushBytes+=bytes;
 GSPGPU_FlushDataCache(data,bytes);
}
}
