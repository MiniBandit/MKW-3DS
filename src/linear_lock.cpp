#include <3ds.h>
#include <cstddef>

// libctru's LINEAR heap allocator is not thread-safe. The menu asset loader
// creates textures and model buffers on a background thread, so every LINEAR
// allocation in the program (including Citro3D's) is routed through one lock
// with -Wl,--wrap=linear*. A zero-initialized RecursiveLock is a valid,
// unlocked lock, so this is usable before static constructors run.
extern "C" {
void* __real_linearAlloc(size_t size);
void* __real_linearMemAlign(size_t size,size_t alignment);
void* __real_linearRealloc(void* mem,size_t size);
size_t __real_linearGetSize(void* mem);
void __real_linearFree(void* mem);
u32 __real_linearSpaceFree(void);
}
namespace {
RecursiveLock linearLock;
struct Guard {
 Guard(){RecursiveLock_Lock(&linearLock);}
 ~Guard(){RecursiveLock_Unlock(&linearLock);}
};
// libctru treats an empty free list as "not set up yet": the allocation after
// one that used the last free byte re-creates the whole heap as free, on top of
// every live block. So an allocation that would empty the list fails instead.
void* keepLinearUsable(void* p){
 if(p&&__real_linearSpaceFree()==0){__real_linearFree(p);return nullptr;}
 return p;
}
}
extern "C" {
void* __wrap_linearAlloc(size_t size){Guard g;return keepLinearUsable(__real_linearAlloc(size));}
void* __wrap_linearMemAlign(size_t size,size_t alignment){Guard g;return keepLinearUsable(__real_linearMemAlign(size,alignment));}
void* __wrap_linearRealloc(void* mem,size_t size){Guard g;return __real_linearRealloc(mem,size);}
size_t __wrap_linearGetSize(void* mem){Guard g;return __real_linearGetSize(mem);}
void __wrap_linearFree(void* mem){Guard g;__real_linearFree(mem);}
u32 __wrap_linearSpaceFree(void){Guard g;return __real_linearSpaceFree();}
}

// libctru's VRAM allocator is not thread-safe either (-Wl,--wrap=vram*). Two
// threads in it at once can corrupt its block list and hand one block to two
// owners, such as both screens' render targets. DIAG (gpu_guard.cpp) logs
// contention, double frees, overlapping blocks and bad screen transfers.
extern "C" {
void* __real_vramAlloc(size_t size);
void* __real_vramAllocAt(size_t size,vramAllocPos pos);
void* __real_vramMemAlign(size_t size,size_t alignment);
void* __real_vramMemAlignAt(size_t size,size_t alignment,vramAllocPos pos);
void* __real_vramRealloc(void* mem,size_t size);
size_t __real_vramGetSize(void* mem);
void __real_vramFree(void* mem);
u32 __real_vramSpaceFree(void);
}
namespace mkw::gpuGuard {
#ifndef MKW_RELEASE
void lockContended();
void refusedLastBlock();
void allocated(void* p,size_t size,void* caller);
void freeing(void* p,void* caller);
#else
inline void lockContended(){}
inline void refusedLastBlock(){}
inline void allocated(void*,size_t,void*){}
inline void freeing(void*,void*){}
#endif
}
namespace {
RecursiveLock vramLock;
struct VramGuard {
 VramGuard(){if(RecursiveLock_TryLock(&vramLock)!=0){mkw::gpuGuard::lockContended();RecursiveLock_Lock(&vramLock);}} // TryLock: 0 = acquired
 ~VramGuard(){RecursiveLock_Unlock(&vramLock);}
};
// As for LINEAR, but libctru restarts VRAM when both banks' free lists are
// empty: both screens' render targets then get handed out again, and the two
// screens draw over each other until the game is closed.
void* keepVramUsable(void* p){
 if(p&&__real_vramSpaceFree()==0){__real_vramFree(p);mkw::gpuGuard::refusedLastBlock();return nullptr;}
 return p;
}
}
extern "C" {
void* __wrap_vramAlloc(size_t size){VramGuard g;void* p=keepVramUsable(__real_vramAlloc(size));mkw::gpuGuard::allocated(p,size,__builtin_return_address(0));return p;}
void* __wrap_vramAllocAt(size_t size,vramAllocPos pos){VramGuard g;void* p=keepVramUsable(__real_vramAllocAt(size,pos));mkw::gpuGuard::allocated(p,size,__builtin_return_address(0));return p;}
void* __wrap_vramMemAlign(size_t size,size_t alignment){VramGuard g;void* p=keepVramUsable(__real_vramMemAlign(size,alignment));mkw::gpuGuard::allocated(p,size,__builtin_return_address(0));return p;}
void* __wrap_vramMemAlignAt(size_t size,size_t alignment,vramAllocPos pos){VramGuard g;void* p=keepVramUsable(__real_vramMemAlignAt(size,alignment,pos));mkw::gpuGuard::allocated(p,size,__builtin_return_address(0));return p;}
void* __wrap_vramRealloc(void* mem,size_t size){VramGuard g;mkw::gpuGuard::freeing(mem,__builtin_return_address(0));void* p=__real_vramRealloc(mem,size);mkw::gpuGuard::allocated(p,size,__builtin_return_address(0));return p;}
size_t __wrap_vramGetSize(void* mem){VramGuard g;return __real_vramGetSize(mem);}
void __wrap_vramFree(void* mem){VramGuard g;mkw::gpuGuard::freeing(mem,__builtin_return_address(0));__real_vramFree(mem);}
u32 __wrap_vramSpaceFree(void){VramGuard g;return __real_vramSpaceFree();}
}

