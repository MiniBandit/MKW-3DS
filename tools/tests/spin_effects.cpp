// Original CreateSpinEffect and finite-spin update, RMCU 806a2370 / 8069bfa0.
// These exercise actual controller behavior, not renderer visibility.
#include <mkw/kart_item_effects.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
struct Sink : KartFxSink {
 bool live[KartFxCount]{};unsigned created[KartFxCount]{},moved[KartFxCount]{};EffectFrame frame[KartFxCount]{};
 bool alive(KartFx f)const override{return live[unsigned(f)];}
 void create(KartFx f,const EffectFrame& m)override{auto i=unsigned(f);if(!live[i]){live[i]=true;++created[i];frame[i]=m;}}
 void setFrame(KartFx f,const EffectFrame& m)override{++moved[unsigned(f)];frame[unsigned(f)]=m;}
 void followFade(KartFx)override{}
 void kill(KartFx f)override{live[unsigned(f)]=false;}
 void setColor(KartFx,const uint8_t*)override{}
};
int main(){
 for(bool local:{false,true})for(bool left:{false,true})for(unsigned rotations:{1u,2u,4u}){
  KartItemEffects fx;fx.reset(local?0:1,local);Sink sink;KartFxInput in;
  in.spinSerial=1;in.spinRotations=rotations;in.spinLeft=left;in.position={10,20,30};
  const auto f=left?(rotations==1?KartFx::SpinHalfL:KartFx::SpinL):(rotations==1?KartFx::SpinHalfR:KartFx::SpinR);
  fx.step(in,sink);assert(sink.created[unsigned(f)]==1&&sink.alive(f));
  in.position.x=11;in.action=-1;in.spinRotations=0;fx.step(in,sink);
  assert(sink.alive(f)&&sink.frame[unsigned(f)].origin.x==11); // finite particles outlive action
  sink.kill(f);fx.step(in,sink);assert(!sink.alive(f)); // no per-frame restart
  in.spinSerial=2;in.spinRotations=rotations;fx.step(in,sink);assert(sink.created[unsigned(f)]==2);
  in.lightningBolt=true;fx.step(in,sink);assert(!sink.alive(f));
 }
 for(unsigned gate=0;gate<3;++gate){
  KartItemEffects fx;fx.reset(0,true,gate==0);Sink sink;KartFxInput in;
  in.spinSerial=1;in.spinRotations=1;in.spinLeft=true;in.beforeRespawn=gate==1;
  fx.step(in,sink,gate==2);assert(!sink.alive(KartFx::SpinHalfL));
  // Suppressed start events do not get replayed after leaving LOD/respawn.
  in.beforeRespawn=false;fx.step(in,sink);assert(!sink.alive(KartFx::SpinHalfL));
 }
 KartItemEffects fx;fx.reset(0,true);Sink sink;KartFxInput in;
 in.spinSerial=1;in.spinRotations=1;fx.step(in,sink);assert(sink.alive(KartFx::SpinHalfR));
 in.powPending=true;fx.step(in,sink);assert(!sink.alive(KartFx::SpinHalfR));
 puts("PASS 12 source direction/count/local variants, finite follow/retrigger, lightning/POW replacement, ghost/respawn/LOD suppression");
}
