#include <mkw/coin_battle.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace mkw;
namespace {
std::vector<uint8_t> fixture(){
    // Two primary points (including fixed kind1), three reserved points.
    // Keep dormant reservations so a lost unit cannot occupy a later wave.
    std::vector<uint8_t> out(28+5*60);
    auto u16=[&](unsigned at,unsigned v){out[at]=v>>8;out[at+1]=v;};
    auto u32=[&](unsigned at,uint32_t v){out[at]=v>>24;out[at+1]=v>>16;out[at+2]=v>>8;out[at+3]=v;};
    auto f32=[&](unsigned at,float v){uint32_t bits;std::memcpy(&bits,&v,4);u32(at,bits);};
    std::memcpy(out.data(),"RKMD",4);u32(4,out.size());u16(8,1);u16(10,20);u32(16,0);
    std::memcpy(out.data()+20,"GOBJ",4);u16(24,5);
    for(unsigned i=0;i<5;++i){const unsigned at=28+60*i;
        u16(at,115);f32(at+4,2000.f*i);f32(at+8,i==2?900.f:1000.f);f32(at+12,2000);
        u16(at+42,0);u16(at+44,i<2?i:0xffff);u16(at+46,i==1?1:0);
    }
    return out;
}
CoinBattle fresh(){
    BattleRules rules;rules.initialCoins=2;rules.maxCoins=5;rules.coinWaveSize=1;rules.coinReplenishThreshold=10;
    std::array<RaceTeam,12> teams{};teams[1]=RaceTeam::Blue;
    const auto kmp=fixture();CoinBattle battle;assert(battle.load(kmp.data(),kmp.size(),rules,2,teams));
    battle.assignWaves();assert(battle.activate(0)==2);return battle;
}
struct Draws {
    std::vector<unsigned> values,ranges;unsigned at=0;
    Draws(std::initializer_list<unsigned> v):values(v){}
    static unsigned next(void* ptr,unsigned range){auto& d=*static_cast<Draws*>(ptr);
        d.ranges.push_back(range);assert(d.at<d.values.size());return d.values[d.at++];}
};
CoinBody launch(void*,unsigned,unsigned,unsigned){CoinBody b;b.previous=b.position={0,5000,2000};return b;}
ItemSurface kill(Vec3,Vec3,float){ItemSurface s;s.kill=true;return s;}
bool retire(void* ptr,unsigned,const CoinBattle::Coin&){++*static_cast<unsigned*>(ptr);return true;}
bool visible(void* ptr,unsigned,const CoinBattle::Coin&){++*static_cast<unsigned*>(ptr);return false;}
}
int main(){
    // Visual offset participates in both acceleration and loss, without
    // moving the collision sphere or changing nine other course origins.
    for(unsigned course=32;course<42;++course)assert(CoinBody::visualOffset(course)==(course==34?50.f:0.f));
    CoinBody body;body.position={2000,-20,2000};
    assert(body.step(nullptr,false,34)==CoinBody::Contact::Air&&body.position.y<0&&body.visualPosition(34).y>0);
    body={};body.position={2000,-20,2000};assert(body.step(nullptr,false,32)==CoinBody::Contact::Lost);
    body={};body.position={2000,800,2000};body.velocity={0,10,0};
    assert(body.step(nullptr,false,34,1000)==CoinBody::Contact::Air);
    assert(std::abs(body.velocity.y-6.733f)<.0002f); // visible850: not below850 before update
    body={};body.position={2000,799,2000};body.velocity={0,10,0};
    assert(body.step(nullptr,false,34,1000)==CoinBody::Contact::Air);
    assert(std::abs(body.velocity.y-8.119f)<.0002f); // visible849: underwater acceleration
    body={};body.position={2000,800,2000};
    assert(body.step(nullptr,false,34,1000)==CoinBody::Contact::Lost); // crosses850 this tick

    auto battle=fresh();
    battle.stepDropped(nullptr,34,1200);assert(battle.coins()[0].state==CoinBattle::State::Available);
    battle.stepDropped(nullptr,34,1201);assert(battle.coins()[0].state==CoinBattle::State::Retiring);
    for(unsigned tick=0;tick<179;++tick)battle.stepDropped(nullptr,34);
    assert(battle.coins()[0].state==CoinBattle::State::Retiring&&!battle.collect(0,0));
    battle.stepDropped(nullptr,34);assert(battle.coins()[0].state==CoinBattle::State::Lost);
    assert(!battle.recycle(0,0)); // no primary point even if its coin is lost
    assert(!battle.recycle(0,2)); // reserved, but still occupied by a dormant wave
    assert(battle.stepWaves()==1&&battle.collect(2,0));
    assert(!battle.coins()[2].placementBound);
    assert(!battle.recycle(0,2,nullptr,nullptr,150,1000)); // placement900 is below actual water
    assert(battle.recycle(0,2,nullptr,nullptr,150,900)); // equality is eligible
    assert(battle.coins()[0].state==CoinBattle::State::Spawning&&battle.coins()[0].placementBound);
    assert(battle.coins()[0].appearanceTicks==150&&battle.score(0)==1);

    battle=fresh();unsigned calls=0;
    battle.failedRegistration(1);Draws fixed{1};
    assert(!battle.stepRecycling(Draws::next,&fixed,nullptr,nullptr,150,CoinBody::NoWater,retire,&calls));
    assert(fixed.at==1&&calls==0&&!battle.coins()[1].registrationFailed&&battle.coins()[1].state==CoinBattle::State::Available);
    battle.failedRegistration(0);Draws seen{0};
    assert(!battle.stepRecycling(Draws::next,&seen,nullptr,nullptr,150,CoinBody::NoWater,visible,&calls));
    assert(seen.at==1&&calls==1&&!battle.coins()[0].registrationFailed);
    assert(battle.coins()[0].state==CoinBattle::State::Available);
    battle.failedRegistration(0);Draws hidden{0};
    assert(battle.stepRecycling(Draws::next,&hidden,nullptr,nullptr,150,CoinBody::NoWater,retire,&calls));
    assert(hidden.at==1&&calls==2&&battle.coins()[0].state==CoinBattle::State::Retiring&&!battle.coins()[0].placementBound);
    Draws retiring{0};assert(!battle.stepRecycling(Draws::next,&retiring)&&retiring.at==1);
    for(unsigned tick=0;tick<180;++tick)battle.stepDropped(nullptr,32);
    assert(battle.coins()[0].state==CoinBattle::State::Lost);
    Draws occupied{0,3};assert(!battle.stepRecycling(Draws::next,&occupied));
    assert(occupied.at==2&&occupied.ranges==std::vector<unsigned>({5,5}));
    Draws dormant{4};assert(!battle.stepRecycling(Draws::next,&dormant));assert(dormant.at==1);
    // Free a reserved point through collection; the next manager selection
    // can reuse it once, without score changes or duplicate point ownership.
    assert(battle.stepWaves()==1&&battle.collect(2,0));
    Draws recycled{0,2};assert(battle.stepRecycling(Draws::next,&recycled));
    assert(recycled.at==2&&battle.coins()[0].placement==2&&battle.coins()[0].appearanceTicks==150&&battle.score(0)==1);
    battle.failedRegistration(0);assert(!battle.coins()[0].registrationFailed); // pending spawn is not settled
    assert(battle.hit(0,false,launch)==1);battle.failedRegistration(2);
    assert(!battle.coins()[2].registrationFailed&&!battle.coins()[2].placementBound);
    battle.stepDropped(kill,32);assert(battle.coins()[2].state==CoinBattle::State::Retiring);
    for(unsigned tick=0;tick<180;++tick)battle.stepDropped(kill,32);
    assert(battle.coins()[2].state==CoinBattle::State::Lost);
    Draws primary{2,0};assert(!battle.stepRecycling(Draws::next,&primary));assert(primary.at==2);
    puts("PASS native visual/body offset, rising-water acceleration and moving/resting loss boundaries, reserved/occupied/dormant/submerged point gates, deferred failed-registration retirement, one-unit/one-placement RNG and delayed reuse with unique ownership");
}
