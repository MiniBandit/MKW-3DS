#pragma once
#include "mkw/kart_start_effects.hpp"
#include "mkw/race_effects.hpp"
#include "mkw/kart_item_effects.hpp"
#include <array>
#include <algorithm>

namespace mkw {
// Smoke state belongs to the race, never to a static in the main loop.
struct KartSmokeState {
    KartStartEffects start;
    bool burnout=false;
};

// The remote KartEffect resources are already in RKRace; route the
// other racers to the original S roots instead of omitting them.
class RemoteKartSmoke {
public:
    static constexpr unsigned FirstSlot=KartFxSlots::FirstFreeSlot+8,MaxRacers=12,SlotsPerRacer=4;
    static_assert(FirstSlot+(MaxRacers-1)*SlotsPerRacer<=RaceEffects::SlotCount);
    bool bind(RaceEffects& effects){
        start_=effects.find("rk_startS");miss_=effects.find("rk_startMissS");
        return start_>=0&&miss_>=0;
    }
    void reset(){state_={};startWheels_={};}
    bool startActive(unsigned racer)const{return racer<MaxRacers&&startWheels_[racer]>0;}
    void step(RaceEffects& effects,unsigned racer,const EffectFrame& pose,
              Vec3 rearLeft,Vec3 rearRight,unsigned timer,bool preview,
              bool countdown,bool accelerate,bool leftFloor,bool rightFloor,
              bool bike,bool burnout,bool hidden,uint32_t serial,
              int duration,bool lod){
        if(!racer||racer>=MaxRacers)return;
        auto& state=state_[racer];
        const unsigned slot=FirstSlot+(racer-1)*SlotsPerRacer;
        const uint8_t group=uint8_t(2+racer);
        // Source S startup table uses the rear-right point (80697078).
        // Keep the other startup slot reserved for independent wheel paths.
        const int wheels=state.start.step(timer,preview,countdown,accelerate,leftFloor,rightFloor,bike,serial,duration);
        if(wheels!=KartStartEffects::Hold)startWheels_[racer]=wheels;
        auto frame=pose;frame.origin=rearRight;
        if(hidden||lod){effects.kill(slot);effects.kill(slot+1);}
        else if(wheels!=KartStartEffects::Hold){
            if(wheels&(bike?1:2))effects.create(slot,start_,frame,group);
            else effects.followFade(slot);
            effects.move(slot,frame);
        }
        // Finite burnout handles keep their own racer group and
        // follow the racer until all authored particles have expired.
        if(!preview&&!hidden&&!lod&&burnout&&!state.burnout){
            effects.kill(slot+2);effects.create(slot+2,miss_,pose,group);
        }
        state.burnout=burnout;
        if(hidden){effects.kill(slot+2);effects.kill(slot+3);}
        else effects.move(slot+2,pose);
        (void)rearLeft;
    }
private:
    int start_=-1,miss_=-1;
    std::array<KartSmokeState,MaxRacers> state_{};
    std::array<int,MaxRacers> startWheels_{};
};

// Original landing signals replace the arbitrary airborne frame thresholds.
// Large smoke is HardLanding (94), small kart smoke is a manual-drift hop.
// Bikes also emit a small puff when settling both wheels after a wheelie.
class KartLandingEffects {
public:
    static constexpr unsigned FirstSlot=RemoteKartSmoke::FirstSlot+(RemoteKartSmoke::MaxRacers-1)*RemoteKartSmoke::SlotsPerRacer;
    static_assert(FirstSlot+12*3<=RaceEffects::SlotCount);
    bool bind(RaceEffects& e){
        static constexpr const char* names[4][2]={{"rk_jumpSmoke","rk_jumpSmokeS"},{"rk_jumpSmokeMini","rk_jumpSmokeMiniS"},
            {"rk_jumpSmokeBikeL","rk_jumpSmokeBikeL_S"},{"rk_jumpSmokeBikeS","rk_jumpSmokeBikeS_S"}};
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<2;++j){spec_[i][j]=e.find(names[i][j]);if(spec_[i][j]<0)return false;}
        miniBike_=e.find("rk_jumpSmokeBikeLMini");return miniBike_>=0;
    }
    void reset(){wheelieFrames_={};}
    void step(RaceEffects& e,unsigned racer,bool local,const EffectFrame& pose,Vec3 rearMid,
              bool preview,bool hidden,bool lod,bool bike,bool groundStart,bool hard,
              bool manualDrift,bool jumpPad,bool water,bool wheelie,bool allWheels){
        if(racer>=12)return;
        const unsigned base=FirstSlot+racer*3;
        auto frame=pose;if(bike)frame.origin=rearMid;
        if(hidden){for(unsigned j=0;j<3;++j)e.kill(base+j);wheelieFrames_[racer]=0;return;}
        if(!preview&&!lod&&!jumpPad&&!water){
            if(hard){
                const int type=bike?(wheelie?2:3):0;
                e.create(base,spec_[type][local?0:1],frame,uint8_t(2+racer));
            }else if(groundStart&&manualDrift){
                const int spec=bike?(local?miniBike_:spec_[3][1]):spec_[1][local?0:1];
                e.create(base+1,spec,frame,uint8_t(2+racer));
            }else if(bike&&allWheels&&wheelieFrames_[racer]>5){
                e.create(base+2,local?miniBike_:spec_[3][1],frame,uint8_t(2+racer));wheelieFrames_[racer]=0;
            }
        }
        if(wheelie)wheelieFrames_[racer]=std::min(wheelieFrames_[racer]+1,200u);
        if(lod)for(unsigned j=0;j<3;++j)e.kill(base+j);
        else for(unsigned j=0;j<3;++j)e.move(base+j,frame);
    }
private:
    int spec_[4][2]{},miniBike_=-1;
    std::array<unsigned,12> wheelieFrames_{};
};
// Bike table index25 (USA80695654), shared original root for local/CPU.
class KartBrakeEffects {
public:
    static constexpr unsigned FirstSlot=1201,MaxRacers=12;
    static_assert(FirstSlot+MaxRacers<=RaceEffects::SlotCount);
    bool bind(RaceEffects& e){spec_=e.find("rk_brakeSmk");return spec_>=0;}
    void reset(){state_={};}
    void step(RaceEffects& e,unsigned racer,const EffectFrame& pose,Vec3 rear,
            bool bike,bool local,bool ghost,bool braking,bool accelerating,
            unsigned driftState,bool autoDrift,float engineSpeed,bool floor,
            bool preview,bool hidden,bool lod){
        if(racer>=MaxRacers)return;
        auto& s=state_[racer];const unsigned slot=FirstSlot+racer;
        const Vec3 previous=s.ready?s.rear:rear;
        const Vec3 velocity=rear-previous;s.rear=rear;s.ready=true;
        if(floor)s.airborne=0;else if(s.airborne<10)++s.airborne;
        if(!bike||ghost||preview||hidden||lod){e.kill(slot);s.active=false;return;}
        // Non-drift branch only; the original rear-wheel helper gives ten
        // ticks' floor grace, then FollowFade. Local/CPU parameters are copied
        // from 80898fd0 offsets84/88 vs8c/90 (life at2c vs0c).
        const bool active=braking&&!accelerating&&driftState==0&&!autoDrift&&engineSpeed>20&&s.airborne<10;
        auto frame=pose;frame.origin=previous;
        if(active&&!s.active)e.kill(slot);
        if(active)e.create(slot,spec_,frame,uint8_t(2+racer));
        else if(s.active)e.followFade(slot);
        e.move(slot,frame);e.setVelocity(slot,velocity*(local?1.25f:1.1f));
        if(active){e.setEmissionRatio(slot,local?.5f:.1f);e.setParticleLife(slot,local?15:10);}
        s.active=active;
    }
private:
    struct State {Vec3 rear{};unsigned airborne=0;bool ready=false,active=false;};
    std::array<State,MaxRacers> state_{};
    int spec_=-1;
};

