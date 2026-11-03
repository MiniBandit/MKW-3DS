#include <mkw/race_damage_residency.hpp>
#include <mkw/sound_scene.hpp>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
using namespace mkw;
using namespace mkw::snd;
std::vector<u8> read(const char* path){std::ifstream f(path,std::ios::binary);assert(f);return {(std::istreambuf_iterator<char>(f)),{}};}
struct Spy : Hardware {
 unsigned starts=0;float gain=0;
 void start(int,const WaveData& w,int channel,u32)override{assert(w.channel[channel].data);++starts;}
 void startStream(int,StreamFeed*,int)override{assert(false);}
 void stop(int)override{}
 void pause(int,bool)override{}
 void set(int,f32,f32 l,f32 r,f32)override{gain=std::max(gain,l+r);}
 bool finished(int)override{return false;}
};
struct NoCache : WaveResidency {
 Engine& engine;explicit NoCache(Engine& e):engine(e){}
 bool ready(u32 f,u32 w)override{auto* file=engine.fileData(f);return file&&w<file->waveOffset.size()&&file->waveOffset[w]!=InvalidId;}
 bool acquire(u32 f,u32 w)override{return ready(f,w);}
 void release(u32,u32)override{}
};
bool resident(const ScenePlan& p,u32 file,u32 wave){auto it=std::find(p.files.begin(),p.files.end(),file);return it!=p.files.end()&&(p.masks[it-p.files.begin()].empty()||p.masks[it-p.files.begin()][wave]);}
unsigned play(const Archive& a,const ScenePlan& p,const std::vector<u8>& brsar,u32 sound,bool expect){
 Spy hw;Engine engine;engine.setup(&a,&hw);NoCache cache(engine);engine.setResidency(&cache);
 std::vector<std::unique_ptr<LoadedFile>> files;std::vector<std::vector<u8>> waves;
 for(size_t i=0;i<p.files.size();++i){auto f=a.file(p.files[i]);assert(f->mainOffset+f->mainSize<=brsar.size()&&f->waveOffset+f->waveSize<=brsar.size());
  auto l=std::make_unique<LoadedFile>();l->fileId=p.files[i];l->main.assign(brsar.begin()+f->mainOffset,brsar.begin()+f->mainOffset+f->mainSize);
  waves.emplace_back(brsar.begin()+f->waveOffset,brsar.begin()+f->waveOffset+f->waveSize);l->waves=waves.back().data();l->waveBytes=f->waveSize;l->waveOffset.assign(f->waveCount,InvalidId);l->cached.assign(f->waveCount,nullptr);
  for(u32 w=0;w<f->waveCount;++w)if(resident(p,l->fileId,w))l->waveOffset[w]=a.wave(l->fileId,w)->offset;
  engine.addFile(l.get());files.push_back(std::move(l));
 }
 Handle h;assert(engine.startSound(h,sound));
 for(int frame=0;frame<WaveWaitFrames+5;++frame){engine.updateGameFrame();for(int tick=0;tick<6;++tick)engine.updateSoundFrame();}
 if(expect){assert(hw.starts>0&&hw.gain>0);assert(engine.stats().missingData==0&&engine.stats().startsFailed==0);}else{assert(hw.starts==0&&engine.stats().startsFailed>0);}
 engine.shutdown();return hw.starts;
}
int main(int argc,char**argv){assert(argc==3);Archive a;assert(a.load(read(argv[1])));auto brsar=read(argv[2]);
 const u32 voiceGroup[24]={0x46,0xe0,0x12f,0x143,0xed,0x119,0xbc,0x56,0x109,0x150,0x6a,0x11b,0xce,0xfa,0x11a,0x86,0x77,0x93,0xaf,0x15d,0xa0,0x16b,0x15e,0x178};
 unsigned tested=0,oldMissing=0,maxBytes=0;
 for(unsigned c=0;c<24;++c){u32 groups[]={id::group::GRP_CMN,id::group::GRP_RACE,voiceGroup[c]};u32 keep[]={id::group::GRP_CMN};SoundSystem::Scene sc;sc.groups=groups;sc.groupCount=3;sc.residentGroups=keep;sc.residentCount=1;sc.cacheBytes=0;
  auto old=planSoundScene(a,sc);std::vector<u32> must;appendRaceDamageEffects(must);appendRaceDamageVoices(must,c);std::sort(must.begin(),must.end());must.erase(std::unique(must.begin(),must.end()),must.end());sc.residentSounds=must.data();sc.residentSoundCount=int(must.size());auto current=planSoundScene(a,sc);unsigned bytes=0;
  for(size_t i=0;i<current.files.size();++i)for(u32 w=0;w<a.file(current.files[i])->waveCount;++w)if(resident(current,current.files[i],w)&&!resident(old,current.files[i],w))bytes+=(a.wave(current.files[i],w)->size+31)&~31u;
  maxBytes=std::max(maxBytes,bytes);
  for(u32 sound:must){auto r=a.reach(sound);assert(r.count);bool before=true;for(u32 i=0;i<r.count;++i){assert(resident(current,r.file,r.waves[i]));before&=resident(old,r.file,r.waves[i]);}if(!before)++oldMissing;play(a,current,brsar,sound,true);++tested;}
  if(c==0)play(a,old,brsar,id::SE_ITM_KAME_HIT,false);
 }
 std::vector<u32> invalid;appendRaceDamageVoices(invalid,24);appendRaceDamageVoices(invalid,255);assert(invalid.empty());
 printf("PASS original audio backend starts: %u across 24 characters; old demoted plan lacks %u; largest added residency %u bytes; zero-cache shell hit reproduced then restored\n",tested,oldMissing,maxBytes);
}
