#include <mkw/asset_loader.hpp>
#include <algorithm>
#include <cstdio>

namespace mkw {
AssetLoader& AssetLoader::instance(){static AssetLoader value;return value;}
AssetLoader::AssetLoader(){LightLock_Init(&lock_);LightEvent_Init(&wake_,RESET_ONESHOT);}
bool AssetLoader::start(int priorityOffset){
 if(thread_)return true;
 s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
 stopping_.store(false);foreground_=false;
 backgroundPriority_=std::clamp<int>(priority+priorityOffset,0x18,0x3f);
 foregroundPriority_=std::clamp<int>(priority-1,0x18,0x3f);
 // Lower priority (higher number) than the caller, same core: never preempts
 // frame work, and consumes the main thread's VBlank/GPU idle time instead.
 thread_=threadCreate(entry,this,96*1024,backgroundPriority_,-2,false);
 return thread_!=nullptr;
}
void AssetLoader::setForeground(bool foreground){
 if(!thread_||foreground_==foreground)return;
 if(R_SUCCEEDED(svcSetThreadPriority(threadGetHandle(thread_),foreground?foregroundPriority_:backgroundPriority_)))foreground_=foreground;
}
void AssetLoader::stop(){
 if(!thread_)return;
 LightLock_Lock(&lock_);
 for(auto& queue:queues_){for(auto& job:queue){job->cancel();job->state_.store(AssetJob::Done,std::memory_order_release);}queue.clear();}
 stopping_.store(true);
 LightLock_Unlock(&lock_);
 LightEvent_Signal(&wake_);
 threadJoin(thread_,U64_MAX);threadFree(thread_);thread_=nullptr;
}
void AssetLoader::submit(const std::shared_ptr<AssetJob>& job,AssetPriority priority){
 if(!job||job->done())return;
 const int level=int(priority);
 LightLock_Lock(&lock_);
 // Promotion also applies after a job starts: a now-visible model must
 // not remain speculative and get cancelled by cancelPrefetch().
 if(job->state_.load()==AssetJob::Running && level<job->priority_)job->priority_=level;
 if(job->state_.load()==AssetJob::Queued){
  if(job->priority_>=0&&job->priority_<=level){LightLock_Unlock(&lock_);return;}
  if(job->priority_>=0){auto& old=queues_[job->priority_];old.erase(std::remove(old.begin(),old.end(),job),old.end());}
  job->priority_=level;queues_[level].push_back(job);
  if(job->waitMs==0)job->waitMs=-float(svcGetSystemTick()*1000.0/SYSCLOCK_ARM11);
 }
 LightLock_Unlock(&lock_);
 LightEvent_Signal(&wake_);
}
void AssetLoader::cancelPrefetch(){
 LightLock_Lock(&lock_);
 for(auto& job:queues_[int(AssetPriority::Prefetch)]){job->cancel();job->state_.store(AssetJob::Done,std::memory_order_release);}
 queues_[int(AssetPriority::Prefetch)].clear();
 if(running_&&running_->priority_==int(AssetPriority::Prefetch))running_->cancel();
 LightLock_Unlock(&lock_);
}
unsigned AssetLoader::pending()const{
 LightLock_Lock(&lock_);unsigned count=0;for(const auto& queue:queues_)count+=unsigned(queue.size());LightLock_Unlock(&lock_);return count;
}
void AssetLoader::drainSlow(void (*sink)(const char*,const char*,float,float,void*),void* context){
 LightLock_Lock(&lock_);const auto copy=slow_;const unsigned count=slowCount_;slowCount_=0;LightLock_Unlock(&lock_);
 for(unsigned i=0;i<count;++i)sink(copy[i].label,copy[i].detail,copy[i].run,copy[i].wait,context);
}
std::shared_ptr<AssetJob> AssetLoader::next(){
 LightLock_Lock(&lock_);std::shared_ptr<AssetJob> job;
 for(auto& queue:queues_)if(!queue.empty()){job=std::move(queue.front());queue.pop_front();break;}
 if(job)job->state_.store(AssetJob::Running,std::memory_order_release);
 running_=job;
 LightLock_Unlock(&lock_);return job;
}
void AssetLoader::entry(void* self){static_cast<AssetLoader*>(self)->loop();}
void AssetLoader::loop(){
 while(!stopping_.load()){
  auto job=next();
  if(!job){LightEvent_Wait(&wake_);continue;}
  const auto start=svcGetSystemTick();
  if(job->waitMs<0)job->waitMs+=float(start*1000.0/SYSCLOCK_ARM11);
  if(!job->cancelled())job->run();
  job->runMs=float((svcGetSystemTick()-start)*1000.0/SYSCLOCK_ARM11);
  LightLock_Lock(&lock_);running_.reset();
  if(job->runMs>25&&slowCount_<slow_.size()){auto& slow=slow_[slowCount_++];slow.label=job->label;std::snprintf(slow.detail,sizeof slow.detail,"%s",job->detail());slow.run=job->runMs;slow.wait=job->waitMs;}
  LightLock_Unlock(&lock_);
  job->state_.store(AssetJob::Done,std::memory_order_release);
 }
}
}
