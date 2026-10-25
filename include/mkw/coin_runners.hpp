#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <mkw/math.hpp>

namespace mkw {
// Coin Runners: coins wait at the arena's coin spots (KMP GOBJ object 0x73),
// a racer driving through one collects it and it comes back later; a racer who
// is hit drops half of their coins, scattered for anyone to pick up. After three
// minutes the most coins wins.
class CoinRunners {
public:
    static constexpr unsigned MaxRacers=12;
    static constexpr unsigned DurationTicks=180*60;
    static constexpr unsigned RespawnTicks=600;    // a field coin returns after 10 s
    static constexpr unsigned DroppedTicks=600;    // a dropped coin lasts 10 s
    static constexpr unsigned PickupDelay=30;      // ... and can be taken after 0.5 s
    static constexpr float PickupRadius=220.f;
    struct Coin {Vec3 position{};bool active=true;bool dropped=false;uint32_t until=0;uint32_t from=0;};

    void reset(unsigned racers,const std::vector<Vec3>& spots){
        count_=std::clamp(racers,1u,MaxRacers);coins_.fill(0);field_.clear();tick_=0;
        for(const auto& p:spots)field_.push_back({p,true,false,0,0});
    }
    // Once per simulation tick with every racer's position.
    void step(const std::array<Vec3,MaxRacers>& racers){
        ++tick_;
        for(auto it=field_.begin();it!=field_.end();){
            auto& c=*it;
            if(c.dropped&&tick_>=c.until){it=field_.erase(it);continue;}
            if(!c.active&&!c.dropped&&tick_>=c.until)c.active=true;
            if(c.active&&tick_>=c.from)
                for(unsigned r=0;r<count_;++r){
                    const Vec3 d=racers[r]-c.position;
                    if(d.x*d.x+d.z*d.z<PickupRadius*PickupRadius&&std::abs(d.y)<400.f){
                        if(coins_[r]<999)++coins_[r];
                        if(c.dropped){c.until=0;c.active=false;}
                        else{c.active=false;c.until=tick_+RespawnTicks;}
                        break;
                    }
                }
            if(c.dropped&&!c.active){it=field_.erase(it);continue;}
            ++it;
        }
    }
    // A hit: half of the victim's coins (at least one) scatter around them.
    void hit(unsigned victim,Vec3 position){
        if(victim>=count_||!coins_[victim])return;
        const unsigned lost=std::max(1u,coins_[victim]/2);
        coins_[victim]-=lost;
        for(unsigned i=0;i<lost&&field_.size()<MaxField;++i){
            const float a=float(i)*6.2831853f/float(lost),r=260.f+float(i%3)*80.f;
            field_.push_back({position+Vec3{std::cos(a)*r,0,std::sin(a)*r},true,true,tick_+DroppedTicks,tick_+PickupDelay});
        }
    }
    unsigned racers()const{return count_;}
    unsigned coins(unsigned racer)const{return racer<count_?coins_[racer]:0;}
    // 0 = most coins; ties keep racer order.
    unsigned rank(unsigned racer)const{
        unsigned r=0;
        for(unsigned i=0;i<count_;++i)if(coins_[i]>coins_[racer]||(coins_[i]==coins_[racer]&&i<racer))++r;
        return r;
    }
    const std::vector<Coin>& field()const{return field_;}
    uint32_t tick()const{return tick_;}
private:
    static constexpr size_t MaxField=256;
    unsigned count_=1;
    std::array<unsigned,MaxRacers> coins_{};
    std::vector<Coin> field_;
    uint32_t tick_=0;
};
}
