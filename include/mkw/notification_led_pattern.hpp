#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace mkw::led {
// PTM SetInfoLEDPattern, 0x8010640: four control bytes and 32 RGB samples.
// Protocol reference: PabloMK7/ctr-led-brary. Patterns below are our approved
// preview values, NOT a claim of exact CTGP-7 timing.
struct Pattern {uint8_t duration=0,smooth=0,loopDelay=255,reserved=0;uint8_t r[32]{},g[32]{},b[32]{};};
static_assert(sizeof(Pattern)==100);
struct Color {float r,g,b;};
constexpr Color Black{0,0,0},White{255,255,255},Red{255,36,36},Yellow{255,211,0},Green{36,255,68},Blue{0,0,255},Cyan{69,234,255},Orange{255,174,50},Warning{255,69,10};
constexpr Color BoostColors[]={Yellow,{255,0,189},{255,0,0},{152,0,255}};
constexpr Color RainbowColors[]={Red,{255,133,0},{255,224,0},{56,238,75},{32,223,255},{40,85,255},{184,56,255}};
enum class Mode:uint8_t {Off,Hit,Electric,GreenHit,BlueHit,BlueLaunch,Blooper,Pow,Cloud,CloudDark,WarningOn,WarningDark,Mushroom,Panel,Golden,Star,Mega,Bullet,DriftBlue,DriftOrange,ReleaseBlue,ReleaseOrange,Rainbow,StartPink,StartBlue};
inline Color mix(Color a,Color b,float t){return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};}
inline Color dim(Color c,float t){return {c.r*t,c.g*t,c.b*t};}
inline float ease(float t){return t*t*(3-2*t);}
inline float fade(float t){if(t<0||t>=1)return 0;return t<.045f?t/.045f:std::pow(1-(t-.045f)/.955f,1.55f);}
template<size_t N> inline Color roll(const Color(&colors)[N],float phase){float p=(phase-std::floor(phase))*N;unsigned n=unsigned(p);return mix(colors[n],colors[(n+1)%N],ease(p-n));}
inline bool looping(Mode m){return m==Mode::Golden||m==Mode::Star||m==Mode::Mega||m==Mode::Bullet||m==Mode::DriftBlue||m==Mode::DriftOrange||m==Mode::Rainbow;}
inline Color sample(Mode m,float phase){
 const Color warm[]={Yellow,Red,{128,128,128}},blue[]={Cyan,White},orange[]={Orange,White};
 switch(m){
 case Mode::Hit:return dim(Color{255,0,0},fade(phase));case Mode::Electric:return dim(Yellow,fade(phase));case Mode::GreenHit:return dim(Color{0,255,0},fade(phase));
 case Mode::BlueHit:return dim(mix(Blue,White,ease(std::clamp(phase<.45f?phase/.45f:(1-phase)/.55f,0.f,1.f))),fade(phase));
 case Mode::BlueLaunch:case Mode::Blooper:return dim(m==Mode::BlueLaunch?Blue:White,fade(phase<.5f?phase/.38f:(phase-.5f)/.38f));
 case Mode::Pow:return dim(Warning,fade(phase));
 // Original jugemu_signal CLR0 lightb1 C2 and hg_signal_start C2.
 case Mode::StartPink:return dim(Color{200,0,80},fade(phase));
 case Mode::StartBlue:return dim(Color{0,135,255},fade(phase));
 case Mode::Cloud:return Yellow;case Mode::WarningOn:return Warning;
 case Mode::Mushroom:case Mode::Panel:case Mode::Star:return roll(BoostColors,phase);
 case Mode::Golden:return dim(roll(BoostColors,phase),.5f+.5f*std::cos(phase*6*6.283185307f)); // 6 pulses / 1.5s = 4Hz
 case Mode::Mega:case Mode::Bullet:return roll(warm,phase);
 case Mode::DriftBlue:return roll(blue,phase);case Mode::DriftOrange:return roll(orange,phase);
 case Mode::ReleaseBlue:return dim(Cyan,fade(phase));case Mode::ReleaseOrange:return dim(Orange,fade(phase));
 case Mode::Rainbow:return roll(RainbowColors,phase);
 default:return Black;
 }
}
struct Selection {Mode mode=Mode::Off;uint32_t epoch=0;unsigned period=90,remaining=90,offset=0;};
inline Pattern pattern(const Selection& s,unsigned age){
 Pattern p{};const bool loop=looping(s.mode),constant=s.mode==Mode::Cloud||s.mode==Mode::WarningOn;
 unsigned ticks=loop?s.period:std::max(1u,s.remaining);
 p.duration=uint8_t(std::clamp(std::lround(ticks*16.f/60.f),1l,255l));
 p.smooth=1;p.loopDelay=loop||constant?0:255;
 for(unsigned i=0;i<32;++i){
  const float phase=(age+s.offset+ticks*(float(i)/(loop?32.f:31.f)))/std::max(1u,s.period);
  Color c=sample(s.mode,phase);
  if(s.mode==Mode::Mushroom||s.mode==Mode::Panel)c=dim(c,1.f-ease(std::clamp((phase-.8f)/.2f,0.f,1.f)));
  if(!loop&&!constant&&i==31)c=Black; // one-shot never leaves a lit final sample
  auto byte=[](float f){return uint8_t(std::clamp(std::lround(f),0l,255l));};
  p.r[i]=byte(c.r);p.g[i]=byte(c.g);p.b[i]=byte(c.b);
 }
 return p;
}
// Sample the pattern most recently handed to PTM, including its brightness,
// so an interrupted loop fades from its current color instead of restarting.
inline Color patternColor(const Pattern& p,std::uint64_t elapsedMs){
 if(!p.duration)return {float(p.r[31]),float(p.g[31]),float(p.b[31])};
 const double duration=p.duration*62.5;
 double position=double(elapsedMs)/duration;
 if(p.loopDelay==0)position-=std::floor(position);else position=std::min(position,1.0);
 const double scaled=position*(p.loopDelay==0?32.0:31.0);const unsigned i=std::min(31u,unsigned(scaled));
 const unsigned j=i==31?(p.loopDelay==0?0:31):i+1;
 Color a{float(p.r[i]),float(p.g[i]),float(p.b[i])},b{float(p.r[j]),float(p.g[j]),float(p.b[j])};
 return p.smooth?mix(a,b,float(scaled-i)):a;
}
inline Pattern fadeToBlack(Color start,bool flashGap=false){
 Pattern p{};p.duration=flashGap?2:4;p.smooth=1;p.loopDelay=255;
 for(unsigned i=0;i<32;++i){const auto c=dim(start,1.f-ease(float(i)/31));
  p.r[i]=uint8_t(std::clamp(std::lround(c.r),0l,255l));p.g[i]=uint8_t(std::clamp(std::lround(c.g),0l,255l));p.b[i]=uint8_t(std::clamp(std::lround(c.b),0l,255l));}
 return p;
}
// Two distinct flashes: color two may rise only after color one reaches
// 10%. The shared boundary sample prevents PTM interpolation starting early.
inline Pattern characterPattern(Color first,Color second){
 Pattern p{};p.duration=24;p.smooth=1;p.loopDelay=255;
 for(unsigned i=0;i<32;++i){
  const float a=i<2?float(i)/2.f:i<=15?1.f-.9f*ease(float(i-2)/13.f):i<18?.1f*(18-i)/3.f:0.f;
  const float b=i<=15?0.f:i<18?ease(float(i-15)/3.f):1.f-ease(float(i-18)/13.f);
  const Color c{first.r*a+second.r*b,first.g*a+second.g*b,first.b*a+second.b*b};
  p.r[i]=uint8_t(std::clamp(std::lround(c.r),0l,255l));p.g[i]=uint8_t(std::clamp(std::lround(c.g),0l,255l));p.b[i]=uint8_t(std::clamp(std::lround(c.b),0l,255l));
 }
 return p;
}
inline void applyBrightness(Pattern& p,unsigned percent){
 percent=std::min(percent,100u);
 for(unsigned i=0;i<32;++i){p.r[i]=(unsigned(p.r[i])*percent+50)/100;p.g[i]=(unsigned(p.g[i])*percent+50)/100;p.b[i]=(unsigned(p.b[i])*percent+50)/100;}
}
// Presentation only. No allocation, collision query, asset load or sound work.
struct Input {
 bool active=false,star=false,mega=false,bullet=false,golden=false,cloud=false,cloudLit=false,warning=false,warningLit=false,rainbow=false;
 unsigned startSignal=0;bool burnout=false;
 bool mushroomUse=false,mushroom=false,boostVisible=true;unsigned boost=0,ramp=0,miniTurboSerial=0,drift=0;
 uint32_t events=0;unsigned level=3,brightness=100; // Off / Items / Items + Events / ... + Drifts
};
enum Event:uint32_t {Hit=1,Electric=2,GreenHit=4,BlueHit=8,BlueLaunch=16,Blooper=32,Pow=64};
class Policy {
public:
 Selection step(Input in){
  if(!in.active||!in.level){reset();return {};}
  if(in.level!=level_){reset();level_=in.level;}
  if(in.level<2){in.warning=false;in.rainbow=false;in.ramp=0;if(!in.mushroom)in.boost=0;}
  if(in.level<3)in.drift=0;
  ++tick_;
  auto pulse=[&](Mode mode,unsigned duration,unsigned priority){if(tick_>=until_||priority>=priority_){pulse_=mode;start_=tick_;until_=tick_+duration;priority_=priority;}};
  // Drift release requires a newly accepted MT boost, not simply lost sparks.
  if(in.level>=3&&previousDrift_>=2&&in.miniTurboSerial!=previousMt_){
   release_=previousDrift_>=3?Mode::ReleaseOrange:Mode::ReleaseBlue;releaseStart_=tick_;releaseUntil_=tick_+30;
  }
  previousDrift_=in.drift;previousMt_=in.miniTurboSerial;
  // Every accepted impact owns its flash/fade, including terrain hazards.
  // Continuous effects and remaining warnings resume after the one-shot.
  const unsigned hits=in.events&(Hit|GreenHit|BlueHit|Electric);
  if(hits){
   impact_=hits&Electric?Mode::Electric:hits&BlueHit?Mode::BlueHit:hits&GreenHit?Mode::GreenHit:Mode::Hit;
   impactStart_=tick_;impactUntil_=tick_+90;
  }
  if(in.startSignal!=previousSignal_){
   if(in.startSignal)pulse(in.startSignal<4?Mode::StartPink:in.burnout?Mode::Hit:Mode::StartBlue,60,96);
   previousSignal_=in.startSignal;
  }
  if(in.burnout&&!previousBurnout_){impact_=Mode::Hit;impactStart_=tick_;impactUntil_=tick_+90;}
  previousBurnout_=in.burnout;
  if(in.events&Pow)pulse(Mode::Pow,21,60);
  if(in.events&Blooper)pulse(Mode::Blooper,90,65);
  if(in.events&BlueLaunch)pulse(Mode::BlueLaunch,90,70);
  if(in.events&Hit)pulse(Mode::Hit,90,40);
  if(in.events&GreenHit)pulse(Mode::GreenHit,90,45);
  if(in.events&BlueHit)pulse(Mode::BlueHit,90,50);
  if(in.events&Electric)pulse(Mode::Electric,90,95);
  // Only consecutive Golden activations count as rapid use; a preceding
  // normal mushroom must not turn a single Golden press into a flashing loop.
  if(!in.golden){rapid_=false;lastUse_=0;}
  if(in.mushroomUse&&in.golden){
   if(lastUse_&&tick_-lastUse_<=30)rapid_=true;
   lastUse_=tick_;
  }
  if(!in.boost&&!in.ramp){rapid_=false;lastUse_=0;}
  if(!in.golden)rapid_=false;
  const unsigned boost=std::max(in.boost,in.ramp);
  if(boost&&(!previousBoost_||boost>previousBoost_||in.mushroomUse)&&!rapid_){boostPeriod_=std::max(boost,in.mushroom?90u:60u);boostStart_=tick_;boostAge_=boostPeriod_-boost;}
  if(rapid_&&!wasRapid_)rapidStart_=tick_;
  wasRapid_=rapid_;previousBoost_=boost;
  const bool boostShown=boost&&in.boostVisible;
  if(boostShown&&!boostVisible_){boostPeriod_=boost;boostStart_=tick_;boostAge_=0;}
  boostVisible_=boostShown;
  auto foreground=[&](){return Selection{pulse_,start_,until_-start_,until_-tick_};};
  Mode mode=Mode::Off;unsigned period=90;uint32_t epoch=0;
  // User ordering: rainbow > warning > electricity > items > hits > drifts.
  if(tick_<impactUntil_)return {impact_,impactStart_,90,impactUntil_-tick_};
  else if(in.rainbow){mode=Mode::Rainbow;period=210;}
  else if(in.warning){mode=in.warningLit?Mode::WarningOn:Mode::WarningDark;}
  else if(tick_<until_&&pulse_==Mode::Electric)return foreground();
  else if(tick_<until_&&priority_>=60)return foreground();
  else if(in.cloud){mode=in.cloudLit?Mode::Cloud:Mode::CloudDark;}
  else if(in.bullet)mode=Mode::Bullet;
  else if(in.mega)mode=Mode::Mega;
  else if(in.star){mode=Mode::Star;period=120;}
  else if(boostShown){mode=rapid_?Mode::Golden:boostPeriod_>60?Mode::Mushroom:Mode::Panel;period=rapid_?90:boostPeriod_;epoch=rapid_?rapidStart_:boostStart_;}
  else if(tick_<until_)return foreground();
  // Both drift charging and release yield to every item/event, including
  // the dark half of warning/cloud flashes. Expired releases are never queued.
  else if(tick_<releaseUntil_)return {release_,releaseStart_,30,releaseUntil_-tick_};
  else if(in.drift>=2){mode=in.drift>=3?Mode::DriftOrange:Mode::DriftBlue;period=66;}
  if(mode!=background_){background_=mode;backgroundStart_=tick_;}
  if(!epoch)epoch=backgroundStart_;
  return {mode,epoch,period,boost?boost:period,(mode==Mode::Mushroom||mode==Mode::Panel)?boostAge_:0};
 }
 unsigned age(const Selection& s)const{return tick_>=s.epoch?tick_-s.epoch:0;}
 void reset(){*this=Policy{};}
private:
 uint32_t tick_=0,start_=0,until_=0,lastUse_=0,boostStart_=0,rapidStart_=0,backgroundStart_=0;
 uint32_t impactStart_=0,impactUntil_=0;Mode impact_=Mode::Off;
 unsigned level_=3;uint32_t releaseStart_=0,releaseUntil_=0;Mode release_=Mode::Off;
 unsigned priority_=0,previousDrift_=0,previousMt_=0,previousBoost_=0,boostPeriod_=90,boostAge_=0;
 unsigned previousSignal_=0;bool previousBurnout_=false;
 bool boostVisible_=false,rapid_=false,wasRapid_=false;Mode pulse_=Mode::Off,background_=Mode::Off;
};
}
