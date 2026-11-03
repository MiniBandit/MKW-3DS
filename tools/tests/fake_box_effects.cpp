// Original HandleFibCollision's two branches and the actual cooked particle port.
#include <mkw/kart_item_effects.hpp>
#include <mkw/nw4r_ef_draw.hpp>
#include <cassert>
#include <cstdio>
#include <memory>
using namespace mkw;
// Host filesystem boundary only: use RaceEffects' real loose-file loader.
// The 3DS archive implementation requires libctru locks and is ARM-only.
namespace mkw::pack {bool read(const char*,std::vector<uint8_t>&){return false;}}
using Color=std::array<float,4>;
static float unit(float v){return std::clamp(v,0.f,1.f);}
static unsigned checkFragmentMaterial(const ef::Material&m,bool alphaMask=false){
 unsigned checked=0;uint32_t rng=0x13579;
 auto sample=[&](){rng=rng*1664525u+1013904223u;return float(rng>>24)/255.f;};
 for(unsigned trial=0;trial<2000;++trial){
  std::array<Color,7> reg;for(auto&r:reg)for(float&v:r)v=sample();
  Color tex[2];for(auto&t:tex)for(float&v:t)v=sample();
  auto value=[&](uint8_t sel,unsigned c){return sel<7?reg[sel][c]:sel==ef::ValHalf?.5f:sel==ef::ValOne?1.f:0.f;};
  auto channel=[&](ef::Channel c){Color o;for(unsigned k=0;k<3;++k)o[k]=value(c.rgb,c.rgbFromAlpha?3:k);o[3]=value(c.alpha,3);return o;};
  const Color vertex=channel(m.vertex),lookup=channel(m.lookup);Color prev{},buffer{},result[6]{};
  for(unsigned s=0;s<m.stages;++s){
   if(s>=2){if(m.stage[s-2].bufferRgb)for(unsigned c=0;c<3;++c)buffer[c]=result[s-2][c];if(m.stage[s-2].bufferAlpha)buffer[3]=result[s-2][3];}
   const auto&st=m.stage[s];const Color constant=channel(st.constant);
   for(unsigned c=0;c<4;++c){
    float a[3];for(unsigned k=0;k<3;++k){
     const auto source=c==3?st.alphaSrc[k]:st.rgbSrc[k];const auto op=c==3?st.alphaOp[k]:st.rgbOp[k];
     const Color* color=&prev;
     switch(source){case ef::Src::Vertex:color=&vertex;break;case ef::Src::Lookup:color=&lookup;break;
      case ef::Src::Tex0:color=&tex[m.swapUnits?1:0];break;case ef::Src::Tex1:color=&tex[m.swapUnits?0:1];break;
      case ef::Src::Const:color=&constant;break;case ef::Src::Buffer:color=&buffer;break;default:break;}
     a[k]=(*color)[op==ef::Operand::Alpha||op==ef::Operand::OneMinusAlpha?3:c];
     if(op==ef::Operand::OneMinusColor||op==ef::Operand::OneMinusAlpha)a[k]=1.f-a[k];
    }
    const auto f=c==3?st.alphaFunc:st.rgbFunc;float out=0;
    switch(f){case ef::Func::Replace:out=a[0];break;case ef::Func::Modulate:out=a[0]*a[1];break;
     case ef::Func::Add:out=a[0]+a[1];break;case ef::Func::Interpolate:out=a[0]*a[2]+a[1]*(1-a[2]);break;
     default:assert(false);}
    result[s][c]=unit(out*(c==3?st.alphaScale:st.rgbScale));
   }
   prev=result[s];
  }
  for(unsigned c=0;c<4;++c){
   const float expected=c==3?reg[ef::RegC0][3]*tex[0][3]*(alphaMask?reg[ef::RegC1][3]:1.f):
    unit(unit(reg[ef::RegK0][c]*(1-tex[0][c])+reg[ef::RegC0][c]*tex[0][c])+
         unit(reg[ef::RegK1][c]*tex[1][c]+reg[ef::RegC1][c]));
   assert(std::fabs(prev[c]-expected)<.000001f);++checked;
  }
 }
 return checked;
}
struct Sink : KartFxSink {
 bool live[KartFxCount]{};unsigned creates[KartFxCount]{};EffectFrame frames[KartFxCount]{};
 float specificVelocity[KartFxCount]{};
 bool alive(KartFx f)const override{return live[unsigned(f)];}
 void create(KartFx f,const EffectFrame& m)override{if(!alive(f)){live[unsigned(f)]=true;++creates[unsigned(f)];}frames[unsigned(f)]=m;}
 void setFrame(KartFx f,const EffectFrame& m)override{frames[unsigned(f)]=m;}
 void followFade(KartFx)override{}
 void kill(KartFx f)override{live[unsigned(f)]=false;}
 void setColor(KartFx,const uint8_t*)override{}
 void setSpecificVelocity(KartFx f,float speed)override{specificVelocity[unsigned(f)]=speed;}
};
int main(int argc,char**argv){
 assert(argc==2);
 for(float base:{25.f,50.f,100.f})for(float speed:{0.f,25.f,50.f,100.f,300.f}){
  KartItemEffects fx;fx.reset(0,true);Sink sink;KartFxInput in;
  in.baseSpeed=base;in.velocity={0,0,speed};in.fakeBoxHit=in.itemBoxHit=true;fx.step(in,sink);
  const float expected=200.f*std::min(speed,std::max(base,50.f))/std::max(base,50.f);
  assert(std::fabs(sink.specificVelocity[unsigned(KartFx::FakeBoxBreak)]-expected)<.00001f);
  assert(std::fabs(sink.specificVelocity[unsigned(KartFx::ItemBoxBreak)]-expected)<.00001f);
  assert(sink.specificVelocity[unsigned(KartFx::FakeBoxVanish)]==0);
 }
 for(bool local:{false,true})for(bool immune:{false,true}){
  KartItemEffects fx;fx.reset(local?0:1,local);Sink sink;KartFxInput in;
  in.position={12,34,56};in.right={0,0,-1};in.forward={1,0,0};
  in.fakeBoxHit=!immune;in.fakeBoxVanish=immune;
  const auto effect=immune?KartFx::FakeBoxVanish:KartFx::FakeBoxBreak;
  fx.step(in,sink);assert(sink.alive(effect)&&fx.keepAlive());
  assert(sink.alive(KartFx::FakeBoxSpark)==!immune);
  assert(sink.frames[unsigned(effect)].x.x==(immune?1.f:0.f));
  in.fakeBoxHit=in.fakeBoxVanish=false;in.position.x=89;
  fx.step(in,sink);assert(sink.frames[unsigned(effect)].origin.x==89&&sink.creates[unsigned(effect)]==1);
  sink.kill(effect);fx.step(in,sink);assert(!sink.alive(effect)); // no stale event replay
  in.fakeBoxHit=!immune;in.fakeBoxVanish=immune;fx.step(in,sink);assert(sink.creates[unsigned(effect)]==2);
  in.fakeBoxHit=in.fakeBoxVanish=false;fx.step(in,sink,true);assert(!sink.alive(effect)); // source LOD table kills 21/22
  in.beforeRespawn=true;fx.step(in,sink);assert(!sink.alive(effect));
 }
 auto effects=std::make_unique<RaceEffects>();assert(effects->load(argv[1]));
 {
  KartItemEffects fx;fx.reset(0,true);Sink sink;KartFxInput in;
  in.fakeBoxHit=true;in.hasFakeBoxDriverOrigin=true;in.fakeBoxDriverOrigin={50,60,70};in.pressed=true;
  fx.step(in,sink);assert(sink.frames[unsigned(KartFx::FakeBoxSpark)].origin.y==60);
  assert(sink.frames[unsigned(KartFx::FakeBoxSpark)].scale==.7f);
  in.fakeBoxHit=false;in.hasFakeBoxDriverOrigin=false;in.pressed=false;in.position={4,5,6};fx.step(in,sink);
  assert(sink.frames[unsigned(KartFx::FakeBoxSpark)].origin.y==5&&sink.frames[unsigned(KartFx::FakeBoxSpark)].scale==.7f);
  fx.step(in,sink,true);assert(!sink.alive(KartFx::FakeBoxSpark));
  in.fakeBoxHit=true;fx.step(in,sink,true);assert(!sink.alive(KartFx::FakeBoxSpark));
 }
 for(bool local:{false,true}){
  KartItemEffects fx;fx.reset(local?0:1,local);Sink sink;KartFxInput in;in.position={1,2,3};in.itemBoxHit=true;
  fx.step(in,sink);assert(sink.creates[unsigned(KartFx::ItemBoxBreak)]==1&&fx.keepAlive());
  in.itemBoxHit=false;in.position.x=4;fx.step(in,sink,true);
  assert(sink.alive(KartFx::ItemBoxBreak)&&sink.frames[unsigned(KartFx::ItemBoxBreak)].origin.x==4);
  in.itemBoxHit=true;fx.step(in,sink,true);assert(sink.creates[unsigned(KartFx::ItemBoxBreak)]==2);
  in.beforeRespawn=true;fx.step(in,sink);assert(!sink.alive(KartFx::ItemBoxBreak));
 }
 KartFxSlots slots;assert(slots.bind(*effects));
 std::vector<ef::Material> materials(effects->system().resources.size());
 for(size_t i=0;i<materials.size();++i){
  const auto&r=effects->system().resources[i];const bool ok=ef::translateMaterial(r.desc.draw,materials[i]);
  if(!ok)std::fprintf(stderr,"untranslated cooked material: %s hidden=%u\n",r.name,(r.desc.draw.flags&ef::DrawSetting::Hidden)!=0);
  if(std::strncmp(r.name,"rk_niseBox",10)==0&&!ok){std::fprintf(stderr,"unsupported fake-box material: %s\n",r.name);assert(ok);}
  if(std::strncmp(r.name,"rk_bombBlue",11)==0)assert(ok);
  if(std::strncmp(r.name,"rk_itemBox",10)==0)assert(ok);
 }
 std::vector<ef::TextureSize> sizes;
 for(const auto&t:effects->textures)sizes.push_back({t.width,t.height,t.paddedWidth,t.paddedHeight});
 unsigned equations=0;for(size_t i=0;i<materials.size();++i){const char*n=effects->system().resources[i].name;
  if(!std::strcmp(n,"rk_niseBox_hahen")||!std::strcmp(n,"rk_niseBox_hahenS"))equations+=checkFragmentMaterial(materials[i]);
  if(!std::strcmp(n,"rk_itemBox_hahenS"))equations+=checkFragmentMaterial(materials[i],true);}
 unsigned drawn=0;
 for(bool local:{false,true})for(auto effect:{KartFx::FakeBoxBreak,KartFx::FakeBoxVanish,KartFx::ItemBoxBreak}){
  effects->reset();slots.select(local?0:1,local);EffectFrame frame;frame.origin={20,30,-100};
  slots.create(effect,frame);assert(slots.alive(effect));
  ef::DrawList list;bool visible=false;
  for(unsigned tick=0;tick<180;++tick){
   effects->step();ef::buildDrawList(effects->system(),materials,sizes.data(),sizes.size(),ef::Mtx34::identity(),list);
   for(const auto&v:list.vertices)assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
   for(const auto&b:list.batches)if(std::strncmp(b.res->name,effect==KartFx::ItemBoxBreak?"rk_itemBox":"rk_niseBox",10)==0){assert(b.count%3==0);visible|=b.count>0;}
  }
  assert(visible&&effects->liveParticles()==0&&!slots.alive(effect));
  assert(effects->system().effects.used()==0&&effects->system().emitters.used()==0&&effects->system().managers.used()==0);++drawn;
 }
 effects->reset();int blue=effects->find("rk_bombBlue");assert(blue>=0);
 EffectFrame blast;blast.origin={20,30,-100};blast.scale=1.2f;effects->burst(blue,blast);
 ef::DrawList blueDraw;bool blueVisible=false;
 for(unsigned tick=0;tick<240;++tick){
  effects->step();ef::buildDrawList(effects->system(),materials,sizes.data(),sizes.size(),ef::Mtx34::identity(),blueDraw);
  for(const auto&v:blueDraw.vertices)assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
  for(const auto&b:blueDraw.batches)if(std::strncmp(b.res->name,"rk_bombBlue",11)==0)blueVisible|=b.count>0;
 }
 assert(blueVisible&&effects->liveParticles()==0);
 assert(effects->system().effects.used()==0&&effects->system().emitters.used()==0&&effects->system().managers.used()==0);
 for(auto kind:{KartFx::FakeBoxBreak,KartFx::ItemBoxBreak}){
  effects->reset();slots.select(0,true);KartItemEffects controller;controller.reset(0,true);KartFxInput in;
  in.baseSpeed=100;in.velocity={0,0,50};in.fakeBoxHit=kind==KartFx::FakeBoxBreak;in.itemBoxHit=kind==KartFx::ItemBoxBreak;
  controller.step(in,slots);const auto*e=effects->system().active.head;assert(e&&e->emitters.head);
  while(e&&std::strcmp(e->emitters.head->res->name,kind==KartFx::FakeBoxBreak?"rk_niseBox":"rk_itemBox"))e=e->next;
  assert(e);const auto*em=e->emitters.head;assert(em->prm.velPowerSpecDir==100.f&&em->prm.emitRatio==em->res->desc.emit);
  effects->step();controller.step(KartFxInput{},slots);
  assert(e->emitters.head->prm.velPowerSpecDir==0.f);
 }
 effects->reset();slots.select(0,true);KartItemEffects electrical;electrical.reset(0,true);KartFxInput contact;
 contact.fakeBoxHit=true;contact.velocity={0,0,50};bool electricalDrawn=false;
 for(unsigned tick=0;tick<180;++tick){
  contact.position={float(tick),10,20};electrical.step(contact,slots);contact.fakeBoxHit=false;effects->step();
  ef::DrawList list;ef::buildDrawList(effects->system(),materials,sizes.data(),sizes.size(),ef::Mtx34::identity(),list);
  for(const auto&b:list.batches)if(!std::strcmp(b.res->name,"rk_niseBoxBiri"))electricalDrawn|=b.count>0;
 }
 assert(electricalDrawn&&effects->liveParticles()==0&&effects->system().effects.used()==0);
 printf("PASS fake-box contact/immunity, pose/world follow, finite lifetime/retrigger/LOD; %u full/remote cooked draws and teardown; blue blast smoke; %u original fragment RGBA equations; 15 original shard-speed cases and real EGG velocity control\n",drawn,equations);
}
