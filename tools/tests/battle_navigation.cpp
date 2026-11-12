#include <mkw/battle_navigation.hpp>
#include <cassert>
#include <cstdio>
#include <set>
using namespace mkw;
int main(){
    using S=BattleNavigation::State;
    struct Case {S state;unsigned difficulty;bool majority;unsigned first,second;};
    constexpr Case cases[]={
        {S::ItemSearch,0,false,0,0},{S::ItemSearch,0,true,0,0},
        {S::ItemSearch,1,false,60,20},{S::ItemSearch,1,true,50,30},
        {S::ItemSearch,2,false,80,10},{S::ItemSearch,2,true,70,20},
        {S::CoinSearch,0,false,50,50},{S::CoinSearch,0,true,50,50},
        {S::CoinSearch,1,false,75,25},{S::CoinSearch,1,true,60,20},
        {S::CoinSearch,2,false,90,10},{S::CoinSearch,2,true,80,10},
        {S::Attack,0,false,0,100},{S::Attack,0,true,0,100},
        {S::Attack,1,false,75,25},{S::Attack,1,true,65,15},
        {S::Attack,2,false,90,10},{S::Attack,2,true,80,10}};
    for(const auto& c:cases){std::array<unsigned,11> bins{};for(unsigned roll=0;roll<100;++roll)++bins[BattleNavigation::pick(c.state,c.difficulty,c.majority,roll)];
        if(c.state==S::ItemSearch){assert(bins[3]==c.first&&bins[7]==c.second&&bins[2]==100-c.first-c.second);}
        if(c.state==S::CoinSearch){assert(bins[8]==c.first&&bins[2]==c.second&&bins[5]==100-c.first-c.second);}
        if(c.state==S::Attack){assert(bins[6]==c.first&&bins[2]==c.second&&bins[5]==100-c.first-c.second);}}
    BattleNavigation n;unsigned calls=0;auto random=[&](unsigned limit){assert(limit==100);++calls;return 0u;};
    n.step(0,false,false,1,false,random);assert(n.state()==S::Decide&&n.mode()==2&&calls==0);
    n.step(0,false,false,1,false,random);assert(n.state()==S::ItemSearch&&n.mode()==3);
    n.step(0,true,true,1,false,random);assert(n.state()==S::ItemSearch&&n.mode()==3&&calls==1);
    n.step(0,true,false,1,false,random);assert(n.state()==S::Attack&&n.mode()==6&&calls==2);
    n.step(0,true,true,1,false,random);assert(calls==2); // Attack rolls on entry, not every route point.
    n.step(0,false,false,1,false,random);n.step(2,false,false,1,false,random);
    n.step(2,false,false,1,false,random);assert(n.state()==S::CoinSearch&&n.mode()==8);
    n.step(2,true,false,1,false,random);assert(n.state()==S::CoinSearch); // collectors retain coins as destination while armed
    n.step(1,true,false,1,false,random);n.step(1,true,false,1,false,random);n.step(1,true,false,1,false,random);
    assert(n.state()==S::ItemSearch);
    std::set<std::array<unsigned,6>> unique;
    for(unsigned rank=0;rank<720;++rank){auto p=battlePermutation(6,rank);auto sorted=p;std::sort(sorted.begin(),sorted.end());
        assert((sorted==std::array<unsigned,6>{0,1,2,3,4,5}));unique.insert(p);}
    assert(unique.size()==720);
    assert((battlePermutation(6,0)==std::array<unsigned,6>{0,1,2,3,4,5}));
    assert((battlePermutation(6,719)==std::array<unsigned,6>{5,4,3,2,1,0}));
    assert(unsigned(6*battleAttackerFraction(0,false))==6);
    assert(unsigned(6*battleAttackerFraction(1,false))==3&&unsigned(6*battleAttackerFraction(1,true))==4);
    assert(unsigned(6*battleAttackerFraction(2,false))==2&&unsigned(6*battleAttackerFraction(2,true))==5);
    std::puts("PASS original 18 navigation probability tables, queued role/item transitions and RNG cadence, native720 role permutations, easy/normal/hard attacker fractions");
}
