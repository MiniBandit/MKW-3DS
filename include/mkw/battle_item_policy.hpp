#pragma once
#include <mkw/math.hpp>
#include <algorithm>
#include <array>

namespace mkw {
// Opponent categories from USA807253fc/80725628, using KartMove+0x74
// (vel1Dir). Categories3/4 mean a close opponent in front/behind.
inline unsigned battleOpponentClass(Vec3 delta,Vec3 direction){
    const float distance2=dot(delta,delta);
    if(distance2>=7000.f*7000.f)return 0;
    if(distance2>=6000.f*6000.f)return 2;
    if(distance2>=4000.f*4000.f)return 1;
    const float angle=std::atan2(length(cross(direction,delta)),dot(direction,delta))*57.295780181884766f;
    return angle<10.f?3:angle>170.f?4:1;
}
// AIControlBattle's Easy/Normal/Hard item policy, USA80725908/25ad8/26044.
// This requests use; AIItemBase consumes it through its queued states.
class BattleItemPolicy {
public:
    struct Intent {bool use=false;unsigned direction=0;};
    void reset(){*this=BattleItemPolicy{};}
    template<class Random> Intent step(unsigned difficulty,unsigned kind,bool canUse,bool front,bool back,Random random){
        constexpr int Wait[]={15,15,15,0,0,60,0,0,0,0,45,0,0,0,0,0,45,45,45}; //8089dd30
        constexpr int Delay[]={360,900,360,180,360,900,900,180,0,360,900,0,180,0,0,900,900,900,900}; //8089dd7c
        constexpr bool Rear[]={false,false,true,true,false,false,false,false,false,false,false,false,false,false,false,false,false,false,true}; //8089ddc8
        difficulty=std::min(difficulty,2u);
        if(state_==0){
            if(kind>=19)return {};
            if(timer_>Wait[kind]){
                timer_=difficulty==0?180:Delay[kind];
                if(difficulty==2)state_=3;
                else if(difficulty==0||random(100)<75)state_=Rear[kind]?1:2;
                else state_=3;
            }else ++timer_;
            return {};
        }
        --timer_;
        if(!canUse)return {};
        unsigned direction=0;
        if(state_==1){if((difficulty!=0&&back)||timer_<0)direction=2;}
        else if(state_==2){if((difficulty!=0&&front)||timer_<0)direction=1;}
        else {if(front)direction=1;else if(back)direction=2;else if(timer_<0)direction=1;}
        if(!direction)return {};
        state_=0;timer_=0;return {true,direction};
    }
private:
    unsigned state_=0;int timer_=0;
};
}
