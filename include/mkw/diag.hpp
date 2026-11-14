#pragma once
// Diagnostic build only (MKW_AUTO_PROFILE). Two measurement aids:
//  - SD log deferral: while a race is being profiled, append-mode opens of
//    sdmc:/3ds/MKWiiDS/ logs (runtime.log, item/effect/audio traces) go to RAM
//    and are written when profiling stops, so log I/O never lands inside a
//    measured frame (src/linear_lock.cpp wraps fopen).
//  - GPU pass leave-out: on one presented frame in PassPeriod the render
//    thread drops one pass's draw commands (rotating through PassCount-1
//    passes) and compares that frame's GPU time with its two neighbours.
//    The rows show what each pass costs on the hardware's GPU.
//  - Vertex probe: on another frame in PassPeriod the render thread draws one
//    model pass a second time with every vertex projected to one point (the
//    same vertex work, no pixels). Its added GPU time is the pass's vertex
//    cost; the leave-out cost minus it is mostly fill.
#ifdef __3DS__
#include <3ds.h>
#else
// Host tools (tools/menu_sprite_scales.cpp and the other offline menu checks)
// compile the menu sources too. They only need these to exist; nothing they
// run is timed or contended.
#include <chrono>
#define SYSCLOCK_ARM11 268111856ull
using LightLock = int;
inline void LightLock_Init(LightLock*) {}
inline void LightLock_Lock(LightLock*) {}
inline void LightLock_Unlock(LightLock*) {}
inline unsigned long long svcGetSystemTick() {
  return std::chrono::duration_cast<std::chrono::duration<unsigned long long, std::ratio<1, SYSCLOCK_ARM11>>>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
#endif
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mkw {
// DIAG: each fopen's path and time since the last take(), up to 48.
struct OpenTrace {
  struct Open {char path[72];unsigned long long ticks;bool found;};
  Open opens[48];unsigned count=0,dropped=0;LightLock lock;
  OpenTrace(){LightLock_Init(&lock);}
  void add(const char* path,unsigned long long ticks,bool found){
    LightLock_Lock(&lock);
    if(count<48){auto& o=opens[count++];std::snprintf(o.path,sizeof o.path,"%s",path);o.ticks=ticks;o.found=found;}else ++dropped;
    LightLock_Unlock(&lock);
  }
  template<class F> void take(F&& each){
    LightLock_Lock(&lock);const unsigned n=count,lost=dropped;Open copy[48];std::memcpy(copy,opens,sizeof(Open)*n);count=dropped=0;LightLock_Unlock(&lock);
    for(unsigned i=0;i<n;++i)each(copy[i]);if(lost){Open o{};o.found=true;std::snprintf(o.path,sizeof o.path,"(%u more)",lost);each(o);}
  }
};
extern OpenTrace openTrace;
}
namespace mkw::diag {
// Repeated file I/O skipped: Lakitu's joint files are read once a session,
// the packs stay open between races, and accepting the license that
// last-license.txt already names doesn't rewrite it. A/B: no-load-cache.txt.
inline bool loadCache=true;
// Draw tally: model draws also count the screen pixels their triangles
// cover (CPU clipping, both faces, overdraw included; GPU-skinned meshes
// count 0). Slow; for Azahar runs. On with model-pixels.txt.
inline bool modelPixels=false;
// Menu composition: exact shortcuts in uiTextMotion. A/B: no-menu-fast.txt;
// menu-check.txt composes every menu frame both ways and logs differences.
inline bool menuFast=true,menuCheck=false;
// Opponents load compact .dsku driver banks when race.pak has them. A/B:
// no-unlit-banks.txt.
inline bool unlitBanks=true;
// Released items, other racers' items and course objects (not crowds) take
// their G3D light in the vertex shader, from the camera's side, instead of
// the load-time bake. A/B: no-live-light.txt. Opponents are baked as seen
// from behind instead of from the front. A/B: no-rear-bake.txt.
inline bool liveLight=true,rearBake=true;
// Opponents lit in the vertex shader like the local racer (full banks, lit
// GPU skinning; LOD models stay baked). Off by default. A/B: live-opponents.txt.
inline bool liveOpponents=false;
// Opponent Detail Full: opponents' drivers swap to model_lod beyond 4,000 units
// of camera distance, as on the Wii (wiiLowDriver). A/B: no-wii-driver-lod.txt.
inline bool wiiDriverLod=true;
// Lighter 3D at 60 FPS (New 3DS, 3D slider up). A/B: no-stereo-60.txt (3D at
// 30 FPS again), no-stereo-opponent-lod.txt (opponents as in 2D).
inline bool stereoSixty=true,stereoOpponentLod=true;
// 3D: whole models are also tested against each eye's own frustum when drawn
// (record-time culling has to use both eyes' union). A/B: no-stereo-eye-cull.txt.
inline bool stereoEyeCull=true;
// 3D: course objects take -low LODs nearer and drop tiny parts sooner
// (RenderQuality::objectLow/tinyRatio). A/B: no-stereo-object-lod.txt.
inline bool stereoObjectLod=true;
// Lighter 3D: course-object and opponent shadow volumes under ~8 px on screen are
// skipped while 3D draws (RenderQuality::stereoShadowRatio). A/B: no-stereo-shadow-cull.txt.
inline bool stereoShadowCull=true;
// 3D: shadow volumes shade the rectangle their bounds cover in either eye, not
// the whole screen (Renderer::shadowVolumesNow). A/B: no-stereo-shadow-rects.txt.
inline bool stereoShadowRects=true;
// Light zones: racers take BLIGHT light and ambient k from the floor's KCL
// bits 8-10 (RacerLightZone). A/B: no-light-zones.txt.
inline bool lightZones=true;
// Pause keeps the racers and course object graphics loaded when the pause
// menu's textures still fit, so resuming needs no reload. A/B: no-pause-keep.txt.
inline bool pauseKeep=true;
// Race loop iterations: raw timestamps at each profiler stage (no formatting
// on the hot path). main.cpp reports the iteration in which the intro ends
// or the race starts (GO), and the three after it.
struct IterationMarks {unsigned long long start=0,ticks[16]{};const char* names[16]{};unsigned count=0;};
inline IterationMarks iteration,previousIteration;
inline void iterationMark(const char* name){auto& it=iteration;if(it.count<16){it.ticks[it.count]=svcGetSystemTick();it.names[it.count++]=name;}}
inline unsigned long long iterationBegin(){const auto now=svcGetSystemTick();previousIteration=iteration;iteration=IterationMarks{};iteration.start=now;return now;}
// "label total ms: stage ms ... tail ms" for previousIteration, ending at end.
inline void describeIteration(char* text,size_t size,const char* label,unsigned long long end){
 const auto& it=previousIteration;auto ms=[](unsigned long long t){return double(t)*1000.0/SYSCLOCK_ARM11;};
 int used=std::snprintf(text,size,"%s %.1f ms:",label,ms(end-it.start));auto last=it.start;
 for(unsigned i=0;i<it.count&&used>0&&size_t(used)<size;++i){used+=std::snprintf(text+used,size-used," %s %.1f",it.names[i],ms(it.ticks[i]-last));last=it.ticks[i];}
 if(used>0&&size_t(used)<size)std::snprintf(text+used,size-used," tail %.1f",ms(end-last));
}
// Menu composition timing (src/menu_ui.cpp): exclusive time per
// Composer operation, summed per frame; main.cpp logs it per menu page.
inline constexpr const char* ComposeNames[]={"control","group","groupPane","quad","region","focusLayers","panel","inPane","textSprite","text","mainText","licensePortrait","licenseName","icon","licenseTrophies","movie","glowGroup","selectionGlow","chrome","compose","textMotion","focusAnimation"};
inline unsigned long long composeTicks[std::size(ComposeNames)]{};inline unsigned composeCalls[std::size(ComposeNames)]{};
// Menu Mii publish on the main thread: NativeMiiHead::load stamps its start,
// after CFL_Initialize and before CFL_Finalize; MenuModels' publish adds the
// GPU wait and the total. main.cpp turns each new one into a profiler event.
inline unsigned long long miiLoadStamps[3]{};
struct MiiPublish {int id=-1;unsigned long long wait=0,init=0,build=0,finalize=0,total=0;};
inline MiiPublish miiPublish{};inline unsigned miiPublishCount=0;
// The chase camera skips its soft-wall pass on courses with no soft weak wall
// (exact: cmd-hash.txt equal either way). A/B: no-soft-wall-skip.txt.
inline bool softWallSkip=true;
struct ComposeTimer {
 static inline unsigned depth=0;static inline unsigned long long child[64]{};
 unsigned category;unsigned long long start;
 explicit ComposeTimer(unsigned c):category(c),start(svcGetSystemTick()){if(depth<63)child[++depth]=0;}
 ~ComposeTimer(){const auto total=svcGetSystemTick()-start;const auto own=total-child[depth];if(depth)--depth;child[depth]+=total;composeTicks[category]+=own;++composeCalls[category];}
};

void deferSdLogs(bool on);
unsigned deferredLogBytes();
extern std::atomic<unsigned> deferredOpens;

enum Pass : uint8_t {PassNone,PassSky,PassCourse,PassScenery,PassOpponents,PassItems,PassPlayer,PassTranslucent,
                     PassPlayerShadow,PassShadowVolumes,PassEffects,PassBloom,PassHud,PassFlare,PassBottom,PassBottomQuads,PassCount};
// bottom_quads is a leave-out only: the lower screen's target bind and
// display transfer stay, its draws go (fixed cost = bottom - bottom_quads).
inline constexpr const char* PassNames[PassCount]={"none","sky","course","scenery","opponents","items","player","translucent",
                                                   "player_shadow","shadow_volumes","effects","bloom","hud","lens_flare","bottom","bottom_quads"};
inline constexpr unsigned PassPeriod=8;
// Passes the vertex probe rotates through (those drawn as models).
inline constexpr uint8_t ProbePasses[]={PassSky,PassCourse,PassScenery,PassOpponents,PassItems,PassPlayer,PassTranslucent};
// passSample tags: a leave-out frame's pass, or a probed pass | ProbeTag.
inline constexpr uint8_t ProbeTag=0x80;

struct PassStats {double delta=0,deltaSq=0,neighbours=0;unsigned samples=0;};
struct PassState {
    LightLock lock;
    bool enabled=false;     // marker gpu-passes.txt or menu autoplay
    bool noBottom=false;    // no-bottom-passes.txt: the rotation leaves out bottom and bottom_quads
    bool active=false;      // main thread: this frame is a profiled racing frame
    uint8_t skip=PassNone;  // main thread: the frame being recorded drops this pass
    uint8_t probe=PassNone; // main thread: the frame being recorded draws this pass twice (vertex probe)
    unsigned frame=0,rotation=0,probeRotation=0;
    // Render thread: paired-difference state.
    float lastNormal=-1,pendingGpu=0,pendingNeighbour=-1;uint8_t pendingTag=PassNone;
    PassStats stats[PassCount]{},probeStats[PassCount]{};double normalGpu=0;unsigned normalFrames=0;
    // Render thread: draws and triangles per pass on profiled frames that
    // leave nothing out (left eye), summed over countedFrames.
    double draws[PassCount]{},triangles[PassCount]{};unsigned countedFrames=0;
    PassState(){LightLock_Init(&lock);}
};
inline PassState passState;

// Frame-split counters (recording thread only; the heartbeat logs them).
// midFrameDrains: drain() calls between begin() and end(), which split a
// recorded frame before the fix; lastDrainCaller is the latest one's caller.
// effectRebuilds counts effect-material rebuilds, effectRebuildsRecording
// those made while a frame was being recorded.
struct SplitStats {unsigned midFrameDrains=0,effectRebuilds=0,effectRebuildsRecording=0;uintptr_t lastDrainCaller=0;};
inline SplitStats splitStats;
// Shadow-volume shade quads by kind (render thread writes, heartbeat reads):
// bounded to a rectangle, whole screen, or skipped (nothing on screen). area
// sums the bounded rectangles' pixels, to compare with whole-screen quads.
struct ShadowRectStats {unsigned bounded=0,full=0,empty=0;uint64_t area=0;};
inline ShadowRectStats shadowRectStats;

// Draw calls per (pass, model) on profiled frames that leave nothing out
// (render thread, left eye), to see which models make a pass's draws. Course
// objects name their models by resource (recording thread, nameModel).
struct DrawEntry {const void* model=nullptr;uint8_t pass=0;uint32_t records=0,draws=0,triangles=0;uint64_t pixels=0;};
struct DrawTally {LightLock lock;DrawEntry entries[256];unsigned count=0,frames=0;DrawTally(){LightLock_Init(&lock);}};
inline DrawTally drawTally;
// The same tally for countdown frames only, written as perf-race-NNN-grid-draws.csv.
// The main thread sets drawGrid; each recorded frame carries it to the render thread.
inline DrawTally gridTally;
inline std::atomic<bool> drawGrid{false};
struct ModelName {const void* model;std::string name;};
inline std::vector<ModelName> modelNames;
inline void nameModel([[maybe_unused]] const void* model,[[maybe_unused]] const std::string& name){
#ifdef MKW_RELEASE
    return;
#endif
    for(auto& n:modelNames)if(n.model==model){if(n.name!=name)n.name=name;return;}
    modelNames.push_back({model,name});
}
inline void tallyDraw(DrawTally& t,const void* model,uint8_t pass,uint32_t draws,uint32_t triangles,uint64_t pixels){
    LightLock_Lock(&t.lock);
    unsigned i=0;while(i<t.count&&(t.entries[i].model!=model||t.entries[i].pass!=pass))++i;
    if(i==t.count&&t.count<256)t.entries[t.count++]={model,pass};
    if(i<t.count){auto& e=t.entries[i];++e.records;e.draws+=draws;e.triangles+=triangles;e.pixels+=pixels;}
    LightLock_Unlock(&t.lock);
}
inline void drawCount(const void* model,uint8_t pass,uint32_t draws,uint32_t triangles,uint64_t pixels){
    tallyDraw(drawTally,model,pass,draws,triangles,pixels);
}
// Every frame, not just profiled ones: the countdown is only ~240 frames.
inline void gridCount(const void* model,uint8_t pass,uint32_t draws,uint32_t triangles,uint64_t pixels){
    tallyDraw(gridTally,model,pass,draws,triangles,pixels);
}
inline void drawFrame(){auto& t=drawTally;LightLock_Lock(&t.lock);++t.frames;LightLock_Unlock(&t.lock);}
inline void gridFrame(){auto& t=gridTally;LightLock_Lock(&t.lock);++t.frames;LightLock_Unlock(&t.lock);}
inline void drawReset(){
    for(auto* t:{&drawTally,&gridTally}){LightLock_Lock(&t->lock);t->count=0;t->frames=0;LightLock_Unlock(&t->lock);}
    modelNames.clear();
}
// perf-race-NNN-draws.csv: per frame, the models drawn in each pass, most draws first.
inline void drawWriteTally(DrawTally& t,int race,const char* suffix){
    std::vector<DrawEntry> rows;unsigned frames;
    LightLock_Lock(&t.lock);rows.assign(t.entries,t.entries+t.count);frames=t.frames;LightLock_Unlock(&t.lock);
    if(!frames||race<0)return;
    std::sort(rows.begin(),rows.end(),[](const DrawEntry& a,const DrawEntry& b){return a.pass!=b.pass?a.pass<b.pass:a.draws>b.draws;});
    char path[72];std::snprintf(path,sizeof path,"sdmc:/3ds/MKWiiDS/perf-race-%03d-%sdraws.csv",race,suffix);
    FILE* f=std::fopen(path,"w");if(!f)return;
    std::fprintf(f,"pass,model,records_per_frame,draws_per_frame,triangles_per_frame,frames,%s\n",modelPixels?"pixels_per_frame":"quad_pixels_per_frame");
    for(const auto& e:rows){
        const char* name=nullptr;for(const auto& n:modelNames)if(n.model==e.model){name=n.name.c_str();break;}
        if(!e.model)name="(non-model)";
        char fallback[24];if(!name){std::snprintf(fallback,sizeof fallback,"model@%08lx",(unsigned long)uintptr_t(e.model));name=fallback;}
        std::fprintf(f,"%s,%s,%.2f,%.2f,%.0f,%u,%.0f\n",e.pass<PassCount?PassNames[e.pass]:"?",name,double(e.records)/frames,double(e.draws)/frames,double(e.triangles)/frames,frames,double(e.pixels)/frames);
    }
    std::fclose(f);
}
inline void drawWrite(int race){drawWriteTally(drawTally,race,"");drawWriteTally(gridTally,race,"grid-");}

// DIAG texture-use.txt: model draws per bound texture (any unit, keyed by its
// data), and their screen pixels with model-pixels.txt, every race frame
// (render thread). The race load registers the course, sky and object
// textures (main thread); textureUseWrite() writes perf-race-NNN-textures.csv,
// one row per registered texture, where it is (vram/linear) and its use.
inline bool textureUse=false;
struct TextureInfo {const void* data=nullptr;std::string owner;int index=-1;unsigned width=0,height=0,fmt=0,levels=0;uint32_t bytes=0;bool unitOne=false;};
struct TextureUseEntry {const void* data=nullptr;uint32_t draws=0,units=0;uint64_t pixels=0;};
struct TextureUseTally {LightLock lock;TextureUseEntry entries[1024];unsigned frames=0;std::vector<TextureInfo> info;TextureUseTally(){LightLock_Init(&lock);}};
inline TextureUseTally textureUseTally;
inline void textureDrawn(const void* const* data,unsigned count,uint32_t draws,uint64_t pixels){
    auto& t=textureUseTally;LightLock_Lock(&t.lock);
    for(unsigned k=0;k<count;++k){
        if(!data[k])continue;
        bool repeat=false;for(unsigned j=0;j<k;++j)repeat|=data[j]==data[k];
        if(repeat)continue;
        unsigned i=unsigned((uintptr_t(data[k])>>4)*2654435761u)&1023u;
        for(unsigned probe=0;probe<1024&&t.entries[i].data&&t.entries[i].data!=data[k];++probe)i=(i+1)&1023u;
        auto& e=t.entries[i];if(e.data&&e.data!=data[k])continue;
        e.data=data[k];e.draws+=draws;e.pixels+=pixels;e.units|=1u<<k;
    }
    LightLock_Unlock(&t.lock);
}
inline void textureFrame(){auto& t=textureUseTally;LightLock_Lock(&t.lock);++t.frames;LightLock_Unlock(&t.lock);}
inline void textureUseReset(){auto& t=textureUseTally;LightLock_Lock(&t.lock);for(auto& e:t.entries)e={};t.frames=0;LightLock_Unlock(&t.lock);}
inline void textureRegister(std::vector<TextureInfo> info){auto& t=textureUseTally;LightLock_Lock(&t.lock);t.info=std::move(info);LightLock_Unlock(&t.lock);}
// Adds the textures not registered yet (racers, items, HUD, effects, Lakitu...).
inline void textureRegisterMore(std::vector<TextureInfo> more){auto& t=textureUseTally;LightLock_Lock(&t.lock);
    for(auto& i:more){bool known=false;for(const auto& k:t.info)known|=k.data==i.data;if(!known)t.info.push_back(std::move(i));}
    LightLock_Unlock(&t.lock);}
inline void textureUseWrite(int race){
    if(!textureUse||race<0)return;
    auto& t=textureUseTally;std::vector<TextureInfo> info;std::vector<TextureUseEntry> used;unsigned frames;
    LightLock_Lock(&t.lock);info=t.info;frames=t.frames;for(const auto& e:t.entries)if(e.data)used.push_back(e);LightLock_Unlock(&t.lock);
    if(!frames)return;
    char path[72];std::snprintf(path,sizeof path,"sdmc:/3ds/MKWiiDS/perf-race-%03d-textures.csv",race);
    FILE* f=std::fopen(path,"w");if(!f)return;
    std::fprintf(f,"owner,index,where,width,height,fmt,levels,bytes,unit1,draws_per_frame,%s,units,frames\n",modelPixels?"pixels_per_frame":"pixels_off");
    for(const auto& i:info){
        const TextureUseEntry* e=nullptr;for(const auto& u:used)if(u.data==i.data){e=&u;break;}
        const uintptr_t at=uintptr_t(i.data);
        std::fprintf(f,"%s,%d,%s,%u,%u,%u,%u,%lu,%d,%.3f,%.0f,%u,%u\n",i.owner.c_str(),i.index,at>=0x1F000000u&&at<0x1F600000u?"vram":"linear",
            i.width,i.height,i.fmt,i.levels,(unsigned long)i.bytes,int(i.unitOne),e?double(e->draws)/frames:0.0,e?double(e->pixels)/frames:0.0,e?e->units:0u,frames);
    }
    std::fclose(f);
}

// Deferred writes run by the pipelined frame hook (previousFrameDone), per
// write kind (render thread): the GPU waits for them between frames.
// writesWrite() writes the race's perf-race-NNN-hook-writes.csv.
struct WriteEntry {const char* kind=nullptr;const void* run=nullptr;uint64_t ticks=0,maxFrameTicks=0;uint32_t count=0,frameCount=0,maxFrameCount=0;uint64_t frameTicks=0;};
struct WriteTally {LightLock lock;WriteEntry entries[16];unsigned count=0,frames=0;uint64_t ticks=0,maxFrameTicks=0;WriteTally(){LightLock_Init(&lock);}};
inline WriteTally writeTally;
// The same for prepare()d writes and upload() staging copies (frame start, off the hook).
inline WriteTally prepareTally;
// One frame's writes, already grouped by kind (frame[i].frameTicks/frameCount).
inline void writesFrame(const WriteEntry* frame,unsigned n,uint64_t total,WriteTally& t=writeTally){
#ifdef MKW_RELEASE
    return;
#endif
    LightLock_Lock(&t.lock);
    for(unsigned k=0;k<n;++k){
        const auto& f=frame[k];unsigned i=0;
        while(i<t.count&&(t.entries[i].run!=f.run||t.entries[i].kind!=f.kind))++i;
        if(i==t.count){if(t.count==16)continue;t.entries[t.count++]={f.kind,f.run};}
        auto& e=t.entries[i];e.ticks+=f.frameTicks;e.count+=f.frameCount;
        e.maxFrameTicks=std::max(e.maxFrameTicks,f.frameTicks);e.maxFrameCount=std::max(e.maxFrameCount,f.frameCount);
    }
    ++t.frames;t.ticks+=total;t.maxFrameTicks=std::max(t.maxFrameTicks,total);
    LightLock_Unlock(&t.lock);
}
inline void writesReset(){for(auto* t:{&writeTally,&prepareTally}){LightLock_Lock(&t->lock);t->count=0;t->frames=0;t->ticks=t->maxFrameTicks=0;LightLock_Unlock(&t->lock);}}
// Hook rows first (total = the hook's whole time); then "prepare: <kind>" rows
// and "prepare total", per frame of the hook's frame count (frames that
// prepared nothing count as zero).
inline void writesWrite(int race){
    auto& t=writeTally;std::vector<WriteEntry> rows,prepared;unsigned frames;uint64_t ticks,maxTicks,prepareTicks,prepareMax;
    LightLock_Lock(&t.lock);rows.assign(t.entries,t.entries+t.count);frames=t.frames;ticks=t.ticks;maxTicks=t.maxFrameTicks;LightLock_Unlock(&t.lock);
    auto& p=prepareTally;
    LightLock_Lock(&p.lock);prepared.assign(p.entries,p.entries+p.count);prepareTicks=p.ticks;prepareMax=p.maxFrameTicks;LightLock_Unlock(&p.lock);
    if(!frames||race<0)return;
    auto byTime=[](const WriteEntry& a,const WriteEntry& b){return a.ticks>b.ticks;};
    std::sort(rows.begin(),rows.end(),byTime);std::sort(prepared.begin(),prepared.end(),byTime);
    char path[72];std::snprintf(path,sizeof path,"sdmc:/3ds/MKWiiDS/perf-race-%03d-hook-writes.csv",race);
    FILE* f=std::fopen(path,"w");if(!f)return;
    const double ms=1000.0/SYSCLOCK_ARM11;
    std::fprintf(f,"kind,run,writes_per_frame,ms_per_frame,max_ms_frame,max_writes_frame,frames\n");
    std::fprintf(f,"total,,,%.3f,%.3f,,%u\n",double(ticks)*ms/frames,double(maxTicks)*ms,frames);
    for(const auto& e:rows)std::fprintf(f,"%s,%08lx,%.2f,%.3f,%.3f,%u,%u\n",e.kind?e.kind:"unnamed",(unsigned long)uintptr_t(e.run),
        double(e.count)/frames,double(e.ticks)*ms/frames,double(e.maxFrameTicks)*ms,e.maxFrameCount,frames);
    if(!prepared.empty())std::fprintf(f,"prepare total,,,%.3f,%.3f,,%u\n",double(prepareTicks)*ms/frames,double(prepareMax)*ms,frames);
    for(const auto& e:prepared)std::fprintf(f,"prepare: %s,%08lx,%.2f,%.3f,%.3f,%u,%u\n",e.kind?e.kind:"unnamed",(unsigned long)uintptr_t(e.run),
        double(e.count)/frames,double(e.ticks)*ms/frames,double(e.maxFrameTicks)*ms,e.maxFrameCount,frames);
    std::fclose(f);
}

// Skinned vertices per course-object resource (recording thread), for finding
// what skins on a course. gpu: skinned in the vertex shader each draw; else a
// fresh CPU skin (reused poses aren't counted, as in renderMetrics).
// skinWrite() writes the race's perf-race-NNN-skin.csv; skinReset() starts the next.
// distance: the instance's distance from the camera. Retail registers each
// object with a draw distance of ObjFlow +0x68 x 10 (40000 for Daisy's
// crowds, USA 0x8080d648); far20k/far40k count vertices beyond 20000/40000.
struct SkinEntry {std::string name;uint64_t vertices=0,far20k=0,far40k=0;unsigned draws=0,frameVertices=0,peak=0;float maxDistance=0;bool gpu=false;};
struct SkinTally {SkinEntry entries[48];unsigned count=0,frames=0,frameTotal=0,peakTotal=0,peakFrame=0;};
inline SkinTally skinTally;
inline void tallySkin([[maybe_unused]] const std::string& name,[[maybe_unused]] unsigned vertices,[[maybe_unused]] bool gpu,[[maybe_unused]] float distance){
#ifdef MKW_RELEASE
    return;
#endif
    auto& t=skinTally;
    unsigned i=0;while(i<t.count&&(t.entries[i].gpu!=gpu||t.entries[i].name!=name))++i;
    if(i==t.count){if(t.count==48)return;t.entries[t.count].name=name;t.entries[t.count].gpu=gpu;++t.count;}
    auto& e=t.entries[i];e.vertices+=vertices;++e.draws;e.frameVertices+=vertices;t.frameTotal+=vertices;
    if(distance>20000)e.far20k+=vertices;
    if(distance>40000)e.far40k+=vertices;
    e.maxDistance=std::max(e.maxDistance,distance);
}
// Course objects hidden past retail's draw distance, per resource (the
// skin CSV's "hidden" rows: draws_per_frame is hidden instances per frame).
struct HiddenEntry {std::string name;uint64_t count=0;};
inline HiddenEntry hiddenTally[48];inline unsigned hiddenCount=0;
inline void tallyHidden([[maybe_unused]] const char* name){
#ifdef MKW_RELEASE
    return;
#endif
    unsigned i=0;while(i<hiddenCount&&hiddenTally[i].name!=name)++i;
    if(i==hiddenCount){if(hiddenCount==48)return;hiddenTally[hiddenCount++].name=name;}
    ++hiddenTally[i].count;
}
inline void skinFrame(){
    auto& t=skinTally;++t.frames;
    if(t.frameTotal>t.peakTotal){t.peakTotal=t.frameTotal;t.peakFrame=t.frames;}
    for(unsigned i=0;i<t.count;++i){auto& e=t.entries[i];e.peak=std::max(e.peak,e.frameVertices);e.frameVertices=0;}
    t.frameTotal=0;
}
inline void skinReset(){skinTally=SkinTally{};for(auto& e:hiddenTally)e=HiddenEntry{};hiddenCount=0;}
// One row per resource: mean vertices per frame, the most in one frame, draws
// per frame. The "total" row's peak is the largest frame (at peak_frame).
inline void skinWrite(int race){
    const auto& t=skinTally;if(!t.frames||race<0)return;
    char path[64];std::snprintf(path,sizeof path,"sdmc:/3ds/MKWiiDS/perf-race-%03d-skin.csv",race);
    FILE* f=std::fopen(path,"w");if(!f)return;
    std::fprintf(f,"path,resource,mean_vertices,peak_vertices,draws_per_frame,frames,peak_frame,mean_beyond_20k,mean_beyond_40k,max_distance\n");
    std::fprintf(f,"all,total,,%u,,%u,%u,,,\n",t.peakTotal,t.frames,t.peakFrame);
    for(unsigned i=0;i<t.count;++i){const auto& e=t.entries[i];
        std::fprintf(f,"%s,%s,%.0f,%u,%.2f,%u,,%.0f,%.0f,%.0f\n",e.gpu?"gpu":"cpu",e.name.c_str(),double(e.vertices)/t.frames,e.peak,double(e.draws)/t.frames,t.frames,
                     double(e.far20k)/t.frames,double(e.far40k)/t.frames,double(e.maxDistance));}
    for(unsigned i=0;i<hiddenCount;++i)std::fprintf(f,"hidden,%s,,,%.2f,%u,,,,\n",hiddenTally[i].name.c_str(),double(hiddenTally[i].count)/t.frames,t.frames);
    std::fclose(f);
}

// Main thread, once per presented race frame before Renderer::begin.
inline void passFrame(bool racing){
    if(racing)skinFrame();
    auto& s=passState;
    s.active=racing&&s.enabled;s.skip=PassNone;s.probe=PassNone;
    if(!s.active)return;
    ++s.frame;
    if(s.frame%PassPeriod==PassPeriod/2)s.skip=uint8_t(PassSky+s.rotation++%(s.noBottom?PassBottom-PassSky:PassCount-1));
    else if(s.frame%PassPeriod==0)s.probe=ProbePasses[s.probeRotation++%std::size(ProbePasses)];
}
inline bool passMarkers(){return passState.active;}
// Render thread, at each frame's begin: gpu is the previous frame's GPU time,
// tag/active what that frame was recorded with.
inline void passSample(uint8_t tag,bool active,float gpu){
    auto& s=passState;
    LightLock_Lock(&s.lock);
    if(!active){s.lastNormal=-1;s.pendingTag=PassNone;}
    else if(tag==PassNone){
        if(s.pendingTag&&s.pendingNeighbour>=0){
            // A leave-out frame saves time; a probe frame adds it.
            const bool probe=s.pendingTag&ProbeTag;
            const double neighbours=.5*(s.pendingNeighbour+gpu),d=probe?s.pendingGpu-neighbours:neighbours-s.pendingGpu;
            auto& p=probe?s.probeStats[s.pendingTag&~ProbeTag]:s.stats[s.pendingTag];p.delta+=d;p.deltaSq+=d*d;p.neighbours+=neighbours;++p.samples;
        }
        s.pendingTag=PassNone;s.lastNormal=gpu;s.normalGpu+=gpu;++s.normalFrames;
    }else{s.pendingTag=tag;s.pendingGpu=gpu;s.pendingNeighbour=s.lastNormal;}
    LightLock_Unlock(&s.lock);
}
// Render thread, once per counted frame.
inline void passCount(const uint32_t (&draws)[PassCount],const uint32_t (&triangles)[PassCount]){
    auto& s=passState;
    LightLock_Lock(&s.lock);
    for(unsigned i=0;i<PassCount;++i){s.draws[i]+=draws[i];s.triangles[i]+=triangles[i];}
    ++s.countedFrames;
    LightLock_Unlock(&s.lock);
}
inline void passReset(){
    auto& s=passState;
    LightLock_Lock(&s.lock);
    for(auto& p:s.stats)p={};for(auto& p:s.probeStats)p={};s.normalGpu=0;s.normalFrames=0;s.lastNormal=-1;s.pendingTag=PassNone;
    for(unsigned i=0;i<PassCount;++i)s.draws[i]=s.triangles[i]=0;s.countedFrames=0;
    LightLock_Unlock(&s.lock);
}
// Writes one race's table; returns false when nothing was sampled.
inline bool passWrite(int race,unsigned course){
    auto& s=passState;
    PassStats stats[PassCount],probes[PassCount];double normalGpu,draws[PassCount],triangles[PassCount];unsigned normalFrames,counted;
    LightLock_Lock(&s.lock);
    for(unsigned i=0;i<PassCount;++i){stats[i]=s.stats[i];probes[i]=s.probeStats[i];draws[i]=s.draws[i];triangles[i]=s.triangles[i];}
    normalGpu=s.normalGpu;normalFrames=s.normalFrames;counted=s.countedFrames;
    LightLock_Unlock(&s.lock);
    if(!normalFrames)return false;
    char path[64];std::snprintf(path,sizeof path,"sdmc:/3ds/MKWiiDS/perf-race-%03d-passes.csv",race);
    // Older files move aside: v2 has draw counts, v1 neither (no vertex_ms).
    bool fresh=true;if(FILE* f=std::fopen("sdmc:/3ds/MKWiiDS/perf-passes.csv","r")){char head[512]{};const bool ok=std::fgets(head,sizeof head,f)!=nullptr;std::fclose(f);
        fresh=!ok||!std::strstr(head,"vertex_ms");
        if(fresh&&ok)std::rename("sdmc:/3ds/MKWiiDS/perf-passes.csv",std::strstr(head,"draws_per_frame")?"sdmc:/3ds/MKWiiDS/perf-passes-v2.csv":"sdmc:/3ds/MKWiiDS/perf-passes-v1.csv");}
    FILE* all=std::fopen("sdmc:/3ds/MKWiiDS/perf-passes.csv","a");
    if(all&&fresh)std::fputs("race,course,pass,samples,gpu_saved_ms,stderr_ms,neighbour_gpu_ms,share_pct,race_gpu_ms,draws_per_frame,triangles_per_frame,vertex_samples,vertex_ms,vertex_stderr_ms\n",all);
    if(FILE* f=std::fopen(path,"w")){
        std::fputs("pass,samples,gpu_saved_ms,stderr_ms,neighbour_gpu_ms,share_pct,race_gpu_ms,draws_per_frame,triangles_per_frame,vertex_samples,vertex_ms,vertex_stderr_ms\n",f);
        const double mean=normalGpu/normalFrames,frames=counted?counted:1;
        // "none" holds draws outside every pass marker (it is never left out).
        for(unsigned i=PassNone;i<PassCount;++i){
            const auto& p=stats[i];if(!p.samples&&!draws[i])continue;
            const double n=p.samples?p.samples:1,d=p.delta/n,var=n>1?(p.deltaSq-n*d*d)/(n-1):0,se=var>0?__builtin_sqrt(var/n):0,nb=p.neighbours/n;
            const auto& v=probes[i];const double vn=v.samples?v.samples:1,vd=v.delta/vn,vvar=vn>1?(v.deltaSq-vn*vd*vd)/(vn-1):0,vse=vvar>0?__builtin_sqrt(vvar/vn):0;
            std::fprintf(f,"%s,%u,%.3f,%.3f,%.3f,%.1f,%.3f,%.1f,%.0f,%u,%.3f,%.3f\n",PassNames[i],p.samples,d,se,nb,nb>0?100*d/nb:0,mean,draws[i]/frames,triangles[i]/frames,v.samples,vd,vse);
            if(all)std::fprintf(all,"%d,%u,%s,%u,%.3f,%.3f,%.3f,%.1f,%.3f,%.1f,%.0f,%u,%.3f,%.3f\n",race,course,PassNames[i],p.samples,d,se,nb,nb>0?100*d/nb:0,mean,draws[i]/frames,triangles[i]/frames,v.samples,vd,vse);
        }
        std::fclose(f);
    }
    if(all)std::fclose(all);
    return true;
}

}
