#pragma once
#include <mkw/notification_led_pattern.hpp>
#include <mkw/led_audio_cues.hpp>
namespace mkw::led {
// Three game ticks after the existing audio gate; do not delay sound itself.
class ImpactDelay {
public:
 Input step(Input in){
  if(!in.active||!in.level){reset();return in;}
  constexpr unsigned mask=Hit|Electric|GreenHit|BlueHit;
  const auto old=frames_[cursor_];frames_[cursor_]={in.events&mask,in.burnout};cursor_=(cursor_+1)%3;
  in.events=(in.events&~mask)|old.events;
  // A failed start must not briefly display the successful blue signal.
  if(in.startSignal==4&&in.burnout&&!old.burnout)in.startSignal=3;
  in.burnout=old.burnout;return in;
 }
 void reset(){*this=ImpactDelay{};}
private:
 struct Frame{unsigned events=0;bool burnout=false;};Frame frames_[3]{};unsigned cursor_=0;
};
// Accepted mushroom activation, delayed exactly three simulation ticks.
// Independent of audio backend, delayed notes, and character voice playback.
class MushroomDelay {
public:
 Input step(Input in,bool used,bool active,unsigned remaining){
  if(!in.active||!in.level||!active||!remaining){reset();return in;}
  const bool due=frames_[cursor_];frames_[cursor_]=used;cursor_=(cursor_+1)%3;
  if(due){ready_=true;remaining_=remaining;}
  else if(ready_&&remaining_)--remaining_;
  in.mushroomUse=due;
  in.boost=ready_?std::min(remaining,remaining_):0;
  in.boostVisible=ready_;
  return in;
 }
 void reset(){*this=MushroomDelay{};}
private:
 bool frames_[3]{},ready_=false;unsigned cursor_=0,remaining_=0;
};
// Audio acknowledgements arrive after actual channel starts and DSP commit.
// This tiny presentation gate never blocks the game or changes sound playback.
class AudioGate {
public:
 Input step(Input in,uint32_t cues,bool audioAvailable){
  if(!in.active||!in.level){reset();return in;}
  if(!audioAvailable){reset();return in;}
  for(unsigned i=0;i<8;++i){if(credits_[i])--credits_[i];if(cues&(1u<<i))credits_[i]=4;}
  const unsigned hits=in.events&(Hit|Electric|GreenHit|BlueHit);
  if(hits){pending_[0]=hits;wait_[0]=0;}
  for(unsigned i=1;i<4;++i){unsigned event=i==1?BlueLaunch:i==2?Blooper:Pow;if(in.events&event){pending_[i]=event;wait_[i]=0;}}
  in.events=0;
  for(unsigned i=0;i<4;++i)if(pending_[i]){
   const unsigned c=i==0?0:i+4;
   // Missing/suppressed audio must not permanently swallow accepted damage.
   if(credits_[c]||++wait_[i]>=30){in.events|=pending_[i];pending_[i]=0;credits_[c]=0;}
  }
  // Retail reuses Dash during its first 48 sequence ticks. Repeated use
  // must preserve an already audible boost and its LED phase.
  // Generic audio gate for panels and power-ups. NotificationLed applies
  // MushroomDelay afterwards, overriding mushroom timing with accepted use+3.
  if(in.mushroomUse)mushroomPending_=true;
  const bool active[]={bool(in.boost||in.ramp),in.star,in.mega,in.bullet};
  for(unsigned i=0;i<4;++i){
   if(!active[i]){ready_[i]=false;stateWait_[i]=0;continue;}
   if(!ready_[i]&&(credits_[i+1]||++stateWait_[i]>=30)){ready_[i]=true;credits_[i+1]=0;}
  }
  in.mushroomUse=mushroomPending_&&ready_[0];
  if(in.mushroomUse||!active[0])mushroomPending_=false;
  in.boostVisible=ready_[0]; // audio replaces the old FOV-derived start gate
  in.star&=ready_[1];in.mega&=ready_[2];in.bullet&=ready_[3];
  return in;
 }
 void reset(){*this=AudioGate{};}
private:
 unsigned credits_[8]{},pending_[4]{},wait_[4]{},stateWait_[4]{};
 bool ready_[4]{},mushroomPending_=false;
};
}
