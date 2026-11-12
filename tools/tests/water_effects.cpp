// Actual KartEffect controller and NW4R port, with source water boundaries
// (80698368), interpolation (806a33d0), and EGG Fade handle lifetime.
#include <mkw/kart_item_effects.hpp>
#include <mkw/nw4r_ef_draw.hpp>
#include <cassert>
#include <cstdio>
#include <memory>
using namespace mkw;
struct Sink : KartFxSink {
 bool live[KartFxCount]{};unsigned creates[KartFxCount]{},fades[KartFxCount]{},follows[KartFxCount]{};
 EffectFrame frames[KartFxCount]{};float ratio=0,size=0;uint16_t life=0;
 bool alive(KartFx f)const override{return live[unsigned(f)];}
 void create(KartFx f,const EffectFrame& m)override{unsigned i=unsigned(f);if(!live[i]){live[i]=true;++creates[i];}frames[i]=m;}
 void setFrame(KartFx f,const EffectFrame& m)override{frames[unsigned(f)]=m;}
 void followFade(KartFx f)override{++follows[unsigned(f)];}
 void fade(KartFx f)override{++fades[unsigned(f)];live[unsigned(f)]=false;}
 void kill(KartFx f)override{live[unsigned(f)]=false;}
 void setColor(KartFx,const uint8_t*)override{}
 void setEmissionRatio(KartFx f,float x)override{if(f==KartFx::WaterWave)ratio=x;}
 void setParticleLife(KartFx f,uint16_t x)override{assert(f==KartFx::WaterWave);life=x;}
 void setParticleScale(KartFx f,float x,float y)override{assert(x==y);if(f==KartFx::WaterWave)size=x;}
};
static void near(float x,float y){assert(std::fabs(x-y)<.00001f);}
int main(int argc,char** argv){
 assert(argc==2);unsigned cases=0;
 // Static pocha heights are independent of Psea/Diving. Factory has two
 // authored surfaces; a respawn must emit once and keep its world origin.
 for(float height:{7.086087226867676f,1821.5867919921875f}){
  KartItemEffects fx;fx.reset(0,true);Sink sink;KartFxInput in;
  in.hasPocha=true;in.pochaHeight=height;in.position={1,5,3};in.velocity={4,-2,6};
  fx.step(in,sink);assert(!sink.alive(KartFx::WaterFall));
  in.beforeRespawn=true;fx.step(in,sink);
  assert(sink.creates[unsigned(KartFx::WaterFall)]==1);
  auto origin=sink.frames[unsigned(KartFx::WaterFall)].origin;
  assert(origin.x==5&&origin.y==height&&origin.z==9);
  in.position={100,100,100};fx.step(in,sink);
  assert(sink.creates[unsigned(KartFx::WaterFall)]==1);
  assert(sink.frames[unsigned(KartFx::WaterFall)].origin.y==height);
 }
 {
  KartItemEffects fx;fx.reset(0,true);Sink sink;KartFxInput in;
  in.beforeRespawn=true;fx.step(in,sink);assert(!sink.alive(KartFx::WaterFall));
 }
 // Interpolate independent original literals: max .25/.15, min .075;
 // life 60..12; screen mode .5. Include the clamped boosted-speed case.
 for(bool local:{false,true})for(bool reduced:{false,true})for(float speed:{0.f,25.f,50.f,100.f}){
  KartItemEffects fx;fx.reset(local?0:1,local);Sink sink;KartFxInput in;
  in.hasSea=in.diving=true;in.seaHeight=10;in.waterBodyHeight=10;in.waterRearHeight=9;
  in.position={4,30,8};in.velocity={speed,0,0};in.waterBaseSpeed=40;in.waterReducedScreen=reduced;
  fx.step(in,sink);assert(sink.alive(KartFx::WaterWave)&&!sink.alive(KartFx::WaterBubbles));
  const unsigned step=speed==0?0:speed==25?1:2;
  const float expected=local?(step==0?.075f:step==1?.1625f:.25f):(step==0?.075f:step==1?.1125f:.15f);
  near(sink.ratio,expected*(reduced?.5f:1.f));assert(sink.life==(step==0?60:step==1?36:12));
  const auto f=sink.frames[unsigned(KartFx::WaterWave)];assert(f.origin.x==4&&f.origin.y==10&&f.origin.z==8&&f.scale==1);
  ++cases;
 }
 KartItemEffects fx;fx.reset(0,true);Sink sink;KartFxInput in;
 in.hasSea=in.diving=true;in.seaHeight=10;in.waterBodyHeight=11;in.waterRearHeight=9;
 in.position={1,11,3};in.pressed=true;fx.step(in,sink);near(sink.size,.7f);
 // Body below sea starts bubbles and FollowFade's the surface wake.
 in.waterBodyHeight=9;fx.step(in,sink);
 assert(sink.alive(KartFx::WaterBubbles)&&sink.follows[unsigned(KartFx::WaterWave)]==1);
 near(sink.frames[unsigned(KartFx::WaterBubbles)].scale,.7f);
 in.spinSerial=1;in.spinRotations=1;in.spinLeft=true;fx.step(in,sink);assert(sink.alive(KartFx::WaterSpinHalf));
 in.spinSerial=2;in.spinRotations=2;fx.step(in,sink);assert(sink.alive(KartFx::WaterSpin));
 // Leaving water detaches the two persistent emitters but follows finite
 // splash particles; the same action is not replayed on re-entry.
 in.diving=false;in.position.x=5;fx.step(in,sink);
 assert(!sink.alive(KartFx::WaterWave)&&!sink.alive(KartFx::WaterBubbles));
 assert(sink.frames[unsigned(KartFx::WaterSpin)].origin.x==5);
 assert(sink.fades[unsigned(KartFx::WaterWave)]==1&&sink.fades[unsigned(KartFx::WaterBubbles)]==1);
 in.diving=true;in.waterBodyHeight=11;fx.step(in,sink);assert(sink.creates[unsigned(KartFx::WaterWave)]==2);
 assert(sink.creates[unsigned(KartFx::WaterSpin)]==1);
 // Exact rear-wheel surface comparison, absent sea, remote split-screen
 // wake suppression, and all water slots' source LOD kill indices.
 for(unsigned gate=0;gate<5;++gate){
  KartItemEffects f;f.reset(1,false);Sink s;auto p=in;
  if(gate==0)p.waterRearHeight=p.seaHeight;
  if(gate==1)p.hasSea=false;
  if(gate==2)p.diving=false;
  if(gate==3)p.waterSplitScreen=true;
  f.step(p,s,gate==4);assert(!s.alive(KartFx::WaterWave));
 }
 for(KartFx f:{KartFx::WaterWave,KartFx::WaterBubbles,KartFx::WaterSpinHalf,KartFx::WaterSpin})
  assert(!keptByLodKill(KartFxTableIndex[unsigned(f)]));
 auto real=std::make_unique<RaceEffects>();assert(real->load(argv[1]));
 KartFxSlots slots;slots.bind(*real);slots.select(0,true);
 // Test the new roots through the real cooked descriptors and actual
 // material translator; an unsupported combiner must fail, not disappear.
 for(const char* n:{"rk_wave","rk_awa","rk_spinWaterHalf","rk_spinWaterHalfS","rk_spinWater","rk_spinWaterS","rk_pochaWater","rk_pochaWaterS"}){
  int id=real->find(n);assert(id>=0);ef::Material material;
  assert(ef::translateMaterial(real->system().resources[id].desc.draw,material)&&material.valid);
 }
 KartItemEffects f;f.reset(0,true);in.velocity={50,0,0};in.pressed=false;in.waterBodyHeight=11;in.waterRearHeight=9;
 f.step(in,slots);for(unsigned i=0;i<120;++i)real->step();assert(real->liveParticles()>0);
 const unsigned count=real->liveParticles();slots.fade(KartFx::WaterWave);assert(!slots.alive(KartFx::WaterWave));
 assert(real->liveParticles()==count); // Fade keeps already born particles.
 slots.create(KartFx::WaterWave,{});assert(slots.alive(KartFx::WaterWave));
 slots.fade(KartFx::WaterWave);slots.fade(KartFx::WaterSpin);
 slots.fade(KartFx::WaterWheelL);slots.fade(KartFx::WaterWheelR);
 for(unsigned i=0;i<300;++i)real->step();assert(real->liveParticles()==0);
 KartItemEffects staticWater;staticWater.reset(0,true);KartFxInput fall;
 fall.hasPocha=true;fall.pochaHeight=1821.5867919921875f;fall.beforeRespawn=true;
 staticWater.step(fall,slots);for(unsigned i=0;i<8;++i)real->step();assert(real->liveParticles()>0);
 std::printf("PASS %u original wave-rate/life cases, two factory pocha heights without Psea, one splash per entry, eight cooked materials and real particle teardown/spawn\n",cases);
}
