#pragma once
#include <mkw/gp_rank.hpp>
#include <mkw/kart_wall_contact.hpp>

namespace mkw {
// A race-local accumulator. Tick only from the physics loop, never from
// rendering/audio, and retain its frozen snapshot until the next race starts.
class GpTelemetry {
public:
 void reset(bool automatic,unsigned miniTurboSerial=0,unsigned offroadSerial=0,unsigned objectSerial=0){data_={};data_.automaticDrift=automatic;finished_=false;wallFrames_=0;miniTurboBase_=miniTurboSerial;offroadBase_=offroadSerial;objectBase_=objectSerial;}
 void step(bool racing,bool finished,bool leading,bool respawnStarted,bool rocketStart,unsigned acceptedItemHits,unsigned miniTurboSerial,unsigned offroadSerial=0,unsigned objectSerial=0,unsigned aiBonus=0){
  if(!racing||finished_)return;
  ++data_.frames;if(leading)++data_.firstPlaceFrames;
  if(respawnStarted)++data_.respawns;
  data_.rocketStart|=rocketStart;
  data_.itemHits=acceptedItemHits;
  data_.miniTurbos=(miniTurboSerial-miniTurboBase_)&0xffffu;
  data_.offroadFrames=offroadSerial-offroadBase_;
  data_.objectHits=(objectSerial-objectBase_)&0xffffu;
  data_.aiBonus=aiBonus&0xffffu;
  finished_=finished;
 }
 // Keep the cooldown independent of sound playback and GPU/effect residency.
 void wallStep(bool racing,bool beforeRespawn,bool contact,float speed,bool penaltyAllowed){
  if(finished_)return;
  wallFrames_=std::min(wallFrames_+1,100);
  if(wallImpactReady(beforeRespawn,contact,speed,wallFrames_)){
   wallFrames_=0;
   if(racing&&penaltyAllowed)data_.wallHits=(data_.wallHits+1)&0xffffu;
  }
 }
 const GpRacePerformance& data()const{return data_;}
 bool finished()const{return finished_;}
private:
 int wallFrames_=0;
 GpRacePerformance data_{};bool finished_=false;unsigned miniTurboBase_=0,offroadBase_=0,objectBase_=0;
};
}
