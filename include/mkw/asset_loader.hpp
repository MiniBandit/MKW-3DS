#pragma once
#include <3ds.h>
#include <array>
#include <atomic>
#include <deque>
#include <memory>

namespace mkw {
// Work executed by the background asset thread. run() may read files and
// allocate LINEAR memory (serialized by linear_lock.cpp) but must never touch
// Citro3D context state or submit GPU work. Results are published by done().
class AssetJob {
public:
 virtual ~AssetJob()=default;
 virtual void run()=0;
 bool done()const{return state_.load(std::memory_order_acquire)==Done;}
 bool queued()const{return state_.load(std::memory_order_acquire)==Queued;}
 void cancel(){cancelled_.store(true,std::memory_order_release);}
 // QA: label and measured wall time (queue wait excluded) for slow-job reports.
 const char* label="job";float runMs=0,waitMs=0;
 virtual const char* detail()const{return "";}
 bool cancelled()const{return cancelled_.load(std::memory_order_acquire);}
private:
 friend class AssetLoader;
 enum State {Queued,Running,Done};
 std::atomic<int> state_{Queued};
 std::atomic<bool> cancelled_{false};
 int priority_=-1;
};

// Realtime is reserved for audio refills, which must never queue behind pages.
enum class AssetPriority {Realtime=0,Urgent=1,Normal=2,Prefetch=3};

// One worker below the main thread's priority on the application core: it
// runs while the main thread sleeps in VBlank/GPU waits, so loading never
// lengthens a presented frame. Jobs within a priority run in FIFO order.
class AssetLoader {
public:
 static AssetLoader& instance();
 bool start(int priorityOffset=2);
 // Drops queued jobs (they complete as cancelled) and joins the worker.
 void stop();
 bool running()const{return thread_!=nullptr;}
 // A visible page waiting for assets takes precedence over preview work.
 void setForeground(bool foreground);
 // Queues a job, or raises an already queued job to a higher priority.
 void submit(const std::shared_ptr<AssetJob>& job,AssetPriority priority);
 // Discards queued prefetch work and asks a running prefetch to stop at its
 // next checkpoint, e.g. after the user moves on.
 void cancelPrefetch();
 unsigned pending()const;
 // QA: jobs that ran longer than 25 ms since the last call ("label ms").
 void drainSlow(void (*sink)(const char*,const char*,float,float,void*),void* context);
private:
 AssetLoader();
 static void entry(void*);
 void loop();
 std::shared_ptr<AssetJob> next();
 Thread thread_=nullptr;
 int backgroundPriority_=0x32,foregroundPriority_=0x2f;bool foreground_=false;
 mutable LightLock lock_;
 LightEvent wake_;
 std::array<std::deque<std::shared_ptr<AssetJob>>,4> queues_;
 std::shared_ptr<AssetJob> running_;
 struct Slow {const char* label;char detail[96];float run,wait;};std::array<Slow,32> slow_{};unsigned slowCount_=0;
 std::atomic<bool> stopping_{false};
};
}
