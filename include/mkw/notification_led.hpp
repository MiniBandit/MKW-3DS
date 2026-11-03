#pragma once
#include <3ds.h>
#include <mkw/led_audio_gate.hpp>
namespace mkw {
class RaceItems;
class NotificationLed {
public:
 NotificationLed();~NotificationLed();
 void step(led::Input input,const RaceItems&,unsigned actionCount,int action,unsigned attacker);
 void clear();
 void characterSelected(unsigned driver,unsigned level,unsigned brightness);
 NotificationLed(const NotificationLed&)=delete;NotificationLed& operator=(const NotificationLed&)=delete;
private:
 static void hook(APT_HookType,void*);
 bool send(const led::Pattern&);
 Handle handle_=0;aptHookCookie hook_{};
 bool failed_=false,owned_=false,suspended_=false,haveSelection_=false,haveAction_=false;
 led::Pattern uploaded_{};std::uint64_t uploadedAt_=0;
 unsigned mushroomUses_=0;bool haveMushroomUses_=false;
 unsigned actionCount_=0;led::Selection selection_{};led::Policy policy_;led::AudioGate audioGate_;led::ImpactDelay impactDelay_;led::MushroomDelay mushroomDelay_;
};
}