// QA: counts file opens and the time spent opening (fopen is wrapped at link).
#include <cstdio>
#include <cstring>
#include <atomic>
#include <string>
#include <mkw/asset_pack.hpp>
#include <mkw/diag.hpp>
namespace mkw {std::atomic<unsigned> fileOpens{0};std::atomic<unsigned long long> fileOpenTicks{0};
std::atomic<unsigned> packReads{0};std::atomic<unsigned long long> packReadBytes{0},packReadTicks{0};
// DIAG: the last opens (path, ticks, found), printed under each load stage.
OpenTrace openTrace;}
extern "C" FILE* __real_fopen(const char* path,const char* mode);
// Diagnostic overlay (mkw/diag.hpp): while a race is profiled, append-mode
// logs under sdmc:/3ds/MKWiiDS/ write into RAM through funopen and reach
// the SD card when profiling stops. Bounded: later bytes are counted, not kept.
namespace mkw::diag {
std::atomic<unsigned> deferredOpens{0};
namespace {
struct Log {char path[64];std::string text;};
constexpr unsigned MaxLogs=16;constexpr std::size_t MaxBytes=256*1024;
Log logs[MaxLogs];unsigned logCount=0;std::size_t logBytes=0,droppedBytes=0;
RecursiveLock logLock; // zero-initialized: valid before static constructors
std::atomic<bool> deferring{false};
int logWrite(void* cookie,const char* data,size_t size){
 RecursiveLock_Lock(&logLock);
 if(logBytes+size<=MaxBytes){logs[reinterpret_cast<uintptr_t>(cookie)].text.append(data,size);logBytes+=size;}else droppedBytes+=size;
 RecursiveLock_Unlock(&logLock);
 return size;
}
int logClose(void*){return 0;}
FILE* deferredOpen(const char* path){
 RecursiveLock_Lock(&logLock);
 unsigned i=0;while(i<logCount&&std::strcmp(logs[i].path,path))++i;
 if(i==logCount){
  if(logCount==MaxLogs||std::strlen(path)>=sizeof logs[0].path){RecursiveLock_Unlock(&logLock);return nullptr;}
  std::strcpy(logs[logCount++].path,path);
 }
 RecursiveLock_Unlock(&logLock);
 // Write-only stream, no seek: bytes append to the RAM copy.
 FILE* f=funopen(reinterpret_cast<void*>(uintptr_t(i)),nullptr,logWrite,nullptr,logClose);
 if(f)deferredOpens.fetch_add(1,std::memory_order_relaxed);
 return f;
}
}
void deferSdLogs(bool on){
 if(on){deferring.store(true,std::memory_order_relaxed);return;}
 if(!deferring.exchange(false))return;
 RecursiveLock_Lock(&logLock);
 for(unsigned i=0;i<logCount;++i){
  auto& log=logs[i];if(log.text.empty())continue;
  if(FILE* f=__real_fopen(log.path,"a")){std::fwrite(log.text.data(),1,log.text.size(),f);
   if(droppedBytes&&!std::strcmp(std::strrchr(log.path,'/'),"/runtime.log"))std::fprintf(f,"diag: %u deferred log bytes dropped\n",unsigned(droppedBytes));
   std::fclose(f);}
  log.text.clear();log.text.shrink_to_fit();
 }
 logBytes=0;droppedBytes=0;
 RecursiveLock_Unlock(&logLock);
}
unsigned deferredLogBytes(){RecursiveLock_Lock(&logLock);const unsigned n=unsigned(logBytes);RecursiveLock_Unlock(&logLock);return n;}
}
#ifndef MKW_RELEASE
namespace {
// DIAG (cfl-fallback-test.txt): hide the packed Mii resource so cfl_mii.c
// takes its fallback, the console's own CFL system archive (title
// 0004009B00010202); cfl-fallback.txt records each step.
bool cflFallbackTest(){
 static const int on=[]{FILE* f=__real_fopen("sdmc:/3ds/MKWiiDS/cfl-fallback-test.txt","r");if(f)std::fclose(f);return f?1:0;}();
 return on;
}
void cflFallbackLog(const char* text){
 if(FILE* f=__real_fopen("sdmc:/3ds/MKWiiDS/cfl-fallback.txt","a")){std::fputs(text,f);std::fclose(f);}
}
}
#endif
extern "C" {
#ifdef MKW_WRAP_ROMFS_MOUNT
Result __real_romfsMountFromTitle(u64 tid,FS_MediaType mediatype,const char* name);
Result __wrap_romfsMountFromTitle(u64 tid,FS_MediaType mediatype,const char* name){
 const Result rc=__real_romfsMountFromTitle(tid,mediatype,name);
 char line[128];std::snprintf(line,sizeof line,"romfsMountFromTitle(%016llX, media %d, %s) = 0x%08lX (%s)\n",
  (unsigned long long)tid,int(mediatype),name?name:"",(unsigned long)rc,R_SUCCEEDED(rc)?"mounted":"failed");
 cflFallbackLog(line);
 return rc;
}
#endif
FILE* __wrap_fopen(const char* path,const char* mode){
#ifndef MKW_RELEASE
 if(path&&cflFallbackTest()){
  if(!std::strcmp(path,"mii/CFL_Res.dat")){cflFallbackLog("test: packed mii/CFL_Res.dat hidden\n");return nullptr;}
  if(!std::strncmp(path,"$CFLRES:",8)){
   FILE* f=__real_fopen(path,mode);long size=-1;
   if(f){std::fseek(f,0,SEEK_END);size=std::ftell(f);std::rewind(f);}
   char line[160];std::snprintf(line,sizeof line,"open %s: %s, %ld bytes\n",path,f?"ok":"FAILED",size);cflFallbackLog(line);
   return f;
  }
 }
#endif
 const u64 start=svcGetSystemTick();FILE* file=nullptr;
 if(mkw::diag::deferring.load(std::memory_order_relaxed)&&path&&mode&&mode[0]=='a'&&
    !std::strncmp(path,"sdmc:/3ds/MKWiiDS/",18)&&std::strncmp(path+18,"perf-",5))file=mkw::diag::deferredOpen(path);
 // Game files (relative paths, read-only) come from the packs first; a file
 // no pack holds is opened loose, as are older SD folders' files.
 if(!file&&path&&mode&&mode[0]=='r'&&!std::strchr(mode,'+')&&!std::strchr(path,':'))file=mkw::pack::openFile(path);
 if(!file)file=__real_fopen(path,mode);
 const u64 ticks=svcGetSystemTick()-start;
 mkw::fileOpens.fetch_add(1,std::memory_order_relaxed);mkw::fileOpenTicks.fetch_add(ticks,std::memory_order_relaxed);
 if(path)mkw::openTrace.add(path,ticks,file!=nullptr);
 // DIAG: every failed open of a game asset (relative path) is logged, so a
 // missing loose file shows even outside the traced load stages.
 if(!file&&path&&!std::strchr(path,':'))if(FILE* log=__real_fopen("sdmc:/3ds/MKWiiDS/missing-opens.txt","a")){std::fprintf(log,"%s %s\n",path,mode?mode:"");std::fclose(log);}
 return file;
}
}

