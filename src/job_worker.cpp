#include <mkw/job_worker.hpp>
#include <mkw/render_metrics.hpp>
#include <mkw/skin_positions.hpp>
#include <algorithm>

namespace mkw {
JobWorker& jobWorker(){static JobWorker worker;return worker;}
void JobWorker::entry(void* self){static_cast<JobWorker*>(self)->loop();}
bool JobWorker::start(){
    if(thread_)return true;
    LightEvent_Init(&wake_,RESET_ONESHOT);LightEvent_Init(&done_,RESET_ONESHOT);
    // Same VFP mode (flush-to-zero, default NaN, rounding) as the main thread,
    // so a job computes identical floats on either core.
    fpscr_=__builtin_arm_get_fpscr();
    bool n3ds=false;APT_CheckNew3DS(&n3ds);
    // Old 3DS core 1 is time sliced with the system; the audio threads there
    // already require a limit (sound_system.cpp). Higher-priority system and
    // audio threads still preempt this one.
    if(!n3ds){u32 limit=0;APT_GetAppCpuTimeLimit(&limit);if(limit<80)APT_SetAppCpuTimeLimit(80);}
    // New 3DS: prepare on the main core while it waits for jobs/render slots.
    // Keep the render core free of higher-priority animation jobs. finish()
    // still executes unclaimed work immediately; physics preempts this worker.
    s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
    thread_=threadCreate(entry,this,32*1024,std::clamp<s32>(priority+1,0x1a,0x3f),n3ds?0:1,false);
    return thread_!=nullptr;
}
void JobWorker::stop(){
    if(!thread_)return;
    finishAll();
    quit_.store(true,std::memory_order_release);LightEvent_Signal(&wake_);
    threadJoin(thread_,U64_MAX);threadFree(thread_);thread_=nullptr;quit_.store(false);
}
void JobWorker::submit(Job& job){
    const unsigned n=count_.load(std::memory_order_relaxed);
    if(!thread_||n>=Capacity)return; // finish() runs it on this thread
    jobs_[n]=&job;count_.store(n+1,std::memory_order_release);
    LightEvent_Signal(&wake_);
}
void JobWorker::finish(Job& job){
    if(claim(job)){execute(job);return;}
    while(job.state.load(std::memory_order_acquire)!=2){
        if(LightEvent_WaitTimeout(&done_,20000000LL))svcSleepThread(500000LL);
    }
}
void JobWorker::finishAll(){
    const unsigned n=count_.load(std::memory_order_relaxed);
    for(unsigned i=0;i<n;++i)finish(*jobs_[i]);
    // The worker leaves its claim loop before the queue indices are reused.
    while(busy_.load(std::memory_order_acquire)){
        if(LightEvent_WaitTimeout(&done_,20000000LL))svcSleepThread(500000LL);
    }
    count_.store(0,std::memory_order_relaxed);next_.store(0,std::memory_order_relaxed);
    if(auto* worker=static_cast<RenderMetrics*>(metrics_)){
        renderMetrics.animationHolds+=worker->animationHolds;renderMetrics.scenePoseSamples+=worker->scenePoseSamples;
        renderMetrics.scenePoseReuses+=worker->scenePoseReuses;renderMetrics.fireReads+=worker->fireReads;
        renderMetrics.flushCalls+=worker->flushCalls;renderMetrics.flushBytes+=worker->flushBytes;
        renderMetrics.poseEvaluations+=worker->poseEvaluations;renderMetrics.poseReuses+=worker->poseReuses;
        renderMetrics.skinnedVertices+=worker->skinnedVertices;renderMetrics.workerMs+=worker->workerMs;*worker={};
    }
}
void JobWorker::loop(){
    __builtin_arm_set_fpscr(fpscr_);
    metrics_=&renderMetrics;positionScratchIndex=1;
    for(;;){
        LightEvent_Wait(&wake_);
        if(quit_.load(std::memory_order_acquire))break;
        busy_.store(true,std::memory_order_release);
        for(;;){
            unsigned i=next_.load(std::memory_order_acquire);
            while(i<count_.load(std::memory_order_acquire)&&!next_.compare_exchange_weak(i,i+1,std::memory_order_acq_rel)){}
            if(i>=count_.load(std::memory_order_acquire))break;
            Job& job=*jobs_[i];
            if(claim(job)){
                const auto start=svcGetSystemTick();
                execute(job);
                renderMetrics.workerMs+=float((svcGetSystemTick()-start)*1000.0/SYSCLOCK_ARM11);
                LightEvent_Signal(&done_);
            }
        }
        busy_.store(false,std::memory_order_release);LightEvent_Signal(&done_);
    }
}
}
