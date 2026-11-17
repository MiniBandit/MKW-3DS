#include <mkw/loading_screen.hpp>
#include <mkw/loading_wheel_pixels.hpp>
#include <mkw/renderer.hpp>
#include <mkw/asset_pack.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <atomic>
#include <cmath>
#include <memory>
namespace mkw {
namespace {
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
// RaceLoadingPicture (one at a time).
struct PictureRect{uint16_t x,y,w,h;};
std::vector<uint8_t> pictureFile;   // the cooked file; freed once the picture is up
std::vector<uint8_t> periodPatches; // per period: its columns off, then on
std::array<PictureRect,4> periodRects{};std::array<size_t,4> periodOffsets{};
unsigned periodCount=0,pictureWidth=0;
Thread pictureThread=nullptr;std::atomic<bool> pictureStop{false};
bool pictureStereo=false;
std::array<uint8_t*,2> pictureShown{}; // the framebuffers on screen once faded in
constexpr unsigned PictureFadeFrames=10;
constexpr uint64_t PeriodTicks=uint64_t(SYSCLOCK_ARM11)*2/5; // 0.4 s per period
unsigned pictureEyes(){return pictureStereo?2u:1u;}
// The buffer libctru shows after the next swap.
uint8_t* topBack(unsigned eye){return gfxGetFramebuffer(GFX_TOP,eye?GFX_RIGHT:GFX_LEFT,nullptr,nullptr);}
size_t pictureBytes(){return size_t(pictureWidth)*240*3;}
void blitPeriod(unsigned i,bool on){
 const auto& r=periodRects[i];
 const uint8_t* patch=periodPatches.data()+periodOffsets[i]+(on?size_t(r.w)*r.h*3:0);
 for(unsigned eye=0;eye<pictureEyes();++eye){
  uint8_t* fb=pictureShown[eye];if(!fb)continue;
  // Framebuffer columns run bottom row first: screen rows y..y+h-1 are rows 240-y-h..239-y.
  for(unsigned x=0;x<r.w;++x)std::memcpy(fb+3*((r.x+x)*240+(240-r.y-r.h)),patch+size_t(3)*x*r.h,size_t(3)*r.h);
  GSPGPU_FlushDataCache(fb+3*r.x*240,3*r.w*240);
 }
}
void pictureTimer(void*){
 // Fade in from the black the menus left, one swap per refresh.
 const uint8_t* image=pictureFile.data()+8+8*periodCount;const size_t bytes=pictureBytes();
 for(unsigned f=1;f<=PictureFadeFrames;++f){
  const unsigned k=f*256/PictureFadeFrames;
  for(unsigned eye=0;eye<pictureEyes();++eye){
   uint8_t* fb=topBack(eye);
   if(k>=256)std::memcpy(fb,image,bytes);else for(size_t i=0;i<bytes;++i)fb[i]=uint8_t((image[i]*k)>>8);
   GSPGPU_FlushDataCache(fb,bytes);
   if(f==PictureFadeFrames)pictureShown[eye]=fb;
  }
  gfxScreenSwapBuffers(GFX_TOP,pictureStereo);
  // The swap takes effect at the next VBlank; only then is the other buffer free.
  svcSleepThread(16666667);
 }
 pictureFile.clear();pictureFile.shrink_to_fit();
 // The periods count up, one per 0.4 s, then start again.
 const uint64_t start=svcGetSystemTick();unsigned lit=0;
 while(!pictureStop.load(std::memory_order_acquire)){
  const unsigned want=unsigned((svcGetSystemTick()-start)/PeriodTicks%(periodCount+1));
  if(want!=lit){for(unsigned i=0;i<periodCount;++i)if((i<want)!=(i<lit))blitPeriod(i,i<want);lit=want;}
  svcSleepThread(16666667);
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
 // The display mode the menus left: 800px Mode shows 800 columns, 3D two eyes.
 const bool wide=gfxIsWide();
 const char* path=wide?"loading/race-800.bin":"loading/race-400.bin";
 const long size=pack::size(path);if(size<8)return;
 pictureFile.resize(size_t(size));
 auto fail=[](){pictureFile.clear();pictureFile.shrink_to_fit();periodPatches.clear();periodPatches.shrink_to_fit();};
 if(!pack::readInto(path,pictureFile.data(),pictureFile.size())||std::memcmp(pictureFile.data(),"MKLP",4)){fail();return;}
 uint16_t head[2];std::memcpy(head,pictureFile.data()+4,4);
 pictureWidth=head[0];periodCount=head[1];
 if(pictureWidth!=(wide?800u:400u)||periodCount>periodRects.size()){fail();return;}
 size_t at=8+8*periodCount+pictureBytes(),patches=0;
 for(unsigned i=0;i<periodCount;++i){
  std::memcpy(&periodRects[i],pictureFile.data()+8+8*i,8);const auto& r=periodRects[i];
  if(r.x+r.w>pictureWidth||r.y+r.h>240){fail();return;}
  periodOffsets[i]=patches;patches+=size_t(r.w)*r.h*6;
 }
 if(at+patches!=pictureFile.size()){fail();return;}
 periodPatches.assign(pictureFile.begin()+at,pictureFile.end());
 // Nothing may draw the top screen while the timer thread owns it.
 renderer_->waitForGpu();
 pictureStereo=!wide&&gfxIs3D();pictureShown={};pictureStop.store(false);
 s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
 pictureThread=threadCreate(pictureTimer,nullptr,8*1024,std::max<int>(priority-1,0x18),-2,false);
 if(!pictureThread){fail();return;}
 active_=true;
}
RaceLoadingPicture::~RaceLoadingPicture(){
 if(!active_)return;
 pictureStop.store(true,std::memory_order_release);
 threadJoin(pictureThread,U64_MAX);threadFree(pictureThread);pictureThread=nullptr;
 const size_t bytes=pictureBytes();
#ifndef MKW_RELEASE
 // DIAG loading-shot.txt: the picture as shown (left eye) before its fade out,
 // as loading-shot-NN.ppm. The main thread may use the SD card; the timer can't.
 if(FILE* probe=std::fopen("sdmc:/3ds/MKWiiDS/loading-shot.txt","rb")){
  std::fclose(probe);static unsigned shot=0;char path[64];
  std::snprintf(path,sizeof path,"sdmc:/3ds/MKWiiDS/loading-shot-%02u.ppm",shot++);
  if(const uint8_t* fb=pictureShown[0])if(FILE* f=std::fopen(path,"wb")){
   std::fprintf(f,"P6\n%u 240\n255\n",pictureWidth);std::vector<uint8_t> row(size_t(pictureWidth)*3);
   for(unsigned y=0;y<240;++y){for(unsigned x=0;x<pictureWidth;++x){const uint8_t* p=fb+3*(x*240+(239-y));row[3*x]=p[2];row[3*x+1]=p[1];row[3*x+2]=p[0];}std::fwrite(row.data(),1,row.size(),f);}
   std::fclose(f);
  }
 }
#endif
 // Fade the picture on screen out in place, then leave both buffers black for
 // the race's first frames (drawn black under the loading cover).
 for(unsigned f=1;f<=PictureFadeFrames;++f){
  const unsigned left=PictureFadeFrames-f,scale=left*256/(left+1);
  for(unsigned eye=0;eye<pictureEyes();++eye){
   uint8_t* fb=pictureShown[eye];if(!fb)continue;
   if(!left)std::memset(fb,0,bytes);else for(size_t i=0;i<bytes;++i)fb[i]=uint8_t((fb[i]*scale)>>8);
   GSPGPU_FlushDataCache(fb,bytes);
  }
  svcSleepThread(16666667);
 }
 for(unsigned eye=0;eye<pictureEyes();++eye){uint8_t* fb=topBack(eye);std::memset(fb,0,bytes);GSPGPU_FlushDataCache(fb,bytes);}
 pictureFile.clear();pictureFile.shrink_to_fit();periodPatches.clear();periodPatches.shrink_to_fit();pictureShown={};
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
