#pragma once
#include <mkw/coin_battle.hpp>
#include <mkw/race_effects.hpp>
#include <array>
#include <span>

namespace mkw {
// CoinUnit states1/2/3/6 (USA808689f4,699b8,6ac1c,6b73c).
// Logical ownership stays in CoinBattle; this owns only the visible unit and
// its six original Effect handles. No held pile is drawn on a kart.
class CoinPresentation {
public:
    inline static constexpr std::array<const char*,6> Roots{
        "rk_coinAppear","rk_coinAdd","rk_coinShine","rk_coinGetPre","rk_coinGetCoinTail","rk_coinGet"};
    struct Unit {
        Vec3 position{},velocity{},pickupOffset{};
        float scale=1,yaw=0,pitch=0;
        bool model=false,shadow=false,initialized=false,dynamic=false,add=false;
        CoinBattle::State state=CoinBattle::State::Dormant;
        unsigned frame=0,rotation=0,appearanceDuration=0,pickupDelay=0,pickupStart=0,active=0,pending=0;
        uint8_t owner=CoinBattle::NoOwner;
    };
    void reset(size_t count){units_.assign(count,{});generation_=~0u;arrivalCues_=0;tick_=0;pickupSerial_={};transferBatch_={};transferDebt_={};}
    const Unit& unit(unsigned i)const{return units_[i];}
    bool heldReady(unsigned i)const{
        const auto& u=units_[i];return u.initialized&&u.state==CoinBattle::State::Held&&u.frame>=u.pickupStart+51+u.pickupDelay;
    }
    uint16_t takeArrivalCues(){const auto cues=arrivalCues_;arrivalCues_=0;return cues;}
    void step(std::span<const CoinBattle::Coin> coins,unsigned course,
            const std::array<Vec3,12>& owners,std::span<const uint8_t> visible,RaceEffects& effects){
        if(generation_!=effects.generation()){
            firstSlot_=effects.allocateObjectSlots(unsigned(units_.size()*6));
            generation_=effects.generation();
            for(unsigned i=0;i<Roots.size();++i)specs_[i]=effects.find(Roots[i]);
            for(auto& u:units_){u.active=0;u.initialized=false;}
        }
        for(unsigned i=0;i<units_.size();++i){auto& u=units_[i];const auto& c=coins[i];
            const bool transition=!u.initialized||u.state!=c.state||u.dynamic!=c.dynamic||(c.state==CoinBattle::State::Held&&u.owner!=c.owner);
            if(transition){
                for(unsigned j=0;j<6;++j)effects.fade(firstSlot_+6*i+j);
                const auto old=u.state;u.active=0;u.pending=0;u.frame=0;
                u.model=u.shadow=false;u.scale=1;u.pitch=0;
                if(c.state==CoinBattle::State::Held){
                    // State6 captures coin - kart once. The target then follows
                    // the real kart every calc, rather than a fixed pickup spot.
                    if(old!=CoinBattle::State::Held){u.position=c.body.visualPosition(course);u.velocity=c.body.velocity;}
                    // Manager808726e0..710 serializes acquisitions for each
                    // racer. This debt extends state6's tail, not its45 cue.
                    auto& serial=pickupSerial_[c.owner];
                    u.pickupStart=c.pickupStartDelay;
                    if(c.transferBatch){
                        //80873490..d0/808735e4..610:6-frame stagger,
                        // tail debt makes the entire transfer finish together.
                        if(transferBatch_[c.owner]!=c.transferBatch){
                            const unsigned target=tick_+c.pickupStartDelay+c.pickupTailDelay;
                            if(target<=serial){++serial;transferDebt_[c.owner]=serial-target;}
                            else{serial=target;transferDebt_[c.owner]=0;}
                            transferBatch_[c.owner]=c.transferBatch;
                        }
                        u.pickupDelay=c.pickupTailDelay+transferDebt_[c.owner];
                    }else if(tick_<=serial){++serial;u.pickupDelay=serial-tick_;}
                    else{serial=tick_;u.pickupDelay=0;}
                    u.pickupOffset=u.position-owners[c.owner];u.pending=u.pickupStart?0:(1u<<3)|(1u<<4);
                }else{
                    u.position=c.body.visualPosition(course);
                    if(c.state==CoinBattle::State::Dropped)u.rotation=i%89;
                    if(c.state==CoinBattle::State::Spawning){u.appearanceDuration=c.appearanceTicks;u.pending=1;}
                    if(c.state==CoinBattle::State::Available){
                        if(old==CoinBattle::State::Spawning)
                            u.rotation=(unsigned(u.yaw/6.2831854820251465f*89.f)+1)%89;
                        u.add=c.dynamic&&old!=CoinBattle::State::Dropped;
                        u.pending=1u<<(u.add?1:2);
                    }
                }
                u.state=c.state;u.dynamic=c.dynamic;u.owner=c.owner;u.initialized=true;
            }
            unsigned desired=0;
            if(c.state==CoinBattle::State::Spawning){
                u.position=c.body.visualPosition(course);
                const float t=u.appearanceDuration?float(u.appearanceDuration-c.appearanceTicks)/float(u.appearanceDuration):1;
                const float ease=1-(1-t)*(1-t);
                u.scale=ease;u.pitch=94.2477798461914f*(1-ease);
                u.yaw+=1.5f+(.07f-1.5f)*ease;u.model=true;u.shadow=true;desired=1;
            }else if(c.state==CoinBattle::State::Available||c.state==CoinBattle::State::Dropped||c.state==CoinBattle::State::Retiring){
                u.position=c.body.visualPosition(course);u.yaw=6.2831854820251465f*float(u.rotation)/89.f;u.rotation=(u.rotation+1)%89;
                u.model=true;u.shadow=true;
                if(c.state==CoinBattle::State::Available)desired=1u<<(u.add?1:2);
            }else if(c.state==CoinBattle::State::Held&&u.frame<u.pickupStart){
                u.model=u.shadow=false;
            }else if(c.state==CoinBattle::State::Held&&u.frame<u.pickupStart+51+u.pickupDelay&&u.owner==c.owner){
                // 8086bb44..bf40:7-frame scale1->.42,45-frame parabola,
                // two .95 interpolations, original .52 spin per calc. The
                // model is hidden after frame45; Get lasts through state6.
                const unsigned frame=u.frame-u.pickupStart;
                if(!frame&&u.pickupStart){u.pickupOffset=u.position-owners[c.owner];u.pending=(1u<<3)|(1u<<4);}
                u.scale=1+(.42f-1)*std::min(1.f,float(frame)/7.f);
                u.yaw+=.52f;
                Vec3 offset{};
                if(frame<45){const float t=float(frame)/45.f;
                    const float height=std::max(400.f,u.pickupOffset.y+50.f);
                    const float curve=2*t-1;
                    offset=lerp(u.pickupOffset,Vec3{},t)+Vec3{0,height*(1-curve*curve),0};
                }
                const auto target=owners[c.owner]+offset;
                u.velocity=lerp(u.velocity,target-u.position,.95f);
                u.position=lerp(u.position,u.position+u.velocity,.95f);
                u.model=frame<46;u.shadow=u.model;desired=(1u<<3)|(1u<<4);
                if(frame==45){u.pending|=1u<<5;arrivalCues_|=uint16_t(1u<<c.owner);}
                if(frame>=45)desired|=1u<<5;
            }else{u.model=u.shadow=false;}
            EffectFrame frame;frame.origin=u.position;
            // Effect setScale is the original final model scale, even while
            // state1 eases the visible mesh. State6 uses its current scale.
            frame.scale=c.state==CoinBattle::State::Held?u.scale:1;
            const float cs=std::cos(u.yaw),sn=std::sin(u.yaw);
            frame.x={cs,0,-sn};frame.z={sn,0,cs};
            const bool inView=i>=visible.size()||visible[i];
            for(unsigned j=0;j<6;++j){const unsigned bit=1u<<j,slot=firstSlot_+6*i+j;
                auto root=frame;
                if(j!=3&&j!=4){root.x={1,0,0};root.z={0,0,1};}
                if(!(desired&bit)){if(u.active&bit)effects.fade(slot);u.active&=~bit;u.pending&=~bit;continue;}
                // CoinUnit record+4 is true only for Add/Shine. Those restart
                // when visible again; one-shots are consumed on their first
                // visible create and never emit again during this state.
                if(!inView){if(u.active&bit)effects.fade(slot);u.active&=~bit;u.pending&=~bit;continue;}
                if(j==1||j==2||(u.pending&bit)){
                    effects.create(slot,specs_[j],root);u.active|=bit;u.pending&=~bit;
                }
                effects.move(slot,root);
                effects.setDisabled(slot,false,false);
            }
            ++u.frame;
        }
        ++tick_;
    }
private:
    std::vector<Unit> units_;
    std::array<unsigned,12> transferBatch_{},transferDebt_{};
    std::array<int,6> specs_{};
    unsigned firstSlot_=0,generation_=~0u;
    uint16_t arrivalCues_=0;
    unsigned tick_=0;
    std::array<unsigned,12> pickupSerial_{};
};
} // namespace mkw
