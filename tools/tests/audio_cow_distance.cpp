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
 for(unsigned sound=652;sound<=659;++sound)for(float distance:{100.f,10000.f,50000.f}){
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

 auto layer=std::make_unique<RaceAudioLayer>();RaceAudioFrame f{};
 f.karts=0;f.raceState=4;f.eye={0,0,0};f.target={0,0,1};f.objects=1;
 f.object[0].id=0x193;f.object[0].mode=3;f.object[0].position={distance,0,0};
 for(unsigned tick=0;tick<180;++tick){f.tick=tick;f.objectSounds=tick==0?1:0;
 f.objectSound[0].slot=0;f.objectSound[0].sound=sound;f.objectSound[0].call=sound==655?RaceAudioObjectSound::Start:RaceAudioObjectSound::Voice;
 assert(layer->push(f));layer->gameFrame(engine);engine.updateGameFrame();
 for(unsigned k=0;k<6;++k)engine.updateSoundFrame();}
 printf("sound=%u distance=%.0f hardware_starts=%d peak_gain=%.9g missing=%u failed=%u\n",sound,distance,hw.starts,hw.peak,engine.stats().missingData,engine.stats().startsFailed);
 assert(engine.stats().missingData==0);
 if(distance==50000.f)assert(hw.peak<.001f);
 }
}
