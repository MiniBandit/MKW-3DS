#pragma once
#include <mkw/renderer.hpp>
#include <mkw/render_metrics.hpp>
#include <array>
#include <mkw/animation_budget.hpp>
#include <cstdio>
namespace mkw {
// Optional physical-device/emulator evidence. Marker is on writable SD even
// for ROMFS builds; package allowlists never install it. Batched I/O keeps
// diagnostics out of every frame's hot path. Keep this object off the ARM stack.
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
  file_=std::fopen("sdmc:/3ds/mkw-3ds/performance.csv","w");
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
}
