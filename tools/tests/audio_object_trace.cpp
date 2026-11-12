#include <mkw/race_audio.hpp>
#include <mkw/race_voice_tables.hpp>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cassert>
#include <map>
#include <cmath>
using namespace mkw;using namespace mkw::snd;
std::vector<u8> read(const char* p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
struct HW:Hardware{
 int starts=0;float peak=0;
 void start(int,const WaveData&,int,u32)override{++starts;}
 void startStream(int,StreamFeed*,int)override{}
 void stop(int)override{}void pause(int,bool)override{}
 void set(int,f32,f32 l,f32 r,f32)override{peak=std::max(peak,std::max(l,r));}
 bool finished(int)override{return false;}
};
struct Spy:EngineObserver{
 std::map<unsigned,unsigned> notes;
 void noteOn(const NoteEvent& n)override{++notes[n.sound];}
};
int main(int argc,char**argv){
 assert(argc==3);Archive a;assert(a.load(read(argv[1])));auto raw=read(argv[2]);
 for(bool tracing:{false,true}){
 HW hw;Spy spy;Engine engine;engine.setup(&a,&hw);setObserver(engine,&spy);
 std::vector<std::unique_ptr<LoadedFile>> files;
 for(unsigned i=0;i<a.fileCount();++i){
  auto f=a.file(i);if(!f->mainSize)continue;
  assert(size_t(f->mainOffset)+f->mainSize<=raw.size());
  auto p=std::make_unique<LoadedFile>();p->fileId=i;
  p->main.assign(raw.begin()+f->mainOffset,raw.begin()+f->mainOffset+f->mainSize);
  if(f->waveSize){p->waves=raw.data()+f->waveOffset;p->waveBytes=f->waveSize;
   for(unsigned w=0;w<f->waveCount;++w)p->waveOffset.push_back(a.wave(i,w)->offset);}
  engine.addFile(p.get());files.push_back(std::move(p));
 }
 auto layer=std::make_unique<RaceAudioLayer>();RaceAudioFrame frame{};
 frame.karts=0;frame.raceState=4;frame.mode=0;frame.eye={0,200,-500};frame.target={0,100,0};
 frame.traceObjects=tracing;
 frame.objects=1;frame.object[0].id=0x193;frame.object[0].mode=3;frame.object[0].position={5000,0,0};
 unsigned records=0,initials=0,updates=0;
 for(unsigned t=0;t<240;++t){frame.tick=t;frame.objectSounds=t%40==0?1:0;
  frame.eye.x=frame.target.x=float(t)*30;
  frame.objectSound[0].slot=0;frame.objectSound[0].sound=655;frame.objectSound[0].call=RaceAudioObjectSound::Start;
  assert(layer->push(frame));layer->gameFrame(engine);engine.updateGameFrame();
  for(unsigned k=0;k<6;++k)engine.updateSoundFrame();
  RaceAudioLayer::ObjectTrace trace;
  while(layer->takeObjectTrace(trace)) {
   assert(tracing && trace.tick==t && trace.object==0x193 && trace.sound==655);
   assert(trace.userParam==0x12 && trace.mode==3);
   assert(trace.position.x==5000 && std::abs(trace.listener.x-float(t)*30)<.01f);
   const float dx=trace.position.x-trace.listener.x,dy=trace.position.y-trace.listener.y,dz=trace.position.z-trace.listener.z;
   assert(std::abs(trace.distance-std::sqrt(dx*dx+dy*dy+dz*dz))<.01f);
   assert(std::isfinite(trace.volume) && trace.volume>=0 && trace.volume<=1);
   ++records; if(trace.initial) ++initials; else ++updates;
  }
 }
 assert(!layer->objectTraceDropped.load());
 if(tracing) assert(initials>0 && updates>0); else assert(records==0);
 printf("trace=%u records=%u initial=%u updates=%u missing=%u failed=%u\n",tracing,records,initials,updates,engine.stats().missingData,engine.stats().startsFailed);
 if(tracing) {
  // Consumer stall must drop diagnostics, never block playback; drain then resume.
  for(unsigned t=240;t<1000;++t) {
   frame.tick=t*60;frame.objectSounds=t%40==0?1:0;
   assert(layer->push(frame));layer->gameFrame(engine);engine.updateGameFrame();
   for(unsigned k=0;k<6;++k)engine.updateSoundFrame();
  }
  assert(layer->objectTraceDropped.load()>0);
  RaceAudioLayer::ObjectTrace trace; unsigned buffered=0;
  while(layer->takeObjectTrace(trace)) ++buffered;
  assert(buffered==64);
  printf("bounded_queue buffered=%u dropped=%u\n",buffered,layer->objectTraceDropped.load());
 }
 layer->reset(engine);engine.shutdown();
 }
}