#ifndef MKW_RELEASE
// DIAG GPU memory guard (see the VRAM wrappers above). Writes
// sdmc:/3ds/MKWiiDS/gpu-guard.log at once, outside the race log deferral, so a
// hang still leaves the lines. Checks:
//  - two threads in the VRAM allocator at once (the lock serializes them);
//  - a free of a block that is not live (double free) and a new block that
//    overlaps a live one (a corrupted allocator);
//  - the first VRAM call from each thread;
//  - each screen transfer: the size must match its screen, and the source must
//    lie in a live VRAM block.
#include <cstdarg>
#include <citro3d.h>
#include <mkw/present_pacing.hpp>
namespace mkw::gpuGuard {
namespace {
struct Block {uintptr_t at;size_t size;u32 thread;void* caller;};
constexpr unsigned MaxBlocks=1024;
Block blocks[MaxBlocks];unsigned blockCount=0;bool tableFull=false;
RecursiveLock tableLock; // zero-initialized: valid before static constructors
u32 threads[8]{};unsigned threadCount=0;
unsigned lines=0;
u32 threadId(){u32 id=0;svcGetThreadId(&id,CUR_THREAD_HANDLE);return id;}
void log(const char* format,...){
 if(lines>=400)return;++lines;
 char text[224];va_list args;va_start(args,format);std::vsnprintf(text,sizeof text,format,args);va_end(args);
 if(FILE* f=__real_fopen("sdmc:/3ds/MKWiiDS/gpu-guard.log","a")){
  std::fprintf(f,"%.1f s thread %lu: %s\n",double(svcGetSystemTick())/SYSCLOCK_ARM11,(unsigned long)threadId(),text);std::fclose(f);
 }
}
void noteThread(const char* what,void* caller){
 const u32 id=threadId();
 for(unsigned i=0;i<threadCount;++i)if(threads[i]==id)return;
 if(threadCount<8)threads[threadCount++]=id;
 log("first VRAM %s from this thread (caller %08lx)",what,(unsigned long)uintptr_t(caller));
}
Block* find(uintptr_t at){for(unsigned i=0;i<blockCount;++i)if(blocks[i].at==at)return &blocks[i];return nullptr;}
const Block* containing(uintptr_t at){for(unsigned i=0;i<blockCount;++i)if(at>=blocks[i].at&&at<blocks[i].at+blocks[i].size)return &blocks[i];return nullptr;}
}
void lockContended(){
 static unsigned count=0;
 if(++count<=50)log("VRAM allocator already in use by another thread (contention %u)",count);
}
void refusedLastBlock(){log("VRAM exactly full: refusing the last free block (next line) so libctru does not restart");}
void allocated(void* p,size_t size,void* caller){
 RecursiveLock_Lock(&tableLock);
 noteThread("allocation",caller);
 if(!p){log("VRAM allocation of %lu bytes failed (caller %08lx)",(unsigned long)size,(unsigned long)uintptr_t(caller));RecursiveLock_Unlock(&tableLock);return;}
 const uintptr_t at=uintptr_t(p);
 for(unsigned i=0;i<blockCount;++i){
  const Block& b=blocks[i];
  if(at<b.at+b.size&&b.at<at+size)
   log("OVERLAP: new VRAM block %08lx+%lu (caller %08lx) overlaps live %08lx+%lu (thread %lu, caller %08lx)",
       (unsigned long)at,(unsigned long)size,(unsigned long)uintptr_t(caller),(unsigned long)b.at,(unsigned long)b.size,(unsigned long)b.thread,(unsigned long)uintptr_t(b.caller));
 }
 if(blockCount<MaxBlocks)blocks[blockCount++]={at,size,threadId(),caller};
 else if(!tableFull){tableFull=true;log("VRAM table full (%u blocks): later blocks are not checked",MaxBlocks);}
 RecursiveLock_Unlock(&tableLock);
}
void freeing(void* p,void* caller){
 if(!p)return;
 RecursiveLock_Lock(&tableLock);
 noteThread("free",caller);
 if(Block* b=find(uintptr_t(p)))*b=blocks[--blockCount];
 else if(!tableFull)log("DOUBLE FREE or unknown VRAM free %08lx (caller %08lx)",(unsigned long)uintptr_t(p),(unsigned long)uintptr_t(caller));
 RecursiveLock_Unlock(&tableLock);
}
void logUnitFourBit(int unit,const C3D_Tex* tex,void* caller){
 log("4-BIT VRAM TEXTURE ON UNIT %d: %08lx %ux%u format %d (caller %08lx)",unit,(unsigned long)uintptr_t(tex->data),unsigned(tex->width),unsigned(tex->height),int(tex->fmt),(unsigned long)uintptr_t(caller));
}
void checkTransfer(u32* in,u32 indim,u32* out,u32 outdim){
 static unsigned bad=0;
 if(bad>=100)return;
 u16 w=0,h=0;
 const u32* bottom=reinterpret_cast<u32*>(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h));
 const u32* topLeft=reinterpret_cast<u32*>(gfxGetFramebuffer(GFX_TOP,GFX_LEFT,nullptr,nullptr));
 const u32* topRight=reinterpret_cast<u32*>(gfxGetFramebuffer(GFX_TOP,GFX_RIGHT,nullptr,nullptr));
 const char* screen=out==bottom?"bottom":out==topLeft?"top-left":out==topRight&&topRight!=topLeft?"top-right":nullptr;
 if(!screen)return; // captures and copies into textures
 const u32 expected=out==bottom?GX_BUFFER_DIM(240,320):out==topLeft&&gfxIsWide()?GX_BUFFER_DIM(240,800):GX_BUFFER_DIM(240,400);
 if(indim!=expected||outdim!=expected){
  ++bad;log("SCREEN TRANSFER to %s with size %lux%lu (expected %lux%lu), source %08lx",screen,
      (unsigned long)(indim&0xffff),(unsigned long)(indim>>16),(unsigned long)(expected&0xffff),(unsigned long)(expected>>16),(unsigned long)uintptr_t(in));
 }
 const uintptr_t at=uintptr_t(in);
 // Each screen's source normally changes only when its target is re-created
 // (the bottom one per race). Log every change: a source inside another
 // screen's block (not at a block start) means a corrupted target address.
 static uintptr_t lastSource[3]{};
 const unsigned slot=out==bottom?0:out==topLeft?1:2;
 if(lastSource[slot]!=at){
  RecursiveLock_Lock(&tableLock);
  const Block* b=containing(at);
  log("%s screen now transfers from %08lx (%s %08lx+%lu)",screen,(unsigned long)at,
      !b?"no live block":b->at==at?"start of block":"INSIDE block",(unsigned long)(b?b->at:0),(unsigned long)(b?b->size:0));
  for(unsigned i=0;i<3;++i)if(i!=slot&&lastSource[i]&&b&&lastSource[i]>=b->at&&lastSource[i]<b->at+b->size)
   log("SCREENS SHARE VRAM: %s source %08lx and the other screen's %08lx are in one block",screen,(unsigned long)at,(unsigned long)lastSource[i]);
  RecursiveLock_Unlock(&tableLock);
  lastSource[slot]=at;
 }
 if(at>=0x1F000000&&at<0x1F600000){
  RecursiveLock_Lock(&tableLock);
  const Block* b=containing(at);
  const size_t need=size_t(indim&0xffff)*(indim>>16)*4;
  if(!tableFull&&(!b||at+need>b->at+b->size)){
   ++bad;log("SCREEN TRANSFER to %s from VRAM %08lx that is %s",screen,(unsigned long)at,b?"smaller than the image":"not allocated (freed?)");
  }
  RecursiveLock_Unlock(&tableLock);
 }
}
}
// A 4-bit texture (ETC1, L4, A4) in VRAM sampled by texture unit 1 or 2 can
// destabilize the PICA and corrupt later output on both screens (3dbrew
// GPU/Pitfalls); Azahar doesn't show it. Each such texture is logged once.
extern "C" void __real_C3D_TexBind(int unit,C3D_Tex* tex);
extern "C" void __wrap_C3D_TexBind(int unit,C3D_Tex* tex){
 if(unit>0&&tex&&tex->data){
  const uintptr_t at=uintptr_t(tex->data);
  if(at>=0x1F000000&&at<0x1F600000&&(tex->fmt==GPU_ETC1||tex->fmt==GPU_L4||tex->fmt==GPU_A4)){
   static uintptr_t seen[64];static unsigned seenCount=0;bool known=false;
   for(unsigned i=0;i<seenCount;++i)known|=seen[i]==at;
   if(!known){if(seenCount<64)seen[seenCount++]=at;
    mkw::gpuGuard::logUnitFourBit(unit,tex,__builtin_return_address(0));}
  }
 }
 __real_C3D_TexBind(unit,tex);
}
extern "C" Result __real_GX_DisplayTransfer(u32* inadr,u32 indim,u32* outadr,u32 outdim,u32 flags);
extern "C" Result __wrap_GX_DisplayTransfer(u32* inadr,u32 indim,u32* outadr,u32 outdim,u32 flags){
 mkw::gpuGuard::checkTransfer(inadr,indim,outadr,outdim);
 return __real_GX_DisplayTransfer(inadr,indim,outadr,outdim,flags);
}
// DIAG: present pacing counters (mkw/present_pacing.hpp). citro3d swaps in its
// GX queue callback and counts VBlanks in its VBlank0 callback, both on
// libctru's GSP event thread, so these run in order on one thread.
extern "C" void __real_gfxScreenSwapBuffers(gfxScreen_t scr,bool hasStereo);
extern "C" void __wrap_gfxScreenSwapBuffers(gfxScreen_t scr,bool hasStereo){
 auto& c=mkw::presentPacing::counters;constexpr auto Relaxed=std::memory_order_relaxed;
 // A present still pending now is replaced: GSP never shows it.
 const bool replaced=gspIsPresentPending(scr==GFX_TOP?GSP_SCREEN_TOP:GSP_SCREEN_BOTTOM);
 if(scr==GFX_TOP){
  c.topPresents.fetch_add(1,Relaxed);if(replaced)c.topReplaced.fetch_add(1,Relaxed);
  // Shown at the next VBlank: latency from the frame's main-loop VBlank.
  if(const uint32_t start=c.frameStart.exchange(0,Relaxed)){
   const uint32_t latency=C3D_FrameCounter(0)+1-start;
   c.latency[latency<1?0:latency>=mkw::presentPacing::LatencyBins?mkw::presentPacing::LatencyBins-1:latency-1].fetch_add(1,Relaxed);
  }
  if(const uint64_t at=c.vblankTick.load(Relaxed)){
   const unsigned ms=unsigned((svcGetSystemTick()-at)*1000/SYSCLOCK_ARM11);
   c.phase[ms<mkw::presentPacing::PhaseBins?ms:mkw::presentPacing::PhaseBins-1].fetch_add(1,Relaxed);
  }
 }else{c.bottomPresents.fetch_add(1,Relaxed);if(replaced)c.bottomReplaced.fetch_add(1,Relaxed);}
 __real_gfxScreenSwapBuffers(scr,hasStereo);
}
namespace {
std::atomic<ThreadFunc> c3dVBlank0{nullptr};std::atomic<void*> c3dVBlank0Data{nullptr};
void pacingVBlank0(void*){
 auto& c=mkw::presentPacing::counters;
 c.vblankTick.store(svcGetSystemTick(),std::memory_order_relaxed);c.vblanks.fetch_add(1,std::memory_order_relaxed);
 const uint32_t presents=c.topPresents.load(std::memory_order_relaxed);
 if(presents==c.lastPresents)c.repeats.fetch_add(1,std::memory_order_relaxed);
 c.lastPresents=presents;
 if(const ThreadFunc cb=c3dVBlank0.load(std::memory_order_acquire))cb(c3dVBlank0Data.load(std::memory_order_relaxed));
}
}
extern "C" void __real_gspSetEventCallback(GSPGPU_Event id,ThreadFunc cb,void* data,bool oneShot);
extern "C" void __wrap_gspSetEventCallback(GSPGPU_Event id,ThreadFunc cb,void* data,bool oneShot){
 // citro3d's frame counter stays chained behind the VBlank count.
 if(id==GSPGPU_EVENT_VBlank0&&!oneShot){
  c3dVBlank0Data.store(data,std::memory_order_relaxed);c3dVBlank0.store(cb,std::memory_order_release);
  __real_gspSetEventCallback(id,pacingVBlank0,nullptr,false);return;
 }
 __real_gspSetEventCallback(id,cb,data,oneShot);
}
#endif
