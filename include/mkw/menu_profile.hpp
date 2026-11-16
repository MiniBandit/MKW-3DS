#pragma once
#include <mkw/renderer.hpp>
#include <mkw/render_metrics.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace mkw {
// Diagnostic overlay: menu timing in fixed memory, always on in QA builds.
// Instead of one row per frame (24,000 rows, filled by race frames within a
// Grand Prix), consecutive frames of one page and transition state form a
// segment with averages, maxima and late-frame counts; the slowest frames are
// kept whole. Files are rewritten at each race start and at exit, outside
// measured menu frames:
//   menu-performance.csv           one row per page (all visits)
//   menu-performance-segments.csv  one row per visit or transition
//   menu-performance-spikes.csv    the slowest menu frames, every stage
//   menu-performance-events.txt    load stages, slow loader jobs, sound stats
class MenuProfile {
public:
 enum Stage {Input,Compose,Resident,Textures,Movies,Audio,Models,Begin,Background,Scene,Foreground,Submit,StageCount};
 static constexpr const char* StageNames[StageCount]={"input","compose","resident","textures","movies","audio","models","begin","background","scene","foreground","submit"};
 static constexpr const char* PageNames[]={"Title","MainMenu","Mode","Character","Vehicle","Drift","Cup","Course","Confirm","Race","Pause","QuitConfirm","Results","Error","Strap","License","EngineClass","VSMode","BattleMode","BattleCup","BattleStage","Ghost","Intro","DriftHelp","VSRules","TeamConfirm","Settings","LicenseSettings","EraseLicense","RecordsGeneral","RecordsFavorites","RecordsOther","Unlock","BattleRules"};
 void enable(){enabled_=true;segments_.reserve(MaxSegments);events_.reserve(256);}
#ifdef MKW_RELEASE
 bool enabled()const{return false;}
#else
 bool enabled()const{return enabled_;}
#endif
 void hold(){row_.hold=true;}
 void begin(unsigned page,bool transition){
  if(!enabled())return;
  const auto now=svcGetSystemTick();
  row_={};row_.page=page;row_.transition=transition;row_.interval=previous_?ms(now-previous_):0;row_.frame=frame_++;
  previous_=stamp_=now;renderMetrics={};
 }
 void mark(Stage stage){if(!enabled())return;const auto now=svcGetSystemTick();row_.ms[stage]+=ms(now-stamp_);stamp_=now;}
 void end(const Renderer& renderer,unsigned quads){
  if(!enabled())return;
  auto& r=row_;
  r.draws=renderer.drawCalls;r.quads=quads;r.gpu=renderer.previousGpuMs;r.flushBytes=renderMetrics.flushBytes;r.skinned=renderMetrics.skinnedVertices;
  r.skinMs=renderMetrics.skinMs;r.environmentMs=renderMetrics.environmentMs;r.drawMs=renderMetrics.drawMs;
  // A new segment: another page, a transition starting or ending, or a gap.
  if(!open_||open_->page!=r.page||open_->transition!=r.transition||!r.interval){
   if(segments_.size()<MaxSegments){segments_.push_back({});open_=&segments_.back();open_->page=r.page;open_->transition=r.transition;open_->firstFrame=r.frame;open_->linearFree=linearSpaceFree();}
   else open_=nullptr;
  }
  if(open_&&r.interval>0){
   auto& s=*open_;++s.frames;s.time+=r.interval;s.intervalMax=std::max(s.intervalMax,r.interval);
   if(r.interval>Refresh*1.5f)++s.late1;
   if(r.interval>Refresh*2.5f)++s.late2;
   s.holds+=r.hold;s.gpu+=r.gpu;s.gpuMax=std::max(s.gpuMax,r.gpu);s.draws+=r.draws;s.quads+=r.quads;s.skinned+=r.skinned;s.flushBytes+=r.flushBytes;
   for(unsigned i=0;i<StageCount;++i){s.ms[i]+=r.ms[i];s.msMax[i]=std::max(s.msMax[i],r.ms[i]);}
  }
  // DIAG: every Character-page frame over two refreshes, or whose own work took
  // over 25 ms (its stall shows in the next frame's interval).
  if(r.page==3){float work=0;for(float v:r.ms)work+=v;
   if(r.interval>33.f||work>25.f){char text[200];std::snprintf(text,sizeof text,"slow character frame interval %.1f ms work %.1f ms resident %.1f textures %.1f scene %.1f submit %.1f gpu %.1f transition %u",
    r.interval,work,r.ms[Resident],r.ms[Textures],r.ms[Scene],r.ms[Submit],r.gpu,unsigned(r.transition));event(text);}}
  // The slowest frames, whole.
  if(r.interval>0){
   if(spikes_<SpikeCount)spike_[spikes_++]=r;
   else{unsigned low=0;for(unsigned i=1;i<SpikeCount;++i)if(spike_[i].interval<spike_[low].interval)low=i;if(r.interval>spike_[low].interval)spike_[low]=r;}
  }
 }
 // Races have their own profiler; the next menu frame starts a new segment.
 void raceFrame(float){previous_=0;open_=nullptr;}
 void resetInterval(){previous_=0;}
 void event(const char* text){if(enabled()&&events_.size()<MaxEvents)events_.emplace_back(frame_,text);}
 void write(const char* path)const{
  if(!enabled())return;
  // `path` names menu-performance.csv; the other files sit beside it.
  std::string base(path);if(base.size()>4)base.resize(base.size()-4);
  auto name=[](unsigned page){return page<std::size(PageNames)?PageNames[page]:"?";};
  struct Total {unsigned visits=0,frames=0,late1=0,late2=0,transitionFrames=0;float time=0,intervalMax=0,gpu=0,gpuMax=0;std::array<float,StageCount> ms{};};
  std::array<Total,std::size(PageNames)> totals{};
  for(const auto& s:segments_){if(s.page>=totals.size())continue;auto& t=totals[s.page];t.visits+=!s.transition;t.frames+=s.frames;t.late1+=s.late1;t.late2+=s.late2;
   t.transitionFrames+=s.transition?s.frames:0;t.time+=s.time;t.intervalMax=std::max(t.intervalMax,s.intervalMax);t.gpu+=s.gpu;t.gpuMax=std::max(t.gpuMax,s.gpuMax);
   for(unsigned i=0;i<StageCount;++i)t.ms[i]+=s.ms[i];}
  if(FILE* f=std::fopen(path,"w")){
   std::fputs("page,name,visits,frames,transition_frames,avg_fps,late_frames,very_late_frames,max_interval_ms,gpu_ms,gpu_max_ms",f);
   for(auto* n:StageNames)std::fprintf(f,",%s_ms",n);std::fputc('\n',f);
   for(unsigned p=0;p<totals.size();++p){const auto& t=totals[p];if(!t.frames)continue;const float n=float(t.frames);
    std::fprintf(f,"%u,%s,%u,%u,%u,%.2f,%u,%u,%.2f,%.3f,%.3f",p,name(p),t.visits,t.frames,t.transitionFrames,t.time>0?1000.f*n/t.time:0.f,t.late1,t.late2,t.intervalMax,t.gpu/n,t.gpuMax);
    for(float v:t.ms)std::fprintf(f,",%.3f",v/n);std::fputc('\n',f);}
   std::fclose(f);
  }
  if(FILE* f=std::fopen((base+"-segments.csv").c_str(),"w")){
   std::fputs("segment,first_frame,page,name,transition,frames,avg_fps,late_frames,very_late_frames,max_interval_ms,holds,gpu_ms,gpu_max_ms,draws,quads,skinned,flush_kb,linear_free_kb",f);
   for(auto* n:StageNames)std::fprintf(f,",%s_ms",n);
   for(auto* n:StageNames)std::fprintf(f,",%s_max_ms",n);std::fputc('\n',f);
   for(size_t i=0;i<segments_.size();++i){const auto& s=segments_[i];if(!s.frames)continue;const float n=float(s.frames);
    std::fprintf(f,"%u,%u,%u,%s,%u,%u,%.2f,%u,%u,%.2f,%u,%.3f,%.3f,%.1f,%.1f,%.0f,%.1f,%u",unsigned(i),s.firstFrame,s.page,name(s.page),unsigned(s.transition),s.frames,
     s.time>0?1000.f*n/s.time:0.f,s.late1,s.late2,s.intervalMax,s.holds,s.gpu/n,s.gpuMax,s.draws/n,s.quads/n,s.skinned/n,s.flushBytes/n/1024.f,s.linearFree/1024);
    for(float v:s.ms)std::fprintf(f,",%.3f",v/n);
    for(float v:s.msMax)std::fprintf(f,",%.3f",v);std::fputc('\n',f);}
   std::fclose(f);
  }
  if(FILE* f=std::fopen((base+"-spikes.csv").c_str(),"w")){
   std::fputs("frame,page,name,transition,hold,interval_ms,gpu_ms,draws,quads,skinned,flush_kb,skin_ms,env_ms,model_draw_ms",f);
   for(auto* n:StageNames)std::fprintf(f,",%s_ms",n);std::fputc('\n',f);
   std::array<unsigned,SpikeCount> order{};for(unsigned i=0;i<spikes_;++i)order[i]=i;
   std::sort(order.begin(),order.begin()+spikes_,[&](unsigned a,unsigned b){return spike_[a].interval>spike_[b].interval;});
   for(unsigned k=0;k<spikes_;++k){const auto& r=spike_[order[k]];
    std::fprintf(f,"%u,%u,%s,%u,%u,%.3f,%.3f,%u,%u,%u,%u,%.3f,%.3f,%.3f",r.frame,r.page,name(r.page),unsigned(r.transition),unsigned(r.hold),r.interval,r.gpu,r.draws,r.quads,r.skinned,r.flushBytes/1024,r.skinMs,r.environmentMs,r.drawMs);
    for(float v:r.ms)std::fprintf(f,",%.3f",v);std::fputc('\n',f);}
   std::fclose(f);
  }
  if(FILE* f=std::fopen((base+"-events.txt").c_str(),"w")){
   for(const auto& e:events_)std::fprintf(f,"%u %s\n",e.first,e.second.c_str());
   if(events_.size()==MaxEvents)std::fputs("(event limit reached)\n",f);
   std::fclose(f);
  }
 }
private:
 static float ms(uint64_t ticks){return float(double(ticks)*1000.0/SYSCLOCK_ARM11);}
 static constexpr unsigned MaxSegments=1024,SpikeCount=48,MaxEvents=2000;
 static constexpr float Refresh=1000.f/59.83f;
 struct Row {unsigned frame=0,page=0;bool transition=false,hold=false;float interval=0,gpu=0;std::array<float,StageCount> ms{};unsigned draws=0,quads=0,skinned=0,flushBytes=0;float skinMs=0,environmentMs=0,drawMs=0;};
 struct Segment {unsigned page=0,firstFrame=0,frames=0,late1=0,late2=0,holds=0,linearFree=0;bool transition=false;float time=0,intervalMax=0,gpu=0,gpuMax=0,draws=0,quads=0,skinned=0,flushBytes=0;
  std::array<float,StageCount> ms{},msMax{};};
 std::vector<Segment> segments_;Segment* open_=nullptr;
 std::array<Row,SpikeCount> spike_{};unsigned spikes_=0;
 std::vector<std::pair<unsigned,std::string>> events_;Row row_{};
 uint64_t previous_=0,stamp_=0;unsigned frame_=0;bool enabled_=false;
};
}
