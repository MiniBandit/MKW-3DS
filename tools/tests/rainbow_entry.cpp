#include <mkw/kart_item_effects.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
struct Sink:KartFxSink{
 bool active=false;unsigned starts=0,moves=0,kills=0;EffectFrame last;
 bool alive(KartFx f)const override{return f==KartFx::RainbowEntry&&active;}
 void create(KartFx f,const EffectFrame& at)override{if(f==KartFx::RainbowEntry){assert(!active);active=true;++starts;last=at;}}
 void setFrame(KartFx f,const EffectFrame& at)override{if(f==KartFx::RainbowEntry){assert(active);++moves;last=at;}}
 void followFade(KartFx)override{}
 void kill(KartFx f)override{if(f==KartFx::RainbowEntry){active=false;++kills;}}
 void setColor(KartFx,const uint8_t[4])override{}
};
int main(){
 for(bool local:{false,true}){
  KartItemEffects fx;fx.reset(0,local);Sink sink;KartFxInput in;in.hasRainbowEntry=true;in.beforeRespawn=true;in.position={10,1000,20};in.velocity={0,-100,0};
  fx.step(in,sink);for(unsigned frame=2;frame<=34;++frame){in.position.y-=100;fx.step(in,sink);assert(!sink.active);}
  fx.step(in,sink);assert(sink.active&&sink.starts==1&&sink.last.origin.y==in.position.y);
  assert(sink.last.x.x==1&&sink.last.y.z==1&&sink.last.z.y==-1&&sink.last.scale==1);
  for(unsigned i=0;i<60;++i)fx.step(in,sink);assert(sink.starts==1);
  in.beforeRespawn=false;fx.step(in,sink);assert(!sink.active&&sink.kills==1);
 }
 KartItemEffects fx;fx.reset(0,true);Sink s;KartFxInput in;in.beforeRespawn=true;in.hasRainbowEntry=true;in.position.y=1000;
 fx.step(in,s);in.position.y=300;in.velocity.y=-100;for(unsigned i=0;i<40;++i)fx.step(in,s);assert(!s.active);
 in.position.y=299;in.velocity.y=-60;fx.step(in,s);assert(!s.active);
 in.velocity.y=-61;fx.step(in,s);assert(s.active);
 fx.reset(0,true);s={};in.hasRainbowEntry=false;for(unsigned i=0;i<60;++i){in.position.y-=100;fx.step(in,s);}assert(!s.active);
 puts("PASS real KartItemEffects: local/CPU, 35-frame delay, strict 700/drop and -60 velocity gates, follow/one-shot, rescue stop, orientation, unrelated course");
}
