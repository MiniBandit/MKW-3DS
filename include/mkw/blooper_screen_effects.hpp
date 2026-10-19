#pragma once
#include <mkw/race_effects.hpp>
namespace mkw {
// KartEffect::UpdateInk (RMCU 8069b868). Rank thresholds are established
// from the race's player count, not from the item user's position.
class BlooperScreenEffects {
public:
    static constexpr unsigned FirstSlot=752;
    static_assert(FirstSlot+5<=RaceEffects::SlotCount);
    bool bind(RaceEffects& effects){
        const char* names[]={"rk_gessoA_1P","rk_gessoB_1P","rk_gessoA0_1P","rk_gessoB0_1P","rk_gessoC0_1P"};
        for(unsigned i=0;i<5;++i){spec_[i]=effects.find(names[i]);if(spec_[i]<0)return false;}
        return true;
    }
    void reset(){previousInk_=0;}
    static unsigned style(unsigned rank,unsigned racers){
        const unsigned first=racers/3,second=2*first+(racers%3==2);
        return rank<=first?0:rank<=second?1:2;
    }
    void step(RaceEffects& effects,unsigned ink,unsigned rank,unsigned racers){
        effects.system().cameraSpaceGroups|=1u<<1;
        if(!ink){
            // UpdateInk 8069bdbc..8069be20 resumes the suspended tracks:
            // gravity on the main blot, Newton repulsion on the droplets.
            // Existing particles finish their authored curves/lifetimes.
            if(previousInk_)for(unsigned i=0;i<5;++i)
                effects.stopParticleTrack(FirstSlot+i,i==1?1:2,false);
        }else if(ink>previousInk_){
            for(unsigned i=0;i<5;++i)effects.kill(FirstSlot+i);
            const unsigned kind=style(rank,racers);
            auto create=[&](unsigned i,float y,unsigned stoppedTrack){
                EffectFrame frame;frame.origin.y=y;
                effects.create(FirstSlot+i,spec_[i],frame,1);
                effects.stopParticleTrack(FirstSlot+i,stoppedTrack,true);
            };
            if(kind<2)create(kind,20.f,kind==0?2:1);
            create(2+kind,0.f,2);
        }
        previousInk_=ink;
    }
private:
    int spec_[5]={-1,-1,-1,-1,-1};
    unsigned previousInk_=0;
};
}
