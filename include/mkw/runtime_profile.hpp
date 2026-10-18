#pragma once
#include <mkw/renderer.hpp>
#include <mkw/render_metrics.hpp>
#include <array>
#include <cstdio>
namespace mkw {
// Optional physical-device/emulator evidence. Marker is on writable SD even
// for ROMFS builds and release CIAs; package allowlists never install it. Batched I/O keeps
// diagnostics out of every frame's hot path. Keep this object off the ARM stack.
class RuntimeProfile {
public:
 enum Stage {Simulation,UiPrepare,Wait,Course,Scenery,Opponents,Items,Player,Effects,Hud,Bottom,Submit,StageCount};
 RuntimeProfile(){
  if(FILE* marker=std::fopen("sdmc:/3ds/mkw-3ds/performance.txt","r")){
   std::fclose(marker);file_=std::fopen("sdmc:/3ds/mkw-3ds/performance.csv","w");
   if(file_)std::fputs("course,frame,rendered,physics_ticks,interval_ms,simulation_ms,ui_prepare_ms,wait_ms,course_ms,scenery_ms,opponents_ms,items_ms,player_ms,effects_ms,hud_ms,bottom_ms,submit_ms,previous_gpu_queue_ms,draws,triangles,culled_triangles,culled_submeshes,flush_calls,flush_bytes,pose_evaluations,pose_reuses,skinned_vertices,linear_free,command_usage,render_thread_ms,render_begin_wait_ms,record_wait_ms,worker_ms\n",file_);
  }
 }
 ~RuntimeProfile(){flush();if(file_)std::fclose(file_);}
 void begin(unsigned course,unsigned frame,unsigned ticks,float interval){
  if(!on())return;
  row_={};row_.course=course;row_.frame=frame;startTicks_=ticks;row_.interval=interval;
  stamp_=svcGetSystemTick();
 }
 void mark(Stage stage){if(!on())return;auto now=svcGetSystemTick();row_.ms[stage]=float((now-stamp_)*1000.0/SYSCLOCK_ARM11);stamp_=now;}
 void end(unsigned ticks,const Renderer& renderer,bool rendered=true){
  if(!on())return;
  row_.ticks=ticks-startTicks_;row_.rendered=rendered;
  if(rendered){row_.gpu=renderer.previousGpuMs;row_.draws=renderer.drawCalls;row_.triangles=renderer.triangles;
  row_.culledTriangles=renderer.culledTriangles;row_.culledSubmeshes=renderer.culledSubmeshes;
  row_.metrics=renderMetrics;row_.linearFree=linearSpaceFree();row_.command=renderer.commandUsage;
  row_.renderThread=renderer.renderThreadMs;row_.renderWait=renderer.beginWaitMs;row_.recordWait=renderer.recordWaitMs;
  }
  rows_[count_++]=row_;if(count_==rows_.size())flush();
 }
private:
 struct Row {unsigned course=0,frame=0,ticks=0,rendered=0;float interval=0;std::array<float,StageCount> ms{};float gpu=0;unsigned draws=0,triangles=0,culledTriangles=0,culledSubmeshes=0;RenderMetrics metrics{};unsigned linearFree=0;float command=0,renderThread=0,renderWait=0,recordWait=0;};
 void flush(){
  if(!on())return;
  for(unsigned i=0;i<count_;++i){const auto& r=rows_[i];std::fprintf(file_,"%u,%u,%u,%u,%.4f",r.course,r.frame,r.rendered,r.ticks,r.interval);for(auto ms:r.ms)std::fprintf(file_,",%.4f",ms);
   std::fprintf(file_,",%.4f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.5f,%.4f,%.4f,%.4f,%.4f\n",r.gpu,r.draws,r.triangles,r.culledTriangles,r.culledSubmeshes,r.metrics.flushCalls,r.metrics.flushBytes,r.metrics.poseEvaluations,r.metrics.poseReuses,r.metrics.skinnedVertices,r.linearFree,r.command,r.renderThread,r.renderWait,r.recordWait,r.metrics.workerMs);
  }
  std::fflush(file_);count_=0;
 }
 // Profiling is on only when performance.txt exists on the SD card, release builds
 // included: without it file_ stays null and every call returns immediately.
 bool on()const{return file_!=nullptr;}
 FILE* file_=nullptr;std::array<Row,120> rows_{};Row row_{};unsigned count_=0,startTicks_=0;uint64_t stamp_=0;
};
}
