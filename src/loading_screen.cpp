#include <mkw/loading_screen.hpp>
#include <mkw/loading_wheel_pixels.hpp>
#include <mkw/renderer.hpp>
#include <array>
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
