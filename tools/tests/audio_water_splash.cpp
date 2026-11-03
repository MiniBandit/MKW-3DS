// A water-impact cue is distinct from a cliff fall or driver voice.
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
int main(int argc,char**argv){
 assert(argc==3);Archive a;assert(a.load(read(argv[1])));auto raw=read(argv[2]);
 for(bool splash:{false,true})for(bool local:{false,true}){
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
 frame.karts=1;frame.raceState=4;frame.mode=0;frame.eye={0,200,-500};frame.target={0,100,0};
 auto& k=frame.kart[0];k.present=true;k.local=local;k.rank=1;k.character=0;k.vehicle=1;
 for(unsigned t=0;t<180;++t){frame.tick=t;
  k.status[0]=t>=10?(1u<<4):0;
  k.cues=t==10?(cue::Fall|(splash?cue::WaterSplash:0u)):0u;
  k.fallVoice=splash?0x17:0x16;
  assert(layer->push(frame));layer->gameFrame(engine);engine.updateGameFrame();
  for(unsigned s=0;s<6;++s)engine.updateSoundFrame();
 }
 assert(spy.notes[417]==(splash?4u:0u));assert(engine.stats().missingData==0);
 if(splash){assert(hw.starts>=2);assert(hw.peak>0);}
 printf("splash=%d local=%d waterNotes=%u hardwareStarts=%d peak=%g missing=%u failed=%u\n",splash,local,spy.notes[417],hw.starts,hw.peak,engine.stats().missingData,engine.stats().startsFailed);
 layer->reset(engine);engine.shutdown();
 }
}
