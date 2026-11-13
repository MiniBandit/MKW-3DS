#pragma once
#include <3ds.h>
#include <atomic>
#include <cstdint>
namespace mkw {
// Frame-local work for a second CPU core (core 1 on every model).
// A job reads state that nothing modifies until it finishes (the simulation is
// not stepping) and writes only outputs no other job touches. The thread that
// needs a result runs the job itself unless the worker already claimed it, so
// results never depend on which core ran a job, and a slow or absent worker
// only removes overlap.
struct Job {
    void (*run)(void*)=nullptr;
    void* context=nullptr;
    std::atomic<uint8_t> state{0}; // 0 pending, 1 running, 2 done
    void reset(void (*f)(void*),void* c){run=f;context=c;state.store(0,std::memory_order_relaxed);}
};
class JobWorker {
public:
    static constexpr unsigned Capacity=64;
    bool start();
    void stop();
    bool running() const { return thread_!=nullptr; }
    // Queue jobs in the order they will be needed. Jobs stay valid until done.
    void submit(Job& job);
    // Run the job here if no core has claimed it, else wait until it is done.
    void finish(Job& job);
    // Run the job here if no core has claimed it; never waits. True once done.
    bool help(Job& job){if(claim(job)){execute(job);return true;}return job.state.load(std::memory_order_acquire)==2;}
    // Finish every submitted job and fold the worker's render metrics in.
    void finishAll();
private:
    static void entry(void* self);
    void loop();
    static bool claim(Job& job){uint8_t pending=0;return job.state.compare_exchange_strong(pending,1,std::memory_order_acquire);}
    static void execute(Job& job){job.run(job.context);job.state.store(2,std::memory_order_release);}
    Thread thread_=nullptr;
    Job* jobs_[Capacity]{};
    std::atomic<unsigned> count_{0},next_{0};
    std::atomic<bool> quit_{false},busy_{false};
    uint32_t fpscr_=0;
    void* metrics_=nullptr; // the worker thread's RenderMetrics
    LightEvent wake_{},done_{};
};
JobWorker& jobWorker();
}
