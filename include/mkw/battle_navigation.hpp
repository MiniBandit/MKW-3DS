#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace mkw {
// AIControlBattle ItemSearch/CoinSearch/Attack (USA807247cc..8072514c).
// The alternate tables apply to the team with MORE local humans (8073de08),
// not to Coin Runners. Battle type controls role assignment separately.
class BattleNavigation {
public:
    enum class State {Decide,ItemSearch,CoinSearch,Attack,Approach,Retreat,None};
    void reset(){*this=BattleNavigation{};}
    template<class Random> void step(unsigned role,bool hasItem,bool switched,unsigned difficulty,bool humanMajority,Random random,
                                    bool releasing=false){
        if(next_!=State::None){
            // ControlBattle80725150/8072525c: Approach enter clears
            // Path+21, and its exit restores it. The constructor's+1b5
            // enable is true; the flag permits the incoming link as well.
            if(state_==State::Approach)excludePrevious_=true;
            state_=next_;next_=State::None;
            if(state_==State::Approach)excludePrevious_=false;
            if(state_==State::ItemSearch)mode_=3;
            else if(state_==State::CoinSearch)mode_=8;
            else if(state_==State::Attack)mode_=pick(state_,difficulty,humanMajority,random(100));
            else if(state_==State::Approach)mode_=9;
            else if(state_==State::Retreat)mode_=10;
        }
        switch(state_){
        case State::Decide:next_=role==2?State::CoinSearch:State::ItemSearch;break;
        case State::ItemSearch:
            if(switched)mode_=pick(state_,difficulty,humanMajority,random(100));
            if(hasItem)next_=State::Attack;
            break;
        case State::CoinSearch:
            if(switched)mode_=pick(state_,difficulty,humanMajority,random(100));
            if(role!=2)next_=State::Decide;
            break;
        case State::Attack:
            if(!hasItem&&!releasing)next_=State::Decide;
            break;
        case State::Approach:case State::Retreat:if(!hasItem&&!releasing)next_=State::Decide;break;
        default:break;
        }
    }
    unsigned mode()const{return mode_;}
    bool excludePrevious()const{return excludePrevious_;}
    State state()const{return state_;}
    void requestAttack(unsigned direction){if(state_==State::Attack&&direction)next_=direction==1?State::Approach:State::Retreat;}
    static unsigned pick(State state,unsigned difficulty,bool humanMajority,unsigned roll){
        difficulty=std::min(difficulty,2u);
        if(state==State::ItemSearch){
            constexpr unsigned Weights[3][2][2]={{{0,0},{0,0}},{{60,20},{50,30}},{{80,10},{70,20}}};
            const auto& w=Weights[difficulty][humanMajority];return roll<w[0]?3:roll<w[0]+w[1]?7:2;
        }
        if(state==State::CoinSearch){
            constexpr unsigned Weights[3][2][2]={{{50,50},{50,50}},{{75,25},{60,20}},{{90,10},{80,10}}};
            const auto& w=Weights[difficulty][humanMajority];return roll<w[0]?8:roll<w[0]+w[1]?2:5;
        }
        if(state==State::Attack){
            // Both of retail's first two Attack bins follow rival influence;
            // their close-range item-target behaviour is handled separately.
            constexpr unsigned Weights[3][2][2]={{{0,100},{0,100}},{{75,25},{65,15}},{{90,10},{80,10}}};
            const auto& w=Weights[difficulty][humanMajority];return roll<w[0]?6:roll<w[0]+w[1]?2:5;
        }
        return 2;
    }
private:
    State state_=State::Decide,next_=State::Decide;unsigned mode_=2;bool excludePrevious_=true;
};
// AITrickHandler8072b56c / Bike8072b6c8, ProbabilityBattle vt24.
// Opportunity gates are shared with Race; Battle has no percentage draw.
// Car directions use the original{1,2,3,4} table, bike always Up1.
template<class Random> uint8_t battleTrickDirection(bool bike,bool airStart,bool jumpPad,bool inAction,Random random){
    if(!airStart||jumpPad||inAction)return 0;
    return bike?1:uint8_t(1+random(4));
}
inline unsigned battlePermutationCount(unsigned count){unsigned result=1;for(unsigned i=2;i<=count;++i)result*=i;return result;}
inline std::array<unsigned,6> battlePermutation(unsigned count,unsigned rank){
    std::array<unsigned,6> result{},unused{0,1,2,3,4,5};count=std::min(count,6u);
    unsigned divisor=battlePermutationCount(count);
    rank%=divisor;
    for(unsigned i=0;i<count;++i){divisor/=count-i;const unsigned pick=rank/divisor;rank%=divisor;
        result[i]=unused[pick];for(unsigned j=pick;j+1<count-i;++j)unused[j]=unused[j+1];}
    return result;
}
inline float battleAttackerFraction(unsigned difficulty,bool alternate){
    // Coin Battle director8073e2f4 / 8073e3b4: rebalance every30 seconds.
    if(difficulty==0)return 1.f;
    return difficulty==1?(alternate?.67f:.5f):(alternate?.834f:.34f);
}
}
