// Real retail Mega Mushroom sequence: maintenance ticks must not restart it.
#include <mkw/snd_engine.hpp>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace mkw::snd;
static std::vector<u8> read(const char* p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
struct HW:Hardware {
 unsigned starts=0,stops=0;
 void start(int,const WaveData& w,int,u32)override{assert(w.loop);++starts;}
 void startStream(int,StreamFeed*,int)override{assert(false);}
 void stop(int)override{++stops;}void pause(int,bool)override{}
 void set(int,f32,f32,f32,f32)override{}
 bool finished(int)override{return false;}
};
int main(int argc,char**argv){
 assert(argc==3||argc==4);const bool oldMaintenance=argc==4;Archive archive;assert(archive.load(read(argv[1])));auto raw=read(argv[2]);
 HW hw;Engine engine;engine.setup(&archive,&hw);std::vector<std::unique_ptr<LoadedFile>> files;
 for(unsigned id:{89u,621u}){
  const auto*f=archive.file(id);assert(f&&size_t(f->mainOffset)+f->mainSize<=raw.size());
  auto p=std::make_unique<LoadedFile>();p->fileId=id;p->main.assign(raw.begin()+f->mainOffset,raw.begin()+f->mainOffset+f->mainSize);
  if(f->waveSize){p->waves=raw.data()+f->waveOffset;p->waveBytes=f->waveSize;for(unsigned w=0;w<f->waveCount;++w)p->waveOffset.push_back(archive.wave(id,w)->offset);}
  engine.addFile(p.get());files.push_back(std::move(p));
 }
 auto soundTicks=[&]{for(int i=0;i<6;++i)engine.updateSoundFrame();};
 Handle h;assert(engine.holdSound(h,274));engine.updateGameFrame();soundTicks();assert(hw.starts==1);
 for(int round=0;round<30;++round){
  // No game input during 0.1..0.6 seconds of maintenance. Audio keeps running.
  for(int t=0;t<6+round;++t){
   engine.updateGameFrame(oldMaintenance);
   soundTicks();
  }
  assert(engine.holdSound(h,274));engine.updateGameFrame();soundTicks();
 }
 std::printf("Mega Mushroom voice starts after 30 stalls: %u\n",hw.starts);std::fflush(stdout);
 assert(hw.starts==1);assert(engine.attached(h));
 // A real game update without another hold still expires normally.
 engine.updateGameFrame();soundTicks();engine.updateGameFrame();soundTicks();
 assert(!engine.attached(h));
 // An explicit stop during maintenance remains effective.
 assert(engine.holdSound(h,274));engine.updateGameFrame();soundTicks();assert(hw.starts==2);
 engine.stop(h,0);engine.updateGameFrame(false);soundTicks();assert(!engine.attached(h));
 assert(engine.stats().missingData==0);engine.shutdown();
 std::puts("PASS: one continuous held voice; real-frame expiry and explicit stop retained.");
}
