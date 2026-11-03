#include <mkw/led_audio_gate.hpp>
#include <cassert>
#include <iostream>
using namespace mkw::led;
int main(){
 Policy p;Input in;in.active=true;
 // Starting lights belong to Items; each countdown edge restarts the flash.
 in.level=1;in.startSignal=1;auto start=p.step(in);assert(start.mode==Mode::StartPink);
 in.startSignal=2;assert(p.step(in).epoch!=start.epoch);
 in.startSignal=3;assert(p.step(in).mode==Mode::StartPink);
 in.startSignal=4;assert(p.step(in).mode==Mode::StartBlue);
 in.burnout=true;assert(p.step(in).mode==Mode::Hit);
 for(unsigned n=0;n<90;++n)assert(p.step(in).mode!=Mode::StartBlue);
 auto pink=sample(Mode::StartPink,.045f),go=sample(Mode::StartBlue,.045f);
 assert(pink.r==200&&pink.g==0&&pink.b==80&&go.r==0&&go.g==135&&go.b==255);
 p.reset();in={};in.active=true;
 // Pure channels expose each pulse separately, including PTM interpolation.
 const auto two=characterPattern({255,0,0},{0,0,255});
 assert(two.r[2]==255&&two.b[18]==255&&two.r[31]==0&&two.b[31]==0);
 for(unsigned i=0;i<32;++i){assert(two.g[i]==0);if(i<=15)assert(two.b[i]==0);if(two.b[i])assert(two.r[i]<=26);}
 for(unsigned i=0;i<31;++i)for(unsigned j=0;j<=100;++j){
  const float red=two.r[i]+(float(two.r[i+1])-two.r[i])*j/100.f;
  const float blue=two.b[i]+(float(two.b[i+1])-two.b[i])*j/100.f;
  if(blue>0)assert(red<=26); // 8-bit rounding of 10% (25.5)
 }
 // A boost ends with its native timer, not the inventory or a free-running clock.
 in.mushroom=true;in.boost=90;auto s=p.step(in);assert(s.mode==Mode::Mushroom&&s.period==90);
 const auto epoch=s.epoch;for(int i=89;i>0;--i){in.boost=i;s=p.step(in);assert(s.epoch==epoch);}
 in.boost=0;assert(p.step(in).mode==Mode::Off);
 p.reset();in={};in.active=true;in.boost=60;s=p.step(in);assert(s.mode==Mode::Panel&&s.period==60);
 in.boost=0;assert(p.step(in).mode==Mode::Off);
 // Every accepted repeat refreshes the native timer, but rapid Golden use
 // must not restart its hardware pattern on every press.
 p.reset();in={};in.active=true;in.golden=true;in.mushroom=true;in.mushroomUse=true;in.boost=90;
 assert(p.step(in).mode==Mode::Mushroom);in.mushroomUse=false;
 for(int i=0;i<12;++i){--in.boost;p.step(in);}in.mushroomUse=true;in.boost=90;s=p.step(in);assert(s.mode==Mode::Golden);
 auto rapidEpoch=s.epoch;for(int i=0;i<120;++i){in.mushroomUse=i%12==0;in.boost=in.mushroomUse?90:in.boost-1;s=p.step(in);assert(s.mode==Mode::Golden&&s.epoch==rapidEpoch);}
 in.boost=0;in.mushroomUse=false;assert(p.step(in).mode==Mode::Off);
 // One Golden press stays a single full boost cycle, including after a normal mushroom.
 for(bool preceding:{false,true}){
  p.reset();in={};in.active=true;in.mushroom=true;in.boost=90;in.mushroomUse=true;
  if(preceding)assert(p.step(in).mode==Mode::Mushroom);
  in.golden=true;assert(p.step(in).mode==Mode::Mushroom);in.mushroomUse=false;
  for(unsigned n=89;n>0;--n){in.boost=n;assert(p.step(in).mode==Mode::Mushroom);}
 }
 // Canceled sparks do not flash; accepted native MT serial does.
 p.reset();in={};in.active=true;in.drift=2;assert(p.step(in).mode==Mode::DriftBlue);
 in.drift=0;assert(p.step(in).mode==Mode::Off);
 in.drift=3;p.step(in);in.drift=0;in.miniTurboSerial=1;assert(p.step(in).mode==Mode::ReleaseOrange);
 p.reset();in={};in.active=true;in.drift=2;p.step(in);in.drift=0;in.miniTurboSerial=1;assert(p.step(in).mode==Mode::ReleaseBlue);
 // Short foreground hit returns to underlying star, and electrical hit wins
 // a simultaneous generic action event, without relighting on the next tick.
 p.reset();in={};in.active=true;in.star=true;in.events=Hit|Electric|GreenHit;s=p.step(in);assert(s.mode==Mode::Electric);in.events=0;
 for(int i=0;i<89;++i)assert(p.step(in).mode==Mode::Electric);assert(p.step(in).mode==Mode::Star);
 // 600 ticks of a sustained state require one hardware upload, not 600.
 p.reset();in={};in.active=true;in.rainbow=true;auto old=p.step(in);unsigned uploads=1;
 for(int i=0;i<600;++i){s=p.step(in);if(s.mode!=old.mode||s.epoch!=old.epoch)++uploads;old=s;}assert(uploads==1);
 for(int i=0;i<1000;++i){auto c=sample(Mode::Rainbow,float(i)/1000);assert(c.r+c.g+c.b>100);}
 assert(sample(Mode::Mushroom,0).g==211);assert(sample(Mode::Mushroom,.25).b==189);assert(sample(Mode::Mushroom,.5).r==255&&sample(Mode::Mushroom,.5).g==0);assert(sample(Mode::Mushroom,.75).b==255);
 for(auto m:{Mode::Hit,Mode::Electric,Mode::GreenHit,Mode::BlueHit,Mode::BlueLaunch,Mode::Blooper,Mode::Pow,Mode::Mushroom,Mode::Panel,Mode::ReleaseBlue,Mode::ReleaseOrange}){auto pat=pattern({m,0,90,90},0);assert(pat.loopDelay==255&&pat.reserved==0&&pat.r[31]==0&&pat.g[31]==0&&pat.b[31]==0);}
 // Drift is below every item/event, even the intentionally dark flash half.
 for(unsigned kind=0;kind<7;++kind){
  p.reset();in={};in.active=true;in.drift=3;p.step(in);in.drift=0;in.miniTurboSerial=1;
  switch(kind){case 0:in.star=true;break;case 1:in.warning=true;break;case 2:in.cloud=true;break;case 3:in.rainbow=true;break;case 4:in.boost=60;break;case 5:in.events=BlueLaunch;break;case 6:in.mega=true;break;}
  assert(p.step(in).mode!=Mode::ReleaseOrange);
  for(unsigned n=0;n<35;++n)p.step(in);
  in.star=in.warning=in.cloud=in.rainbow=in.mega=false;in.boost=0;in.events=0;
  assert(p.step(in).mode!=Mode::ReleaseOrange);
 }
 p.reset();in={};in.active=true;in.level=2;in.drift=3;assert(p.step(in).mode==Mode::Off);
 in.miniTurboSerial=1;in.drift=0;assert(p.step(in).mode==Mode::Off);
 in.rainbow=true;assert(p.step(in).mode==Mode::Rainbow);
 in.level=1;in.boost=60;in.warning=true;assert(p.step(in).mode==Mode::Off);
 in.mushroom=true;in.boost=90;assert(p.step(in).mode==Mode::Mushroom);
 in.level=0;in.events=Electric;assert(p.step(in).mode==Mode::Off);
 for(unsigned brightness:{10u,50u,100u}){auto a=pattern({Mode::Rainbow,0,210,210},0),b=a;applyBrightness(b,brightness);
  assert(a.duration==b.duration&&a.smooth==b.smooth&&a.loopDelay==b.loopDelay);
  for(unsigned i=0;i<32;++i){assert(b.r[i]==(a.r[i]*brightness+50)/100);assert(b.g[i]==(a.g[i]*brightness+50)/100);assert(b.b[i]==(a.b[i]*brightness+50)/100);}}
 // Explicit priority order is stable even when all categories compete.
 p.reset();in={};in.active=true;in.rainbow=true;in.warning=true;in.star=true;in.drift=3;in.events=Electric|BlueLaunch|Hit;
 assert(p.step(in).mode==Mode::Electric);in.rainbow=false;in.events=0;assert(p.step(in).mode==Mode::Electric);
 in.warning=false;assert(p.step(in).mode==Mode::Electric);
 p.reset();in={};in.active=true;in.star=true;in.events=Hit;assert(p.step(in).mode==Mode::Hit);
 in.star=false;in.events=0;assert(p.step(in).mode==Mode::Hit);
 p.reset();in={};in.active=true;in.mushroom=true;in.boost=90;in.boostVisible=false;
 for(unsigned n=0;n<6;++n){assert(p.step(in).mode==Mode::Off);--in.boost;}
 in.boostVisible=true;s=p.step(in);assert(s.mode==Mode::Mushroom&&s.period==84&&s.offset==0);
 in.boost=0;assert(p.step(in).mode==Mode::Off);
 // Warning-to-impact is a complete one-shot; unrelated remaining warnings
 // resume afterwards, never replaying the expired hit.
 for(auto event:{Hit,GreenHit,BlueHit,Electric}){
  p.reset();in={};in.active=true;in.warning=true;in.warningLit=true;
  assert(p.step(in).mode==Mode::WarningOn);in.events=event;
  const Mode expected=event==Electric?Mode::Electric:event==BlueHit?Mode::BlueHit:event==GreenHit?Mode::GreenHit:Mode::Hit;
  assert(p.step(in).mode==expected);in.events=0;
  for(unsigned n=0;n<89;++n){in.warningLit=!in.warningLit;assert(p.step(in).mode==expected);}
  assert(p.step(in).mode==(in.warningLit?Mode::WarningOn:Mode::WarningDark));
 }
 // All impact types override continuous effects, retain their color channels,
 // and return to the underlying rainbow only after their complete fade.
 for(auto event:{Hit,GreenHit,BlueHit,Electric}){
  p.reset();in={};in.active=true;in.rainbow=in.star=in.warning=true;in.events=event;
  const Mode expected=event==Electric?Mode::Electric:event==BlueHit?Mode::BlueHit:event==GreenHit?Mode::GreenHit:Mode::Hit;
  assert(p.step(in).mode==expected);in.events=0;
  for(unsigned n=0;n<89;++n)assert(p.step(in).mode==expected);
  assert(p.step(in).mode==Mode::Rainbow);
 }
 for(unsigned n=0;n<100;++n){auto r=sample(Mode::Hit,n/100.f),g=sample(Mode::GreenHit,n/100.f);assert(r.g==0&&r.b==0&&g.r==0&&g.b==0);}
 assert(sample(Mode::Hit,.045f).r==255&&sample(Mode::GreenHit,.045f).g==255);
 // The extra impact offset is exactly three 60 Hz game ticks, after the
 // audio gate, with colors unchanged and no delay to other event classes.
 for(auto event:{Hit,Electric,GreenHit,BlueHit}){
  ImpactDelay delay;Input d{};d.active=true;d.events=event|BlueLaunch;
  auto o=delay.step(d);assert(o.events==BlueLaunch);d.events=0;
  assert(delay.step(d).events==0);assert(delay.step(d).events==0);
  assert(delay.step(d).events==event);assert(delay.step(d).events==0);
 }
 {ImpactDelay delay;Input d{};d.active=true;d.burnout=true;d.startSignal=4;
  for(unsigned n=0;n<3;++n){auto o=delay.step(d);assert(!o.burnout&&o.startSignal==3);}
  auto o=delay.step(d);assert(o.burnout&&o.startSignal==4);
  d.active=false;delay.step(d);d.active=true;d.burnout=false;assert(!delay.step(d).burnout);
 }
 // Sound-triggered presentation waits for a matching committed cue, not
 // any other racer's or unrelated sound. Sustained timers are not extended.
 assert(Blue.r==0&&Blue.g==0&&Blue.b==255);
 auto pureLaunch=sample(Mode::BlueLaunch,.045f);assert(pureLaunch.r==0&&pureLaunch.g==0&&pureLaunch.b>0);
 auto whiteHit=sample(Mode::BlueHit,.45f);assert(whiteHit.r==whiteHit.g&&whiteHit.g==whiteHit.b);
 AudioGate gate;in={};in.active=true;in.events=GreenHit;in.warning=true;
 assert(gate.step(in,0,true).events==0);in.events=0;
 assert(gate.step(in,mkw::ledAudio::Boost,true).events==0);
 assert(gate.step(in,mkw::ledAudio::Hit,true).events==GreenHit);
 assert(gate.step(in,0,true).events==0);
 gate.reset();in.events=Hit;assert(gate.step(in,0,true).events==0);in.events=0;
 for(unsigned n=0;n<28;++n)assert(gate.step(in,0,true).events==0);
 assert(gate.step(in,0,true).events==Hit); // suppressed voice cannot swallow hit
 gate.reset();in={};in.active=true;in.mushroom=true;in.boost=90;in.mushroomUse=true;
 auto gated=gate.step(in,0,true);assert(!gated.boostVisible&&!gated.mushroomUse&&gated.boost==90);
 in.mushroomUse=false;in.boost=89;gated=gate.step(in,mkw::ledAudio::Boost,true);
 assert(gated.boostVisible&&gated.mushroomUse&&gated.boost==89);
 in.boost=88;assert(!gate.step(in,0,true).mushroomUse);
 in.mushroomUse=true;in.boost=90;gated=gate.step(in,0,true);
 assert(gated.boostVisible&&gated.mushroomUse); // no new Dash note on rapid reuse
 // Activation at tick zero lights at tick three, regardless of audio timing.
 for(unsigned cueAt:{0u,1u,5u,40u})for(bool available:{false,true}){
  AudioGate audio;MushroomDelay delay;Policy policy;
  for(unsigned tick=0;tick<45;++tick){
   Input raw{};raw.active=true;raw.mushroom=true;raw.boost=90-tick;raw.mushroomUse=tick==0;
   auto gated=audio.step(raw,tick==cueAt?mkw::ledAudio::Boost:0u,available);
   auto out=delay.step(gated,raw.mushroomUse,raw.mushroom,raw.boost);
   assert(out.boostVisible==(tick>=3));assert(out.mushroomUse==(tick==3));
   assert((policy.step(out).mode==Mode::Mushroom)==(tick>=3));
  }
 }
 {MushroomDelay delay;Input raw{};raw.active=raw.mushroom=true;
  for(unsigned tick=0;tick<30;++tick){raw.boost=tick<12?90-tick:102-tick;
   auto out=delay.step(raw,tick==0||tick==12,true,raw.boost);
   assert(out.mushroomUse==(tick==3||tick==15));
   if(tick==12)assert(out.boost==78);if(tick==15)assert(out.boost==87);
  }
 }
 gate.reset();in={};in.active=true;in.star=in.mega=in.bullet=true;
 gated=gate.step(in,0,true);assert(!gated.star&&!gated.mega&&!gated.bullet);
 gated=gate.step(in,mkw::ledAudio::Star|mkw::ledAudio::Mega|mkw::ledAudio::Bullet,true);assert(gated.star&&gated.mega&&gated.bullet);
 in.active=false;gate.step(in,0,true);in.active=true;assert(!gate.step(in,0,true).star);
 in.events=Hit;assert(gate.step(in,0,false).events==Hit); // silent backend
 for(auto mode:{Mode::Mega,Mode::Bullet}){auto grey=sample(mode,2.f/3);assert(grey.r==128&&grey.g==128&&grey.b==128);}
 // Interrupted loops and short flash gaps start at the actual uploaded RGB,
 // decline monotonically, and finish black without further game updates.
 for(auto mode:{Mode::Star,Mode::Golden,Mode::DriftBlue,Mode::Rainbow,Mode::Cloud,Mode::WarningOn}){
  auto active=pattern({mode,0,90,90},0);applyBrightness(active,40);
  for(unsigned age:{0u,83u,917u,1890u})for(bool gap:{false,true}){
   const auto color=patternColor(active,age);const auto fade=fadeToBlack(color,gap);
   assert(fade.r[0]==unsigned(std::lround(color.r))&&fade.g[0]==unsigned(std::lround(color.g))&&fade.b[0]==unsigned(std::lround(color.b)));
   for(unsigned n=1;n<32;++n)assert(fade.r[n]<=fade.r[n-1]&&fade.g[n]<=fade.g[n-1]&&fade.b[n]<=fade.b[n-1]);
   const auto end=patternColor(fade,1000);assert(end.r==0&&end.g==0&&end.b==0);
  }
 }
 in.active=false;assert(p.step(in).mode==Mode::Off);
 std::cout<<"PASS native boost termination, rapid Golden continuity, drift release/cancel, priorities, steady-state upload budget, rainbow and finite endings\n";
}
