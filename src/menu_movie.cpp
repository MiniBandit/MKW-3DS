#include <mkw/menu_movie.hpp>
#include <mkw/renderer.hpp>
#include <mkw/render_metrics.hpp>
#include <3ds.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace mkw {
namespace {
unsigned u16(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
std::uint32_t u32(const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
bool powerOfTwo(unsigned n){return n>=8&&n<=1024&&(n&(n-1))==0;}
// ETC1 data is stored in 8x8 texel tiles, one tile row after another from the
// top image row (tex3ds layout). A run of tile rows is one contiguous span.
constexpr unsigned TileSize=8;
}

struct MenuMovie::Impl {
 LightLock lock;
 // Main thread -> stream thread.
 std::string path;bool loop=true,closing=false,paused=false;
 std::uint64_t wantedTicks=0;
 unsigned rowFirst=0,rowEnd=~0u; // tile rows
 // Stream thread -> main thread (published by status).
 std::atomic<int> status{0}; // 0 opening, 1 open, -1 failed
 std::FILE* file=nullptr;
 Texture image;
 std::uint32_t count=0,numerator=0,denominator=0,frameBytes=0,offset=0,width=0,height=0;
 // Single staging buffer owned by the stream thread while Reading and by the
 // main thread while Ready.
 enum Stage {Free,Reading,Ready};
 int stage=Free;
 std::vector<unsigned char> staging;
 std::uint32_t stagedFrame=0;unsigned stagedFirst=0,stagedRows=0;
 std::uint32_t lastFrame=std::numeric_limits<std::uint32_t>::max();unsigned lastFirst=0,lastEnd=0;
 bool fullFrame=false;
 // Main thread only.
 bool hasFrame=false;std::uint32_t current=std::numeric_limits<std::uint32_t>::max();
 Impl(){LightLock_Init(&lock);}
 ~Impl(){if(file)std::fclose(file);}
 std::uint64_t frameFor(std::uint64_t ticks)const{
  const std::uint64_t denominatorTicks=std::uint64_t(denominator)*60;
  const std::uint64_t whole=ticks/denominatorTicks,remainder=ticks%denominatorTicks;
  const std::uint64_t max=std::numeric_limits<std::uint64_t>::max();
  const std::uint64_t part=(remainder*numerator)/denominatorTicks;
  return whole>max/numerator?max:whole*numerator>max-part?max:whole*numerator+part;
 }
 std::uint32_t wantedFrame(std::uint64_t ticks)const{
  const std::uint64_t frame=frameFor(ticks);
  return loop?std::uint32_t(frame%count):frame>=count?count-1:std::uint32_t(frame);
 }
 unsigned rowBytes()const{return width/TileSize*32;} // 4bpp: 8x8 tile = 32 bytes
};

namespace {
class Streamer {
public:
 static Streamer& instance(){static Streamer value;return value;}
 void add(const std::shared_ptr<MenuMovie::Impl>& stream){
  LightLock_Lock(&lock_);streams_.push_back(stream);
  if(!thread_){s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
   // Between the main thread and the general asset loader: frames stay
   // timely while page textures load, and it never preempts frame work.
   thread_=threadCreate(entry,this,32*1024,std::clamp<int>(priority+1,0x18,0x3f),-2,true);}
  if(!thread_)stream->status.store(-1,std::memory_order_release);
  LightLock_Unlock(&lock_);wake();
 }
 void wake(){LightEvent_Signal(&event_);}
private:
 Streamer(){LightLock_Init(&lock_);LightEvent_Init(&event_,RESET_ONESHOT);}
 static void entry(void* self){static_cast<Streamer*>(self)->loop();}
 void loop(){
  for(;;){
   LightLock_Lock(&lock_);auto list=streams_;LightLock_Unlock(&lock_);
   bool worked=false;
   for(auto& stream:list)worked|=service(*stream);
   LightLock_Lock(&lock_);
   streams_.erase(std::remove_if(streams_.begin(),streams_.end(),[](const auto& s){return s->closing&&!s->file;}),streams_.end());
   LightLock_Unlock(&lock_);
   if(!worked)LightEvent_Wait(&event_);
   // This worker outranks AssetLoader on the same core. Several active
   // previews must not consume every idle slice while a page is loading.
   else svcSleepThread(1000000);
  }
 }
 static bool open(MenuMovie::Impl& s,const std::string& path){
  std::FILE* file=std::fopen(path.c_str(),"rb");if(!file)return false;
  std::setvbuf(file,nullptr,_IONBF,0);
  unsigned char header[64];
  if(std::fread(header,1,sizeof header,file)!=sizeof header||std::memcmp(header,"GMV1",4)!=0||u32(header+4)!=1){std::fclose(file);return false;}
  const unsigned width=u16(header+8),height=u16(header+10);
  const std::uint32_t count=u32(header+12),numerator=u32(header+16),denominator=u32(header+20),frameBytes=u32(header+24),offset=u32(header+28);
  const std::uint64_t total=std::uint64_t(offset)+std::uint64_t(frameBytes)*count;
  if(!powerOfTwo(width)||!powerOfTwo(height)||!count||count>50000||!numerator||!denominator||denominator>1000000||std::uint64_t(numerator)>std::uint64_t(denominator)*120||frameBytes!=width*height/2||offset!=64||total>std::uint64_t(std::numeric_limits<long>::max())){std::fclose(file);return false;}
  if(std::fseek(file,0,SEEK_END)!=0||std::ftell(file)!=long(total)){std::fclose(file);return false;}
  if(!s.image.initMovieEtc1(width,height)){std::fclose(file);return false;}
  s.staging.resize(frameBytes);
  s.file=file;s.width=width;s.height=height;s.count=count;s.numerator=numerator;s.denominator=denominator;s.frameBytes=frameBytes;s.offset=offset;
  return true;
 }
 // Returns true if it performed file work.
 bool service(MenuMovie::Impl& s){
  LightLock_Lock(&s.lock);
  if(s.closing){
   std::FILE* file=s.file;s.file=nullptr;LightLock_Unlock(&s.lock);
   if(file){std::fclose(file);return true;}
   return false;
  }
  if(s.status.load()==0){
   const std::string path=s.path;LightLock_Unlock(&s.lock);
   const bool ok=open(s,path);
   s.status.store(ok?1:-1,std::memory_order_release);return true;
  }
  if(s.status.load()!=1||s.stage!=MenuMovie::Impl::Free||(s.paused&&s.fullFrame)){LightLock_Unlock(&s.lock);return false;}
  const std::uint32_t frame=s.wantedFrame(s.wantedTicks);
  const unsigned rows=s.height/TileSize;
  unsigned first=s.fullFrame?std::min(s.rowFirst,rows):0,end=s.fullFrame?std::min(s.rowEnd,rows):rows;
  if(first>=end){LightLock_Unlock(&s.lock);return false;}
  if(frame==s.lastFrame&&first>=s.lastFirst&&end<=s.lastEnd){LightLock_Unlock(&s.lock);return false;}
  s.stage=MenuMovie::Impl::Reading;LightLock_Unlock(&s.lock);
  const unsigned rowBytes=s.rowBytes(),bytes=(end-first)*rowBytes;
  const long position=long(std::uint64_t(s.offset)+std::uint64_t(frame)*s.frameBytes+std::uint64_t(first)*rowBytes);
  const bool ok=std::fseek(s.file,position,SEEK_SET)==0&&std::fread(s.staging.data(),1,bytes,s.file)==bytes;
  LightLock_Lock(&s.lock);
  if(ok){s.stagedFrame=frame;s.stagedFirst=first;s.stagedRows=end-first;s.stage=MenuMovie::Impl::Ready;
   s.lastFrame=frame;s.lastFirst=first;s.lastEnd=end;s.fullFrame=true;}
  else {s.stage=MenuMovie::Impl::Free;s.status.store(-1,std::memory_order_release);}
  // A failed frame is terminal for this stream. Retrying it in a busy loop
  // starves lower-priority page loads and can hold the confirmation forever.
  LightLock_Unlock(&s.lock);
  return true;
 }
 Thread thread_=nullptr;
 LightLock lock_;LightEvent event_;
 std::vector<std::shared_ptr<MenuMovie::Impl>> streams_;
};
}

MenuMovie::MenuMovie(){}
MenuMovie::~MenuMovie(){close();}
void MenuMovie::close(){
 if(!impl_)return;
 // The stream thread closes the file and drops its reference; the texture is
 // released with the last reference. Owners retire MenuMovie objects through
 // Renderer::retire, so a frame still sampling the texture has completed.
 LightLock_Lock(&impl_->lock);impl_->closing=true;LightLock_Unlock(&impl_->lock);
 Streamer::instance().wake();impl_.reset();
}
bool MenuMovie::open(const char* path,bool loop){
 close();impl_=std::make_shared<Impl>();impl_->path=path;impl_->loop=loop;
 Streamer::instance().add(impl_);return true;
}
void MenuMovie::request(std::uint64_t ticks){
 if(!impl_)return;
 LightLock_Lock(&impl_->lock);const bool changed=impl_->wantedTicks!=ticks;impl_->wantedTicks=ticks;LightLock_Unlock(&impl_->lock);
 if(changed)Streamer::instance().wake();
}
void MenuMovie::setPaused(bool paused){
 if(!impl_)return;
 LightLock_Lock(&impl_->lock);const bool changed=impl_->paused!=paused;impl_->paused=paused;LightLock_Unlock(&impl_->lock);
 if(changed)Streamer::instance().wake();
}
void MenuMovie::setRows(unsigned first,unsigned end){
 if(!impl_)return;
 const unsigned tileFirst=first/TileSize,tileEnd=(end+TileSize-1)/TileSize;
 LightLock_Lock(&impl_->lock);const bool changed=impl_->rowFirst!=tileFirst||impl_->rowEnd!=tileEnd;
 impl_->rowFirst=tileFirst;impl_->rowEnd=tileEnd;LightLock_Unlock(&impl_->lock);
 if(changed)Streamer::instance().wake();
}
void MenuMovie::upload(){
 if(!impl_||impl_->status.load(std::memory_order_acquire)!=1)return;
 auto& s=*impl_;
 LightLock_Lock(&s.lock);const bool ready=s.stage==Impl::Ready;LightLock_Unlock(&s.lock);
 if(!ready)return;
 const unsigned rowBytes=s.rowBytes();
 auto* destination=static_cast<unsigned char*>(s.image.gpu.data)+s.stagedFirst*rowBytes;
 std::memcpy(destination,s.staging.data(),s.stagedRows*rowBytes);
 flushGpuData(destination,s.stagedRows*rowBytes);
 s.current=s.stagedFrame;s.hasFrame=true;
 LightLock_Lock(&s.lock);s.stage=Impl::Free;LightLock_Unlock(&s.lock);
 Streamer::instance().wake();
}
bool MenuMovie::uploadBlocking(std::uint64_t ticks){
 if(!impl_)return false;
 request(ticks);
 for(unsigned attempt=0;attempt<4000;++attempt){
  if(failed())return false;
  upload();
  if(impl_->hasFrame&&impl_->current==impl_->wantedFrame(ticks))return true;
  svcSleepThread(1000000);
 }
 return false;
}
const Texture* MenuMovie::texture()const{return impl_&&impl_->hasFrame?&impl_->image:nullptr;}
bool MenuMovie::ready()const{return impl_&&impl_->hasFrame;}
bool MenuMovie::failed()const{return impl_&&impl_->status.load(std::memory_order_acquire)<0;}
bool MenuMovie::finished()const{
 if(!impl_||impl_->status.load(std::memory_order_acquire)!=1||impl_->loop)return false;
 LightLock_Lock(&impl_->lock);const auto ticks=impl_->wantedTicks;LightLock_Unlock(&impl_->lock);
 return impl_->frameFor(ticks)>=impl_->count;
}
unsigned MenuMovie::currentFrame()const{return impl_?impl_->current:0;}
unsigned MenuMovie::frameCount()const{return impl_&&impl_->status.load()==1?impl_->count:0;}
unsigned MenuMovie::height()const{return impl_&&impl_->status.load()==1?impl_->height:0;}
float MenuMovie::framesPerSecond()const{return impl_&&impl_->status.load()==1&&impl_->denominator?float(impl_->numerator)/float(impl_->denominator):0;}
}
