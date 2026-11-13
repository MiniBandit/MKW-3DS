#pragma once
#include "mkw/console_model.hpp"
#include <mkw/renderer.hpp>
#include <mkw/render_metrics.hpp>
#include <array>
#include <mkw/animation_budget.hpp>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <mkw/render_quality.hpp>
#include <atomic>
#ifdef MKW_AUTO_PROFILE
#include <mkw/diag.hpp>
#include <mkw/race_bottom_screen.hpp>
#endif
namespace mkw {
extern std::atomic<unsigned> fileOpens;extern std::atomic<unsigned long long> fileOpenTicks;
// DIAG: count, bytes and time of reads from menu.pak/race.pak (asset_pack.cpp).
extern std::atomic<unsigned> packReads;extern std::atomic<unsigned long long> packReadBytes,packReadTicks;
// Optional physical-device/emulator evidence. Marker is on writable SD even
// for ROMFS builds; package allowlists never install it. Batched I/O keeps
// diagnostics out of every frame's hot path. Keep this object off the ARM stack.
#ifdef MKW_AUTO_PROFILE
// Diagnostic build: profiles every race automatically, no input needed.
// Recording runs from the end of the course intro until results (pauses
// suspend it). Fixed RAM only: per-second aggregates, the worst frames in
// full and an interval histogram. SD writes happen only on pause (snapshot),
// at race end and at exit, never between measured frames.
//   perf-race-NNN-seconds.csv  one row per second of racing
//   perf-race-NNN-spikes.csv   the slowest frames, every counter
//   perf-races.csv             one summary row per race (appended)
//   perf-race-NNN-passes.csv   GPU cost per pass (leave-out frames, mkw/diag.hpp)
//   perf-passes.csv            the same, one row per race and pass (appended)
class RuntimeProfile {
public:
 enum Stage {Simulation,UiPrepare,Wait,Course,Scenery,Opponents,Items,Player,Effects,Hud,Bottom,Submit,StageCount};
 static constexpr const char* StageNames[StageCount]={"simulation","ui_prepare","wait","course","scenery","opponents","items","player","effects","hud","bottom","submit"};
 ~RuntimeProfile(){write(true);}
 void start(){}
 // Called once per race-loop iteration, before begin().
 void autoFrame(bool racing){
  if(racing){if(!active_){active_=true;skip_=true;diag::passReset();diag::skinReset();diag::drawReset();diag::writesReset();diag::textureUseReset();}return;}
  if(frames_)write(true);
  active_=false;
 }
 // What this race draws: stereo frames are counted, the rest are per race.
 void context(bool stereo,bool bloom,bool stadium,bool dynamicShadows,bool pipelined,bool bloomMerge,bool shadowRect,bool playerVram,bool drawDistance,bool spatialCourse,bool hudShrink,bool skyLast,bool plainUv,bool fastVertex,bool vramPriority){
  if(!active_)return;stereoFrames_+=stereo;bloom_=bloom;stadium_=stadium;dynamicShadows_=dynamicShadows;pipelined_=pipelined;bloomMerge_=bloomMerge;shadowRect_=shadowRect;playerVram_=playerVram;drawDistance_=drawDistance;spatialCourse_=spatialCourse;hudShrink_=hudShrink;skyLast_=skyLast;plainUv_=plainUv;fastVertex_=fastVertex;vramPriority_=vramPriority;
 }
 void begin(unsigned course,unsigned frame,unsigned ticks,float interval){
  if(!active_)return;
  row_={};row_.course=course;row_.frame=frame;startTicks_=ticks;row_.interval=interval;course_=course;
  row_.passSkip=diag::passState.skip;opensAt_=fileOpens.load(std::memory_order_relaxed);openTicksAt_=fileOpenTicks.load(std::memory_order_relaxed);
  stamp_=svcGetSystemTick();
 }
 void mark(Stage stage){
  static constexpr const char* names[]={"simulation","ui","wait","course","scenery","opponents","items","player","effects","hud","bottom","submit"};
  diag::iterationMark(names[stage]);
  if(!active_)return;auto now=svcGetSystemTick();row_.ms[stage]=float((now-stamp_)*1000.0/SYSCLOCK_ARM11);stamp_=now;}
 void end(unsigned ticks,const Renderer& renderer,bool rendered=true){
  if(!active_)return;
  // The first iteration after arming or resuming measures loading/pause time.
  if(skip_){skip_=false;return;}
  auto& r=row_;
  r.ticks=ticks-startTicks_;r.rendered=rendered;r.time=elapsed_;
  r.opens=fileOpens.load(std::memory_order_relaxed)-opensAt_;r.openMs=float((fileOpenTicks.load(std::memory_order_relaxed)-openTicksAt_)*1000.0/SYSCLOCK_ARM11);
  if(rendered){r.gpu=renderer.previousGpuMs;r.draws=renderer.drawCalls;r.triangles=renderer.triangles;
   r.culledTriangles=renderer.culledTriangles;r.culledSubmeshes=renderer.culledSubmeshes;r.metrics=renderMetrics;
   r.command=renderer.commandUsage;r.renderThread=renderer.renderThreadMs;r.renderWait=renderer.beginWaitMs;r.recordWait=renderer.recordWaitMs;
   r.hook=renderer.hookMs;r.gpuGap=renderer.gpuGapMs;
   r.drawSetup=renderer.drawSetupMs;r.drawCall=renderer.drawCallMs;r.drawReset=renderer.drawResetMs;r.drawNow=renderer.drawNowMs;r.drawSetups=renderer.drawSetups;r.stateHits=renderer.stateHits;}
  accumulate(r);
 }
 // Pause: save what exists so far; the race continues in the same files.
 void finish(){if(frames_)write(false);skip_=true;}
 // Pause without writing: the race keeps its files, and the first frame
 // after resuming (which measures the pause) is skipped.
 void hold(){skip_=true;}
private:
 struct Row {float time=0;unsigned course=0,frame=0,ticks=0,rendered=0;float interval=0;std::array<float,StageCount> ms{};float gpu=0;unsigned draws=0,triangles=0,culledTriangles=0,culledSubmeshes=0;RenderMetrics metrics{};float command=0,renderThread=0,renderWait=0,recordWait=0,hook=0,gpuGap=0;unsigned passSkip=0,opens=0;float openMs=0;float drawSetup=0,drawCall=0,drawReset=0,drawNow=0;unsigned drawSetups=0,stateHits=0;};
 struct Second {unsigned loops=0,rendered=0,late1=0,late2=0,ticks=0,draws=0,triangles=0,culledTriangles=0,flushBytes=0,skinned=0,linearFree=0,opens=0,leaveOuts=0;
  float duration=0,intervalMax=0,gpu=0,gpuMax=0,command=0,renderThread=0,renderWait=0,recordWait=0,worker=0,contact=0,shadowSupport=0,openMs=0,hook=0,hookMax=0,gpuGap=0,gpuGapMax=0,gpuGapMin=0;
  float drawSetup=0,drawCall=0,drawReset=0,drawNow=0,drawSetups=0,stateHits=0;
  std::array<float,StageCount> ms{},msMax{};};
 static constexpr unsigned MaxSeconds=480,SpikeCount=32,Buckets=200; // 0.5 ms histogram buckets to 100 ms
 static constexpr float Refresh=1000.f/59.83f;
 void accumulate(const Row& r){
  ++frames_;elapsed_+=r.interval;
  ++histogram_[std::min<unsigned>(unsigned(r.interval*2),Buckets-1)];
  intervalMax_=std::max(intervalMax_,r.interval);
  auto& s=current_;
  ++s.loops;s.duration+=r.interval;s.intervalMax=std::max(s.intervalMax,r.interval);s.ticks+=r.ticks;
  s.opens+=r.opens;s.openMs+=r.openMs;s.leaveOuts+=r.passSkip!=0;opens_+=r.opens;openMs_+=r.openMs;
  if(r.interval>Refresh*1.5f)++s.late1;
  if(r.interval>Refresh*2.5f)++s.late2;
  for(unsigned i=0;i<StageCount;++i){s.ms[i]+=r.ms[i];s.msMax[i]=std::max(s.msMax[i],r.ms[i]);}
  if(r.rendered){++s.rendered;s.gpu+=r.gpu;s.gpuMax=std::max(s.gpuMax,r.gpu);s.draws+=r.draws;s.triangles+=r.triangles;s.culledTriangles+=r.culledTriangles;
   s.flushBytes+=r.metrics.flushBytes;s.skinned+=r.metrics.skinnedVertices;s.command=std::max(s.command,r.command);
   s.renderThread+=r.renderThread;s.renderWait+=r.renderWait;s.recordWait+=r.recordWait;s.worker+=r.metrics.workerMs;s.contact+=r.metrics.contactMs;s.shadowSupport+=r.metrics.shadowSupportMs;
   s.hook+=r.hook;s.hookMax=std::max(s.hookMax,r.hook);s.gpuGap+=r.gpuGap;s.gpuGapMax=std::max(s.gpuGapMax,r.gpuGap);
   s.drawSetup+=r.drawSetup;s.drawCall+=r.drawCall;s.drawReset+=r.drawReset;s.drawNow+=r.drawNow;s.drawSetups+=float(r.drawSetups);s.stateHits+=float(r.stateHits);
   // Least gap of the second (0 = unmeasured, e.g. unpipelined frames): what a frame ready in time still pays.
   if(r.gpuGap>0)s.gpuGapMin=s.gpuGapMin>0?std::min(s.gpuGapMin,r.gpuGap):r.gpuGap;}
  // Keep the slowest frames; replace the fastest kept one.
  if(spikes_<SpikeCount)spike_[spikes_++]=r;
  else{unsigned low=0;for(unsigned i=1;i<SpikeCount;++i)if(spike_[i].interval<spike_[low].interval)low=i;if(r.interval>spike_[low].interval)spike_[low]=r;}
  if(s.duration>=1000.f)closeSecond();
 }
 void closeSecond(){
  // One linear-heap walk per second, outside any stage mark.
  current_.linearFree=linearSpaceFree();
  linearMin_=std::min(linearMin_,current_.linearFree);
  if(seconds_<MaxSeconds)second_[seconds_++]=current_;
  current_={};
 }
 float percentile(float q)const{
  const unsigned want=unsigned(q*float(frames_));unsigned seen=0;
  for(unsigned i=0;i<Buckets;++i){seen+=histogram_[i];if(seen>want)return (float(i)+1.f)*.5f;}
  return intervalMax_;
 }
 void write(bool final){
  if(!frames_){reset();return;}
  if(current_.loops&&final)closeSecond();
  if(index_<0)for(int i=0;i<1000;++i){
   char probe[64];std::snprintf(probe,sizeof probe,"sdmc:/3ds/mkw-3ds/perf-race-%03d-seconds.csv",i);
   if(FILE* f=std::fopen(probe,"r")){std::fclose(f);continue;}
   index_=i;break;
  }
  if(index_<0){reset();return;}
  char path[64];
  std::snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/perf-race-%03d-seconds.csv",index_);
  if(FILE* f=std::fopen(path,"w")){
   std::fputs("second,loops,rendered,late_frames,very_late_frames,max_interval_ms,physics_ticks",f);
   for(auto* n:StageNames)std::fprintf(f,",%s_ms",n);
   for(auto* n:StageNames)std::fprintf(f,",%s_max_ms",n);
   std::fputs(",gpu_ms,gpu_max_ms,draws,triangles,culled_triangles,flush_kb,skinned_vertices,command_usage_max,render_thread_ms,render_begin_wait_ms,record_wait_ms,worker_ms,contact_ms,shadow_support_ms,linear_free_kb,file_opens,file_open_ms,leave_out_frames,hook_ms,hook_max_ms,gpu_gap_ms,gpu_gap_max_ms,gpu_gap_min_ms,draw_setup_ms,draw_call_ms,draw_reset_ms,draw_now_ms,draw_setups,state_hits\n",f);
   for(unsigned i=0;i<seconds_;++i){const auto& s=second_[i];const float n=float(std::max(1u,s.rendered));
    std::fprintf(f,"%u,%u,%u,%u,%u,%.3f,%u",i,s.loops,s.rendered,s.late1,s.late2,s.intervalMax,s.ticks);
    for(float v:s.ms)std::fprintf(f,",%.3f",v/n);
    for(float v:s.msMax)std::fprintf(f,",%.3f",v);
    std::fprintf(f,",%.3f,%.3f,%.1f,%.0f,%.0f,%.1f,%.0f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%.3f,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%.1f,%.1f\n",s.gpu/n,s.gpuMax,s.draws/n,s.triangles/n,s.culledTriangles/n,s.flushBytes/n/1024.f,s.skinned/n,
     s.command,s.renderThread/n,s.renderWait/n,s.recordWait/n,s.worker/n,s.contact/n,s.shadowSupport/n,s.linearFree/1024,s.opens,s.openMs,s.leaveOuts,s.hook/n,s.hookMax,s.gpuGap/n,s.gpuGapMax,s.gpuGapMin,
     s.drawSetup/n,s.drawCall/n,s.drawReset/n,s.drawNow/n,s.drawSetups/n,s.stateHits/n);
   }
   std::fclose(f);
  }
  std::snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/perf-race-%03d-spikes.csv",index_);
  if(FILE* f=std::fopen(path,"w")){
   std::fputs("race_time_s,frame,rendered,interval_ms,physics_ticks",f);
   for(auto* n:StageNames)std::fprintf(f,",%s_ms",n);
   std::fputs(",gpu_ms,draws,triangles,culled_triangles,culled_submeshes,flush_calls,flush_bytes,pose_evaluations,pose_reuses,skinned_vertices,command_usage,render_thread_ms,render_begin_wait_ms,record_wait_ms,worker_ms,contact_draws,contact_triangles,contact_ms,shadow_support_ms,animation_holds,scene_pose_samples,scene_pose_reuses,file_opens,file_open_ms,leave_out_pass,hook_ms,gpu_gap_ms\n",f);
   std::array<unsigned,SpikeCount> order{};for(unsigned i=0;i<spikes_;++i)order[i]=i;
   std::sort(order.begin(),order.begin()+spikes_,[&](unsigned a,unsigned b){return spike_[a].interval>spike_[b].interval;});
   for(unsigned k=0;k<spikes_;++k){const auto& r=spike_[order[k]];
    std::fprintf(f,"%.2f,%u,%u,%.3f,%u",r.time/1000.f,r.frame,r.rendered,r.interval,r.ticks);
    for(float v:r.ms)std::fprintf(f,",%.3f",v);
    std::fprintf(f,",%.3f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.4f,%.3f,%.3f,%.3f,%.3f,%u,%u,%.3f,%.3f,%u,%u,%u,%u,%.3f,%s,%.3f,%.3f\n",r.gpu,r.draws,r.triangles,r.culledTriangles,r.culledSubmeshes,
     r.metrics.flushCalls,r.metrics.flushBytes,r.metrics.poseEvaluations,r.metrics.poseReuses,r.metrics.skinnedVertices,r.command,r.renderThread,r.renderWait,r.recordWait,
     r.metrics.workerMs,r.metrics.contactDraws,r.metrics.contactTriangles,r.metrics.contactMs,r.metrics.shadowSupportMs,r.metrics.animationHolds,r.metrics.scenePoseSamples,r.metrics.scenePoseReuses,r.opens,r.openMs,diag::PassNames[r.passSkip<diag::PassCount?r.passSkip:0],r.hook,r.gpuGap);
   }
   std::fclose(f);
  }
  if(final){
   bool fresh=true;
   if(FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/perf-races.csv","r")){char head[1024]{};const bool v13=std::fgets(head,sizeof head,f)&&std::strstr(head,"vram_priority");std::fclose(f);
    // Rows without the vram_priority column move aside (v12: fast_vertex, v11: racer_lod, v10: plain_uv, v9: sky_last, v8: draw_distance, v7: player_vram, v6: shadow_rect, v5: bottom_cover, v4: bloom_merge (never filled in),
    // v3: pipelined, v2: earlier diagnostic builds).
    fresh=!v13;if(!v13)std::rename("sdmc:/3ds/mkw-3ds/perf-races.csv",std::strstr(head,"fast_vertex")?"sdmc:/3ds/mkw-3ds/perf-races-v12.csv":std::strstr(head,"racer_lod")?"sdmc:/3ds/mkw-3ds/perf-races-v11.csv":std::strstr(head,"plain_uv")?"sdmc:/3ds/mkw-3ds/perf-races-v10.csv":std::strstr(head,"sky_last")?"sdmc:/3ds/mkw-3ds/perf-races-v9.csv":std::strstr(head,"draw_distance")?"sdmc:/3ds/mkw-3ds/perf-races-v8.csv":std::strstr(head,"player_vram")?"sdmc:/3ds/mkw-3ds/perf-races-v7.csv":std::strstr(head,"shadow_rect")?"sdmc:/3ds/mkw-3ds/perf-races-v6.csv":std::strstr(head,"bottom_cover")?"sdmc:/3ds/mkw-3ds/perf-races-v5.csv":std::strstr(head,"bloom_merge")?"sdmc:/3ds/mkw-3ds/perf-races-v4.csv":std::strstr(head,"pipelined")?"sdmc:/3ds/mkw-3ds/perf-races-v3.csv":std::strstr(head,"stereo_pct")?"sdmc:/3ds/mkw-3ds/perf-races-v2.csv":"sdmc:/3ds/mkw-3ds/perf-races-v1.csv");}
   if(FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/perf-races.csv","a")){
    if(fresh){std::fputs("race,course,new3ds,reduced_quality,seconds,loops,rendered,avg_fps,late_frames,very_late_frames,p50_ms,p90_ms,p99_ms,max_ms",f);
     for(auto* n:StageNames)std::fprintf(f,",%s_ms",n);
     std::fputs(",gpu_ms,draws,triangles,min_linear_free_kb,stereo_pct,bloom,stadium,dynamic_shadows,shadow_volumes,pass_profiling,file_opens,file_open_ms,pipelined,bloom_merge,bottom_cover,shadow_rect,player_vram,draw_distance,spatial_course,hud_shrink,sky_last,plain_uv,racer_lod,fast_vertex,vram_priority\n",f);}
    std::array<double,StageCount> ms{};double gpu=0,draws=0,triangles=0;unsigned rendered=0,loops=0,late1=0,late2=0;
    for(unsigned i=0;i<seconds_;++i){const auto& s=second_[i];rendered+=s.rendered;loops+=s.loops;late1+=s.late1;late2+=s.late2;
     for(unsigned j=0;j<StageCount;++j)ms[j]+=s.ms[j];gpu+=s.gpu;draws+=s.draws;triangles+=s.triangles;}
    const double n=std::max(1u,rendered);bool n3ds=mkw::isNew3ds();
    std::fprintf(f,"%d,%u,%u,%u,%.1f,%u,%u,%.2f,%u,%u,%.1f,%.1f,%.1f,%.2f",index_,course_,unsigned(n3ds),unsigned(renderQuality().reduced),elapsed_/1000.f,loops,rendered,
     elapsed_>0?rendered*1000.0/elapsed_:0.0,late1,late2,percentile(.5f),percentile(.9f),percentile(.99f),intervalMax_);
    for(double v:ms)std::fprintf(f,",%.3f",v/n);
    std::fprintf(f,",%.3f,%.1f,%.0f,%u,%.1f,%u,%u,%u,%u,%u,%u,%.1f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",gpu/n,draws/n,triangles/n,linearMin_==~0u?0u:linearMin_/1024,
     frames_?100.0*stereoFrames_/frames_:0.0,unsigned(bloom_),unsigned(stadium_),unsigned(dynamicShadows_),unsigned(renderQuality().shadowVolumes),unsigned(diag::passState.enabled),opens_,openMs_,unsigned(pipelined_),unsigned(bloomMerge_),unsigned(RaceBottomScreen::skipCoveredFill),unsigned(shadowRect_),unsigned(playerVram_),unsigned(drawDistance_),unsigned(spatialCourse_),unsigned(hudShrink_),unsigned(skyLast_),unsigned(plainUv_),unsigned(renderQuality().racerLod),unsigned(fastVertex_),unsigned(vramPriority_));
    std::fclose(f);
   }
   diag::passWrite(index_,course_);diag::passReset();diag::skinWrite(index_);diag::skinReset();diag::drawWrite(index_);diag::drawReset();diag::writesWrite(index_);diag::writesReset();diag::textureUseWrite(index_);diag::textureUseReset();
   reset();
  }
 }
 void reset(){frames_=0;seconds_=0;spikes_=0;elapsed_=0;intervalMax_=0;linearMin_=~0u;current_={};histogram_={};index_=-1;stereoFrames_=0;opens_=0;openMs_=0;}
 bool active_=false,skip_=false;int index_=-1;
 unsigned frames_=0,seconds_=0,spikes_=0,startTicks_=0,course_=0,linearMin_=~0u;float elapsed_=0,intervalMax_=0;uint64_t stamp_=0;
 unsigned stereoFrames_=0,opens_=0,opensAt_=0;unsigned long long openTicksAt_=0;float openMs_=0;bool bloom_=false,stadium_=false,dynamicShadows_=false,pipelined_=false,bloomMerge_=false,shadowRect_=false,playerVram_=false,drawDistance_=false,spatialCourse_=false,hudShrink_=false,skyLast_=false,plainUv_=false,fastVertex_=false,vramPriority_=false;
 Row row_{};Second current_{};
 std::array<Second,MaxSeconds> second_{};std::array<Row,SpikeCount> spike_{};std::array<unsigned,Buckets> histogram_{};
};
#endif
#ifndef MKW_AUTO_PROFILE
class RuntimeProfile {
public:
 enum Stage {Simulation,UiPrepare,Wait,Course,Scenery,Opponents,Items,Player,Effects,Hud,Bottom,Submit,StageCount};
#if defined(MKW_RELEASE) && !defined(MKW_FRAME_CAPTURE)
 // Shipping CIA/3DSX never samples or writes diagnostic files, regardless
 // of stale SD marker files. No600-row buffer or per-tick clock calls.
 void start(){}
 void begin(unsigned,unsigned,unsigned,float){}
 void mark(Stage){}
 void end(unsigned,const Renderer&,bool=true){}
 void finish(){}
 void hold(){}
 void autoFrame(bool){}
 void context(bool,bool,bool,bool,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false){}
#else
 RuntimeProfile(){
#ifndef MKW_FRAME_CAPTURE
  if(FILE* marker=std::fopen("sdmc:/3ds/mkw-3ds/performance.txt","r")){std::fclose(marker);openFile();}
#endif
 }
 void start(){
#ifdef MKW_FRAME_CAPTURE
  // One pending capture at a time. No SD access while sampling.
  if(!count_&&!collecting_)collecting_=true;
#endif
 }
 ~RuntimeProfile(){finish();}
 // Captures save on pause (no measured frame writes to the card).
 void hold(){finish();}
 void autoFrame(bool){}
 void context(bool,bool,bool,bool,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false,bool=false){}
 void begin(unsigned course,unsigned frame,unsigned ticks,float interval){
  if(!on())return;
  row_={};row_.course=course;row_.frame=frame;startTicks_=ticks;row_.interval=interval;
  stamp_=svcGetSystemTick();
 }
 void mark(Stage stage){if(!on())return;auto now=svcGetSystemTick();row_.ms[stage]=float((now-stamp_)*1000.0/SYSCLOCK_ARM11);stamp_=now;}
 void end(unsigned ticks,const Renderer& renderer,bool rendered=true){
  if(!on())return;
  row_.ticks=ticks-startTicks_;row_.rendered=rendered;row_.animationStep=animationStep();row_.fireBytes=fireCacheBytes.load();row_.fireStride=fireCacheStride.load();
  if(rendered){row_.gpu=renderer.previousGpuMs;row_.draws=renderer.drawCalls;row_.triangles=renderer.triangles;
  row_.culledTriangles=renderer.culledTriangles;row_.culledSubmeshes=renderer.culledSubmeshes;
  row_.metrics=renderMetrics;
#ifndef MKW_FRAME_CAPTURE
  row_.linearFree=linearSpaceFree();
#endif
  row_.command=renderer.commandUsage;
  row_.renderThread=renderer.renderThreadMs;row_.renderWait=renderer.beginWaitMs;row_.recordWait=renderer.recordWaitMs;
  }
  rows_[count_++]=row_;if(count_==rows_.size()){
#ifdef MKW_FRAME_CAPTURE
   collecting_=false; // Save only on pause/exit, outside the measured race.
#else
   finish();
#endif
  }
 }
 void finish(){
#ifdef MKW_FRAME_CAPTURE
  collecting_=false;
  if(count_&&!file_)openFile();
#endif
  if(!file_)return;flush();std::fclose(file_);file_=nullptr;
 }
private:
 struct Row {unsigned animationStep=1,fireBytes=0,fireStride=0;unsigned course=0,frame=0,ticks=0,rendered=0;float interval=0;std::array<float,StageCount> ms{};float gpu=0;unsigned draws=0,triangles=0,culledTriangles=0,culledSubmeshes=0;RenderMetrics metrics{};unsigned linearFree=0;float command=0,renderThread=0,renderWait=0,recordWait=0;};
 void openFile(){
  // Diagnostic overlay: keep every capture (performance-000.csv, -001, ...).
  char path[64];
  for(unsigned i=0;i<1000;++i){
   std::snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/performance-%03u.csv",i);
   if(FILE* existing=std::fopen(path,"r")){std::fclose(existing);continue;}
   file_=std::fopen(path,"w");break;
  }
  if(file_)std::fputs("course,frame,rendered,physics_ticks,interval_ms,simulation_ms,ui_prepare_ms,wait_ms,course_ms,scenery_ms,opponents_ms,items_ms,player_ms,effects_ms,hud_ms,bottom_ms,submit_ms,previous_gpu_queue_ms,draws,triangles,culled_triangles,culled_submeshes,flush_calls,flush_bytes,pose_evaluations,pose_reuses,skinned_vertices,linear_free,command_usage,render_thread_ms,render_begin_wait_ms,record_wait_ms,worker_ms,contact_draws,contact_triangles,contact_cpu_ms,shadow_support_ms,animation_step,animation_holds,scene_pose_samples,scene_pose_reuses,fire_cache_copies,fire_cache_bytes,fire_cache_stride\n",file_);
 }
 void flush(){
  if(!file_)return;
  for(unsigned i=0;i<count_;++i){const auto& r=rows_[i];std::fprintf(file_,"%u,%u,%u,%u,%.4f",r.course,r.frame,r.rendered,r.ticks,r.interval);for(auto ms:r.ms)std::fprintf(file_,",%.4f",ms);
   std::fprintf(file_,",%.4f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.5f,%.4f,%.4f,%.4f,%.4f,%u,%u,%.4f,%.4f",r.gpu,r.draws,r.triangles,r.culledTriangles,r.culledSubmeshes,r.metrics.flushCalls,r.metrics.flushBytes,r.metrics.poseEvaluations,r.metrics.poseReuses,r.metrics.skinnedVertices,r.linearFree,r.command,r.renderThread,r.renderWait,r.recordWait,r.metrics.workerMs,r.metrics.contactDraws,r.metrics.contactTriangles,r.metrics.contactMs,r.metrics.shadowSupportMs);
   std::fprintf(file_,",%u,%u,%u,%u,%u,%u,%u\n",r.animationStep,r.metrics.animationHolds,r.metrics.scenePoseSamples,r.metrics.scenePoseReuses,r.metrics.fireReads,r.fireBytes,r.fireStride);
  }
  std::fflush(file_);count_=0;
 }
 // Explicit SD marker only, including release builds. Stop after one bounded
 // sample; disk writes occur only on completion/pause, not every120 frames.
 bool on()const{
#ifdef MKW_FRAME_CAPTURE
  return collecting_;
#else
  return file_!=nullptr;
#endif
 }
#ifdef MKW_FRAME_CAPTURE
 bool collecting_=false;
#endif
 FILE* file_=nullptr;std::array<Row,600> rows_{};Row row_{};unsigned count_=0,startTicks_=0;uint64_t stamp_=0;
#endif
};
#endif
}
