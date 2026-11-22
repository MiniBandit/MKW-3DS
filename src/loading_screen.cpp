#include <mkw/loading_screen.hpp>
#include <mkw/loading_wheel_pixels.hpp>
#include <mkw/renderer.hpp>
#include <mkw/asset_pack.hpp>
#include <mkw/stereo.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <atomic>
#include <cmath>
#include <memory>
#include <new>
#include <cstdio>
#include <cstdarg>
#include <malloc.h>
namespace mkw {
namespace {
// A byte buffer whose allocation may fail without abort(): a failed new in a
// release build exits to the HOME Menu with no crash dump.
struct Bytes{
 std::unique_ptr<uint8_t[]> p;
 uint8_t* data() const {return p.get();}
 bool alloc(size_t n){p.reset(new(std::nothrow) uint8_t[n]);return bool(p);}
 void clear(){p.reset();}
};
#ifndef MKW_RELEASE
// DIAG: the loading picture's steps in runtime.log (main thread only: the timer
// thread must not touch the SD card).
void pictureLog(const char* format,...){
 FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/runtime.log","a");if(!f)return;
 va_list args;va_start(args,format);std::vfprintf(f,format,args);va_end(args);std::fputc('\n',f);std::fclose(f);
}
#else
void pictureLog(const char*,...){}
#endif
constexpr unsigned Size=32,Visible=24;
Renderer* renderer_=nullptr;
std::unique_ptr<Texture> wheel;
std::array<uint8_t,Size*Size*4> pixels{};
std::array<uint8_t,320*240*3> background{};
Thread spinner=nullptr;std::atomic<bool> stop{false};unsigned depth=0;
uint8_t* framebuffer=nullptr;unsigned width=320;
bool shown=false;
uint64_t started=0;
unsigned scopes=0;
// RaceLoadingPicture (one at a time; tools/cook_loading_picture.py).
struct PictureRect{uint16_t x,y,w,h;};
enum PictureLayer:unsigned{LogoLayer,TextLayer,PeriodLayer};
std::array<PictureRect,3> pictureRects{};
Bytes pictureStill;   // background, logo and LOADING; freed once both buffers hold the picture
Bytes periodLoop;     // the periods' loop, grey + alpha per frame
std::array<Bytes,2> periodBase; // per eye: the picture under the periods' area
std::atomic<unsigned> picturePresents{0}; // DIAG: refreshes the timer thread presented
unsigned pictureWidth=0,loopFrames=0,loopFps=60;
size_t logoAt=0,textAt=0;
std::array<int,2> eyeShift{};        // per eye: the logo's and LOADING's x shift (3D: out of the screen)
Thread pictureThread=nullptr;std::atomic<bool> pictureStop{false};
bool pictureStereo=false;
std::array<uint8_t*,2> pictureShown{}; // per eye: the buffer on screen
constexpr unsigned PictureFadeFrames=10;
unsigned pictureEyes(){return pictureStereo?2u:1u;}
// The buffer libctru shows after the next swap.
uint8_t* topBack(unsigned eye){return gfxGetFramebuffer(GFX_TOP,eye?GFX_RIGHT:GFX_LEFT,nullptr,nullptr);}
size_t pictureBytes(){return size_t(pictureWidth)*240*3;}
// Framebuffer columns run bottom row first: screen rows y..y+h-1 are rows 240-y-h..239-y.
uint8_t* fbColumn(uint8_t* fb,int x,const PictureRect& r){return fb+3*(size_t(x)*240+(240-r.y-r.h));}
// A premultiplied layer (columns, bottom row first; Channels 4 = BGRA, 2 = grey
// and alpha) over the framebuffer at its place plus `shift`, scaled by k/256.
template<unsigned Channels> void blendLayer(uint8_t* fb,const PictureRect& r,int shift,const uint8_t* src,unsigned k){
 for(unsigned cx=0;cx<r.w;++cx){
  const int x=int(r.x)+int(cx)+shift;if(x<0||x>=int(pictureWidth))continue;
  uint8_t* out=fbColumn(fb,x,r);const uint8_t* in=src+size_t(Channels)*cx*r.h;
  for(unsigned i=0;i<r.h;++i,out+=3,in+=Channels){
   const unsigned a=in[Channels-1];if(!a)continue;
   const unsigned keep=255-a;
   for(unsigned c=0;c<3;++c){const unsigned s=Channels==4?in[c]:in[0];out[c]=uint8_t(((s*k)>>8)+(out[c]*keep+127)/255);}
  }
 }
}
const uint8_t* periodFrame(unsigned frame){const auto& r=pictureRects[PeriodLayer];return periodLoop.data()+size_t(frame)*r.w*r.h*2;}
// The whole picture for one eye at brightness k/256, its periods at rest if asked.
void composeStill(uint8_t* fb,unsigned eye,unsigned k,bool periods){
 const uint8_t* background=pictureStill.data();const size_t bytes=pictureBytes();
 if(k>=256)std::memcpy(fb,background,bytes);else for(size_t i=0;i<bytes;++i)fb[i]=uint8_t((background[i]*k)>>8);
 blendLayer<4>(fb,pictureRects[LogoLayer],eyeShift[eye],pictureStill.data()+logoAt,k);
 blendLayer<2>(fb,pictureRects[TextLayer],eyeShift[eye],pictureStill.data()+textAt,k);
 if(periods)blendLayer<2>(fb,pictureRects[PeriodLayer],eyeShift[eye],periodFrame(0),k);
}
// The periods' area: keep what lies under it, or put that back and draw a loop frame.
void savePeriodBase(uint8_t* fb,unsigned eye){
 const auto& r=pictureRects[PeriodLayer];auto& base=periodBase[eye];
 for(unsigned cx=0;cx<r.w;++cx){const int x=int(r.x)+int(cx)+eyeShift[eye];if(x<0||x>=int(pictureWidth))continue;
  std::memcpy(base.data()+size_t(3)*cx*r.h,fbColumn(fb,x,r),size_t(3)*r.h);}
}
void drawPeriods(uint8_t* fb,unsigned eye,unsigned frame){
 const auto& r=pictureRects[PeriodLayer];const auto& base=periodBase[eye];
 for(unsigned cx=0;cx<r.w;++cx){const int x=int(r.x)+int(cx)+eyeShift[eye];if(x<0||x>=int(pictureWidth))continue;
  std::memcpy(fbColumn(fb,x,r),base.data()+size_t(3)*cx*r.h,size_t(3)*r.h);}
 blendLayer<2>(fb,r,eyeShift[eye],periodFrame(frame),256);
 const int x0=std::max(0,int(r.x)+eyeShift[eye]),x1=std::min(int(pictureWidth),int(r.x)+int(r.w)+eyeShift[eye]);
 if(x1>x0)GSPGPU_FlushDataCache(fb+size_t(3)*x0*240,size_t(3)*(x1-x0)*240);
}
// Shows the buffers just drawn at the next refresh, and waits for it.
void presentPicture(){
 for(unsigned eye=0;eye<pictureEyes();++eye)pictureShown[eye]=topBack(eye);
 gfxScreenSwapBuffers(GFX_TOP,pictureStereo);gspWaitForVBlank();
 picturePresents.fetch_add(1,std::memory_order_relaxed);
}
void pictureTimer(void*){
 // Fade in from the black the menus left, one step per refresh.
 for(unsigned f=1;f<=PictureFadeFrames;++f){
  for(unsigned eye=0;eye<pictureEyes();++eye){uint8_t* fb=topBack(eye);composeStill(fb,eye,f*256/PictureFadeFrames,true);GSPGPU_FlushDataCache(fb,pictureBytes());}
  presentPicture();
 }
 // The other buffer gets the whole picture too; from then on only the
 // periods' area is redrawn, into the hidden buffer, one loop frame per refresh.
 for(unsigned eye=0;eye<pictureEyes();++eye){
  uint8_t* fb=topBack(eye);composeStill(fb,eye,256,false);savePeriodBase(fb,eye);
  blendLayer<2>(fb,pictureRects[PeriodLayer],eyeShift[eye],periodFrame(0),256);GSPGPU_FlushDataCache(fb,pictureBytes());
 }
 presentPicture();
 pictureStill.clear();
 const uint64_t start=svcGetSystemTick();
 while(!pictureStop.load(std::memory_order_acquire)){
  // The frame due at the next refresh, so the bounce keeps its tempo.
  const unsigned frame=unsigned((svcGetSystemTick()-start)*loopFps/SYSCLOCK_ARM11+1)%loopFrames;
  for(unsigned eye=0;eye<pictureEyes();++eye)drawPeriods(topBack(eye),eye,frame);
  presentPicture();
 }
}
float angle(uint64_t start){return float(double(svcGetSystemTick()-start)/SYSCLOCK_ARM11*6.283185307);}
void raster(){
 for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x)
  loadingWheelPixel(float(x),float(y),float(Size),0,&pixels[(y*Size+x)*4]);
}
void cpuFrame(float a){
 const unsigned x0=width-Visible-8,y0=240-Visible-8;
 for(unsigned x=0;x<Visible;++x)for(unsigned y=0;y<Visible;++y){
  uint8_t p[4];loadingWheelPixel(float(x),float(y),float(Visible),a,p);
  const unsigned at=3*((x0+x)*240+(239-y0-y));auto* out=framebuffer+at;
  for(unsigned k=0;k<3;++k)out[k]=loadingWheelBlend(p[2-k],p[3],background[at+k]);
 }
 GSPGPU_FlushDataCache(framebuffer+3*x0*240,3*Visible*240);
}
void timer(void*){
 // Avoid flashing a loading screen for an operation completing immediately.
 while(!stop.load(std::memory_order_acquire)){
  if(svcGetSystemTick()-started>SYSCLOCK_ARM11/2){
   if(!shown){std::memcpy(framebuffer,background.data(),width*240*3);GSPGPU_FlushDataCache(framebuffer,width*240*3);}
   cpuFrame(angle(started));
   if(!shown){gfxScreenSwapBuffers(GFX_BOTTOM,false);shown=true;}
  }
  svcSleepThread(16666667);
 }
}
}
void initializeLoadingScreen(Renderer& renderer){
 renderer_=&renderer;raster();wheel=std::make_unique<Texture>();
 if(!wheel->initRgba8(Size,Size,pixels.data(),pixels.size()))wheel.reset();started=svcGetSystemTick();
}
void shutdownLoadingScreen(){
 stop.store(true);if(spinner){threadJoin(spinner,U64_MAX);threadFree(spinner);spinner=nullptr;}
 wheel.reset();renderer_=nullptr;depth=0;
}
LoadingScope::LoadingScope(){
 if(!renderer_)return;
 ++depth;owner_=true;if(depth!=1)return;
 ++scopes;
 renderer_->waitForGpu();
 u16 w=0,h=0;framebuffer=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);
 width=h; // libctru returns the rotated lower LCD buffer (240 x 320).
 if(!framebuffer||w!=240||h!=320)return;
 if(!renderer_->copyPresentedBottom(background.data()))background.fill(0);
 started=svcGetSystemTick();shown=false;stop.store(false);
 s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
 spinner=threadCreate(timer,nullptr,8*1024,std::max<int>(priority-1,0x18),-2,false);
}
LoadingScope::~LoadingScope(){
 if(!owner_||!depth||--depth)return;
 stop.store(true,std::memory_order_release);
 if(spinner){threadJoin(spinner,U64_MAX);threadFree(spinner);spinner=nullptr;}
 // Retire the software icon even when the next scene does not redraw this LCD.
 if(shown&&framebuffer){
  const unsigned x0=width-Visible-8,y0=240-Visible-8;
  for(unsigned x=0;x<Visible;++x)for(unsigned y=0;y<Visible;++y){
   const unsigned at=3*((x0+x)*240+(239-y0-y));
   std::memcpy(framebuffer+at,background.data()+at,3);
  }
  GSPGPU_FlushDataCache(framebuffer+3*x0*240,3*Visible*240);
 }
 shown=false;framebuffer=nullptr;
}
unsigned loadingScopes(){return scopes;}
RaceLoadingPicture::RaceLoadingPicture(){
 if(!renderer_||pictureThread)return;
#ifndef MKW_RELEASE
 // DIAG no-loading-picture.txt: races load behind the black screen, as before 1.2.0.
 if(FILE* off=std::fopen("sdmc:/3ds/mkw-3ds/no-loading-picture.txt","rb")){std::fclose(off);pictureLog("loading picture: off (no-loading-picture.txt)");return;}
#endif
 // The display mode the menus left: 800px Mode shows 800 columns, 3D two eyes.
 const bool wide=gfxIsWide();
 const char* path=wide?"loading/race-800.bin":"loading/race-400.bin";
 const long size=pack::size(path);
 uint8_t head[36];if(size<36||!pack::readRange(path,0,head,sizeof head)||std::memcmp(head,"MKLQ",4))return;
 uint16_t value[4];std::memcpy(value,head+4,8);std::memcpy(pictureRects.data(),head+12,24);
 pictureWidth=value[0];loopFrames=value[1];loopFps=value[2];
 if(pictureWidth!=(wide?800u:400u)||!loopFrames||!loopFps)return;
 for(const auto& r:pictureRects)if(!r.w||!r.h||r.x+r.w>pictureWidth||r.y+r.h>240)return;
 const auto& logo=pictureRects[LogoLayer];const auto& text=pictureRects[TextLayer];const auto& area=pictureRects[PeriodLayer];
 logoAt=pictureBytes();textAt=logoAt+size_t(logo.w)*logo.h*4;
 const size_t still=textAt+size_t(text.w)*text.h*2,loop=size_t(loopFrames)*area.w*area.h*2;
 if(36+still+loop!=size_t(size)){pictureLog("loading picture: %s size %ld does not match its header",path,size);return;}
 auto release=[](){pictureStill.clear();periodLoop.clear();for(auto& base:periodBase)base.clear();};
 const size_t baseBytes=size_t(area.w)*area.h*3;
 const struct mallinfo heap=mallinfo();
 pictureLog("loading picture: %s still %u KB loop %u KB, heap used %lu KB free %lu KB unclaimed %lu KB",path,unsigned(still/1024),unsigned(loop/1024),
  (unsigned long)heap.uordblks/1024,(unsigned long)heap.fordblks/1024,(unsigned long)(envGetHeapSize()>size_t(heap.arena)?envGetHeapSize()-size_t(heap.arena):0)/1024);
 bool ok=pictureStill.alloc(still)&&periodLoop.alloc(loop);
 for(auto& base:periodBase)if(ok){ok=base.alloc(baseBytes);if(ok)std::memset(base.data(),0,baseBytes);}
 if(!ok){pictureLog("loading picture: FAIL no memory for its %u KB, skipped",unsigned((still+loop+2*baseBytes)/1024));release();return;}
 if(!pack::readRange(path,36,pictureStill.data(),still)||!pack::readRange(path,36+still,periodLoop.data(),loop)){pictureLog("loading picture: FAIL read %s",path);release();return;}
 // 3D: the logo and LOADING... come out of the screen with the race HUD
 // (as Renderer's screen quads: left eye -parallax/2, right eye +parallax/2).
 pictureStereo=!wide&&gfxIs3D();eyeShift={};
 if(pictureStereo){const float parallax=renderer_->stereoLayer(stereo::RaceHudLayer);
  eyeShift[0]=int(std::lround(-.5f*parallax));eyeShift[1]=eyeShift[0]+int(std::lround(parallax));}
 // Nothing may draw the top screen while the timer thread owns it.
 renderer_->waitForGpu();
 pictureShown={};pictureStop.store(false);picturePresents.store(0);
 s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
 pictureThread=threadCreate(pictureTimer,nullptr,8*1024,std::max<int>(priority-1,0x18),-2,false);
 if(!pictureThread){pictureLog("loading picture: FAIL thread not created");release();return;}
 pictureLog("loading picture: shown (%s, %s, thread priority %ld)",wide?"800px":"400px",pictureStereo?"3D":"2D",long(std::max<int>(priority-1,0x18)));
 active_=true;
}
RaceLoadingPicture::~RaceLoadingPicture(){
 if(!active_)return;
 pictureStop.store(true,std::memory_order_release);
 threadJoin(pictureThread,U64_MAX);threadFree(pictureThread);pictureThread=nullptr;
 pictureLog("loading picture: load done after %u refreshes",picturePresents.load());
 const size_t bytes=pictureBytes();
#ifndef MKW_RELEASE
 // DIAG loading-shot.txt: the picture as shown before its fade out, as
 // loading-shot-NN.ppm (3D: -NN-right.ppm too). The main thread may use the
 // SD card; the timer thread can't.
 if(FILE* probe=std::fopen("sdmc:/3ds/mkw-3ds/loading-shot.txt","rb")){
  std::fclose(probe);static unsigned shot=0;
  for(unsigned eye=0;eye<pictureEyes();++eye){
   char path[64];std::snprintf(path,sizeof path,"sdmc:/3ds/mkw-3ds/loading-shot-%02u%s.ppm",shot,eye?"-right":"");
   if(const uint8_t* fb=pictureShown[eye])if(FILE* f=std::fopen(path,"wb")){
    std::fprintf(f,"P6\n%u 240\n255\n",pictureWidth);std::vector<uint8_t> row(size_t(pictureWidth)*3);
    for(unsigned y=0;y<240;++y){for(unsigned x=0;x<pictureWidth;++x){const uint8_t* p=fb+3*(x*240+(239-y));row[3*x]=p[2];row[3*x+1]=p[1];row[3*x+2]=p[0];}std::fwrite(row.data(),1,row.size(),f);}
    std::fclose(f);
   }
  }
  ++shot;
 }
#endif
 // Fade out to black, one step per refresh (each drawn into the hidden buffer
 // from the one on screen), then leave both buffers black for the race's
 // first frames (drawn black under the loading cover).
 for(unsigned f=1;f<=PictureFadeFrames;++f){
  const unsigned left=PictureFadeFrames-f,scale=left*256/(left+1);
  for(unsigned eye=0;eye<pictureEyes();++eye){
   uint8_t* to=topBack(eye);const uint8_t* from=pictureShown[eye];
   if(!left||!from)std::memset(to,0,bytes);else for(size_t i=0;i<bytes;++i)to[i]=uint8_t((from[i]*scale)>>8);
   GSPGPU_FlushDataCache(to,bytes);
  }
  presentPicture();
 }
 for(unsigned eye=0;eye<pictureEyes();++eye){uint8_t* fb=topBack(eye);std::memset(fb,0,bytes);GSPGPU_FlushDataCache(fb,bytes);}
 pictureStill.clear();periodLoop.clear();
 for(auto& base:periodBase)base.clear();
 pictureShown={};
 pictureLog("loading picture: closed");
}
void drawLoadingWheel(Renderer& renderer,uint64_t waitStarted){
 if(!wheel||!wheel->ready||!waitStarted||svcGetSystemTick()-waitStarted<=SYSCLOCK_ARM11/2)return;
 const float a=angle(waitStarted),c=std::cos(a),s=std::sin(a);
 // Call only on the lower LCD while an actual resource wait is pending.
 constexpr float cx=300,cy=220,h=12;
 float q[4][2];const float local[4][2]={{-h,-h},{h,-h},{-h,h},{h,h}};
 for(unsigned i=0;i<4;++i){q[i][0]=cx+c*local[i][0]-s*local[i][1];q[i][1]=cy+s*local[i][0]+c*local[i][1];}
 constexpr float uv[4][2]={{0,0},{1,0},{0,1},{1,1}};constexpr uint32_t color[4]={~0u,~0u,~0u,~0u};
 renderer.drawScreenMaterialQuad(wheel.get(),q,uv,color,0,~0u,1);
}
}
