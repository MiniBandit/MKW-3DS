#include <mkw/notification_led.hpp>
#include <mkw/race_items.hpp>
#include <mkw/sound_system.hpp>
#include <cstring>
#include <mkw/led_kart_colors.hpp>
namespace mkw {
NotificationLed::NotificationLed(){aptHook(&hook_,hook,this);}
NotificationLed::~NotificationLed(){clear();aptUnhook(&hook_);if(handle_)svcCloseHandle(handle_);}
bool NotificationLed::send(const led::Pattern& p){
 if(failed_)return false;
 if(!handle_&&R_FAILED(srvGetServiceHandle(&handle_,"ptm:sysm"))){failed_=true;return false;}
 u32* ipc=getThreadCommandBuffer();ipc[0]=0x8010640;std::memcpy(ipc+1,&p,sizeof(p));
 Result r=svcSendSyncRequest(handle_);if(R_SUCCEEDED(r))r=Result(ipc[1]);
 if(R_FAILED(r)){failed_=true;return false;}uploaded_=p;uploadedAt_=osGetTime();return true;
}
void NotificationLed::characterSelected(unsigned driver,unsigned level,unsigned brightness){
 if(level<2||driver>=led::StandardKartColors.size()||failed_||suspended_)return;
 const auto colors=led::StandardKartColors[driver];
 auto color=[](uint32_t c){return led::Color{float((c>>16)&255),float((c>>8)&255),float(c&255)};};
 auto p=led::characterPattern(color(colors[0]),color(colors[1]));
 led::applyBrightness(p,brightness);if(send(p))owned_=true;
}
void NotificationLed::clear(){
 // Never touch notifications if this game has not taken ownership of the lamp.
 if(owned_){send(led::fadeToBlack(led::patternColor(uploaded_,osGetTime()-uploadedAt_)));owned_=false;}
 policy_.reset();audioGate_.reset();impactDelay_.reset();mushroomDelay_.reset();haveSelection_=false;haveAction_=false;haveMushroomUses_=false;
}
void NotificationLed::hook(APT_HookType type,void* ptr){
 auto& self=*static_cast<NotificationLed*>(ptr);
 if(type==APTHOOK_ONSUSPEND||type==APTHOOK_ONSLEEP||type==APTHOOK_ONEXIT){self.clear();self.suspended_=true;}
 else if(type==APTHOOK_ONRESTORE||type==APTHOOK_ONWAKEUP){self.suspended_=false;}
}
void NotificationLed::step(led::Input in,const RaceItems& items,unsigned actionCount,int action,unsigned /*attacker*/){
 if(suspended_||failed_)return;
 auto& audio=SoundSystem::instance();
 const auto cues=audio.takeLedCues();
 if(!in.active||!in.level){clear();return;}
 if(haveAction_&&actionCount_!=actionCount){
  if(action==KartAction::Lightning||action==KartAction::CloudStrike||action==KartAction::SpinShrink)in.events|=led::Electric;
  else if(action>=0&&action<KartAction::Count)in.events|=led::Hit;
 }
 actionCount_=actionCount;haveAction_=true;
 for(const auto& event:items.events()){
  if(event.effect==ItemEffect::BlueLaunch)in.events|=led::BlueLaunch;
  if(event.effect==ItemEffect::BlooperLaunch)in.events|=led::Blooper;
  if(event.player!=0)continue;
  if(event.effect==ItemEffect::ElectricHit)in.events|=led::Electric;
  if(event.effect==ItemEffect::PowStomp)in.events|=led::Pow;

  if(event.action>=0&&(event.effect==ItemEffect::Spin||event.effect==ItemEffect::Launch||event.effect==ItemEffect::Explosion||event.effect==ItemEffect::Crush||event.effect==ItemEffect::Knockback))
   in.events|=event.item==ItemKind::GreenShell?led::GreenHit:event.item==ItemKind::BlueShell?led::BlueHit:led::Hit;
 }
 for(const auto& sound:items.sounds()){
  if(sound.cue==ItemSoundCue::Pow)in.events|=led::Pow;
 }
 const unsigned uses=items.mushroomUses(0);
 in.mushroomUse=haveMushroomUses_&&uses!=mushroomUses_;
 mushroomUses_=uses;haveMushroomUses_=true;
 const bool mushroomUse=in.mushroomUse,mushroomActive=in.mushroom;const unsigned remaining=in.boost;
 in=impactDelay_.step(audioGate_.step(in,cues,audio.available()));
 in=mushroomDelay_.step(in,mushroomUse,mushroomActive,remaining);
 const auto next=policy_.step(in);
 // Continuous patterns run entirely in PTM. IPC only on state/edge transitions;
 // not once per frame, not from the renderer or audio threads.
 if(haveSelection_&&next.mode==selection_.mode&&next.epoch==selection_.epoch)return;
 selection_=next;haveSelection_=true;
 if(next.mode==led::Mode::Off&&!owned_)return;
  const bool dark=next.mode==led::Mode::Off||next.mode==led::Mode::CloudDark||next.mode==led::Mode::WarningDark;
 auto p=dark?led::fadeToBlack(led::patternColor(uploaded_,osGetTime()-uploadedAt_),next.mode!=led::Mode::Off):led::pattern(next,policy_.age(next));
 if(!dark)led::applyBrightness(p,in.brightness);
 if(send(p))owned_=next.mode!=led::Mode::Off;
}
}
