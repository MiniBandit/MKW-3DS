// Regression: finish cues must survive wave preparation timeout, not just full-resident startup.
#include <mkw/race_audio.hpp>
#include <mkw/race_voice_tables.hpp>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cassert>
#include <map>
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
struct Deferred : WaveResidency {unsigned tick=0,readyTick=0;bool acquire(u32,u32)override{return tick>=readyTick;}bool ready(u32,u32)override{return tick>=readyTick;}void release(u32,u32)override{}};
int main(int argc,char**argv){
 assert(argc==3);Archive a;assert(a.load(read(argv[1])));auto raw=read(argv[2]);
 for(unsigned readyTick:{0u,100u,250u})for(unsigned rank:{1u,4u,12u}){
 HW hw;Spy spy;Deferred deferred;deferred.readyTick=readyTick;Engine engine;engine.setup(&a,&hw);setObserver(engine,&spy);engine.setResidency(&deferred);
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
 frame.karts=12;frame.raceState=4;frame.mode=0;frame.eye={0,200,-500};frame.target={0,100,0};
 auto& k=frame.kart[0];k.present=k.local=true;k.rank=rank;k.character=0;k.vehicle=1;
 for(unsigned i=0;i<3;++i)frame.goalVoice[i]=voice::Goal[0][i];
 for(unsigned t=0;t<400;++t){deferred.tick=t;frame.tick=t;k.finished=t>=10;if(k.finished)frame.raceState=7;
  assert(layer->push(frame));layer->gameFrame(engine);engine.updateGameFrame();
  for(unsigned s=0;s<6;++s)engine.updateSoundFrame();
 }
 assert(spy.notes[219]==4);assert(layer->resultVoice.load()==frame.goalVoice[rank==1?0:rank==4?1:2]);assert(engine.stats().missingData==0);
 printf("ready=%u rank=%u hardwareStarts=%d peak=%g goalNotes=%u voice=%u voiceNotes=%u missing=%u failed=%u\n",readyTick,rank,hw.starts,hw.peak,spy.notes[219],layer->resultVoice.load(),spy.notes[layer->resultVoice.load()],engine.stats().missingData,engine.stats().startsFailed);
 layer->reset(engine);engine.shutdown();
 }
}