// ChargingSSMT is A+B standstill wheelspin, not the failed-start Burnout.
// USA80696c8c: kart table[2,5),[5,8), bike table[4,7), per rear-wheel
// contact. Local constructors select all three wheelSpin roots; remote
// tables retain one wheelSpinS per wheel. Existing bounded EF pools stay put.
class KartWheelSpinEffects {
public:
    static constexpr unsigned FirstSlot=1722,SlotsPerRacer=16,MaxRacers=12;
    static_assert(FirstSlot+SlotsPerRacer*MaxRacers<=RaceEffects::SlotCount);
    void step(RaceEffects& e,unsigned racer,const EffectFrame& pose,
              Vec3 left,Vec3 right,bool bike,bool charging,bool charged,int steer,
              bool leftFloor,bool rightFloor,bool preview,bool hidden,bool ghost,bool lod){
        if(racer>=MaxRacers)return;
        if(generation_!=e.generation()){
            generation_=e.generation();charged_={};airborne_={};active_={};
            const char* names[]={"rk_wheelSpin0","rk_wheelSpin1","rk_wheelSpin2","rk_wheelSpinS"};
            for(unsigned i=0;i<4;++i)smoke_[i]=e.find(names[i]);
            const char* sides[]={"L","R","LB","RB","CB"};
            const char* tails[]={"_Spark00","_Spark01","1T_Chip00","1T_Spark00","1T_Spark01","_Spark01S"};
            for(unsigned i=0;i<5;++i)for(unsigned j=0;j<6;++j){char n[64];
                std::snprintf(n,sizeof n,"rk_driftSpark1%s%s",sides[i],tails[j]);spark_[i][j]=e.find(n);
            }
        }
        const unsigned base=FirstSlot+racer*SlotsPerRacer;
        const bool enabled=!preview&&!hidden&&!ghost&&!lod;
        const bool now=enabled&&charged;uint16_t nextActive=0;
        for(unsigned wheel=0;wheel<2;++wheel){
            auto& air=airborne_[racer][wheel];air=(wheel?rightFloor:leftFloor)?0:std::min(air+1,10u);
            auto frame=pose;frame.origin=wheel?right:left;
            // The charged callback sets +0x90; the smoke dispatcher fades its
            // wheelSpin handles then. Airborne spark grace is ten retail ticks.
            const bool contact=(!bike||wheel==1)&&air<10;
            for(unsigned part=0;part<3;++part){const unsigned slot=base+wheel*3+part;
                const bool active=enabled&&charging&&!charged&&contact&&(racer==0||part==0);
                if(!enabled){e.kill(slot);continue;}
                if(active){const uint16_t bit=uint16_t(1u<<(wheel*3+part));nextActive|=bit;
                    if(!(active_[racer]&bit))e.kill(slot);
                    e.create(slot,smoke_[racer==0?part:3],frame,uint8_t(2+racer));}
                else e.followFade(slot);
                e.move(slot,frame);
            }
            // SSMT callback 806ae924: kart L/R, bike steering L/R or neutral C.
            // Preserve the rear-wheel attachment and original resource offsets.
            const unsigned side=bike?(steer<0?2:steer>0?3:4):wheel;
            for(unsigned part=0;part<2;++part){const unsigned slot=base+6+wheel*2+part;
                const bool active=now&&contact&&(racer==0||part==0);
                if(!enabled){e.kill(slot);continue;}
                if(active){const uint16_t bit=uint16_t(1u<<(6+wheel*2+part));nextActive|=bit;
                    if(!(active_[racer]&bit))e.kill(slot);
                    e.create(slot,spark_[side][racer==0?part:5],frame,uint8_t(2+racer));}
                else e.followFade(slot);
                e.move(slot,frame);
            }
            for(unsigned part=0;part<3;++part){const unsigned slot=base+10+wheel*3+part;
                if(!enabled){e.kill(slot);continue;}
                if(now&&!charged_[racer]&&contact&&racer==0){e.kill(slot);e.create(slot,spark_[side][part+2],frame,uint8_t(2+racer));}
                else e.move(slot,frame);
            }
        }
        charged_[racer]=now;active_[racer]=nextActive;
    }
private:
    unsigned generation_=~0u;int smoke_[4]{-1,-1,-1,-1},spark_[5][6]{};
    std::array<bool,MaxRacers> charged_{};
    std::array<uint16_t,MaxRacers> active_{};
    std::array<std::array<unsigned,2>,MaxRacers> airborne_{};
};

}
