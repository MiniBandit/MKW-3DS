#pragma once
#include <mkw/race_effects.hpp>
#include <mkw/vehicle_exhaust.hpp>
#include <array>
#include <cmath>

namespace mkw {
// Presentation inputs from one remote KartObject. Wheel positions are centres,
// matching KartEffect::UpdateValues; the authored emitters supply their offsets.
struct OpponentDrivingInput {
    EffectFrame pose;
    Vec3 rearLeft{},rearRight{};
    unsigned vehicle=0,driftState=0;
    int driftDirection=0;
    bool bike=false,insideDrift=false,boost=false,wheelie=false,canWheelie=false;
    bool leftFloor=false,rightFloor=false,preview=false,hidden=false;
};

// Missing remote driving emitters, sharing RaceEffects' existing bounded pools.
// USA KartEffect constructor 8068d288/8068dbf4/8068f110 chooses these S roots.
// The full miniTurbo1T onset and jumpSeikou callback are local-only in retail;
// neither is copied to the eleven CPU racers here.
class OpponentDrivingEffects {
public:
    static constexpr unsigned FirstSlot=789,MaxRacers=12,SlotsPerRacer=12;
    static constexpr unsigned EndSlot=FirstSlot+(MaxRacers-1)*SlotsPerRacer;
    static_assert(EndSlot<=RaceEffects::SlotCount,"remote driving effect slots");
    bool bind(RaceEffects& effects){
        static constexpr const char* names[9]={
            "rk_miniTurboS","rk_driftSpark1L_Spark01S","rk_driftSpark1R_Spark01S",
            "rk_driftSpark2L_Spark01S","rk_driftSpark2R_Spark01S",
            "rk_driftSpark1LB_Spark01S","rk_driftSpark1RB_Spark01S",
            "rk_driftSpark1CB_Spark01S","rk_wheelie"};
        for(unsigned i=0;i<specs_.size();++i){specs_[i]=effects.find(names[i]);if(specs_[i]<0)return false;}
        return true;
    }
    void reset(){state_={};}
    void step(RaceEffects& effects,unsigned racer,const OpponentDrivingInput& in,bool lod){
        if(!racer||racer>=MaxRacers)return;
        auto& state=state_[racer-1];
        // 80690570..5b0 counts non-contact ticks; +ae0 defaults to 10
        // (8068a674/700). Sparks survive brief floor gaps while drifting.
        for(unsigned wheel=0;wheel<2;++wheel){
            auto& ticks=state.airborne[wheel];
            if(wheel?in.rightFloor:in.leftFloor)ticks=0;
            else if(ticks<10)++ticks;
        }
        const unsigned base=FirstSlot+(racer-1)*SlotsPerRacer;
        if(in.preview||in.hidden||lod){
            for(unsigned j=0;j<SlotsPerRacer;++j)effects.kill(base+j);
            state.active={};return;
        }
        const uint8_t group=uint8_t(2+racer);
        auto follow=[&](unsigned index,int spec,const EffectFrame& frame,bool active){
            const unsigned slot=base+index;
            // FollowFade keeps residual particles. A new transition back to
            // emission restarts the root even if the old tail is still alive.
            if(active&&!state.active[index])effects.kill(slot);
            if(active)effects.create(slot,spec,frame,group);
            else if(state.active[index])effects.followFade(slot);
            effects.move(slot,frame);state.active[index]=active;
        };
        const auto& exhaust=VehicleExhausts[std::min<unsigned>(in.vehicle,VehicleExhausts.size()-1)];
        for(unsigned pipe=0;pipe<4;++pipe){
            if(!in.boost&&!state.active[pipe]&&!effects.alive(base+pipe))continue;
            EffectFrame frame=in.pose;
            if(pipe<exhaust.count){
                const float* p=exhaust.pipes[pipe];
                constexpr float ToRadians=0.01745329251994329577f;
                const float pitch=p[3]*ToRadians,yaw=p[4]*ToRadians;
                const float sp=std::sin(pitch),cp=std::cos(pitch),sy=std::sin(yaw),cy=std::cos(yaw);
                auto axis=[&](float x,float y,float z){
                    const float y1=y*cp-z*sp,z1=y*sp+z*cp;
                    return in.pose.x*(x*cy+z1*sy)+in.pose.y*y1+in.pose.z*(-x*sy+z1*cy);
                };
                frame.origin=in.pose.toWorld({p[0],p[1],p[2]});
                frame.x=axis(1,0,0);frame.y=axis(0,1,0);frame.z=axis(0,0,1);
                frame.scale=in.pose.scale*exhaust.flameScale;
            }
            follow(pipe,specs_[0],frame,pipe<exhaust.count&&in.boost);
        }
        // KartEffect 80693ea4/80694394: ChargedMt=2 and ChargedSmt=3.
        // Separate blue/orange handles preserve each authored fading tail.
        for(unsigned color=0;color<2;++color)for(unsigned wheel=0;wheel<2;++wheel){
            auto frame=in.pose;frame.origin=wheel?in.rearRight:in.rearLeft;
            follow(4+color*2+wheel,specs_[1+color*2+wheel],frame,
                !in.bike&&in.driftState==(color?3u:2u)&&state.airborne[wheel]<10);
        }
        // 80695180 negates hopStickX for vehicle type 2 (inside drift);
        // 80695458 passes it unchanged otherwise. 8069ea98 selects LB/RB/CB.
        const int sparkDirection=in.insideDrift?-in.driftDirection:in.driftDirection;
        const unsigned bikeSpark=sparkDirection<0?0:sparkDirection>0?1:2;
        auto rear=in.pose;rear.origin=in.rearRight;
        for(unsigned side=0;side<3;++side)
            follow(8+side,specs_[5+side],rear,
                in.bike&&in.driftState>=2&&bikeSpark==side&&state.airborne[1]<10);
        // 80695774: wheelie bit AND MovementBike::canWheelie, with the
        // same rear-wheel contact grace; rk_wheelie is shared by all racers.
        follow(11,specs_[8],rear,in.bike&&in.wheelie&&in.canWheelie&&state.airborne[1]<10);
    }
private:
    struct State {std::array<unsigned,2> airborne{};std::array<bool,SlotsPerRacer> active{};};
    std::array<State,MaxRacers-1> state_{};
    std::array<int,9> specs_{};
};
}
