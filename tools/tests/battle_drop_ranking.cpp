#include <mkw/coin_battle.hpp>
#include <mkw/balloon_battle.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace mkw;
namespace {
bool near(float a,float b,float epsilon=.001f){return std::abs(a-b)<epsilon;}
std::vector<uint8_t> course(){
    std::vector<uint8_t> bytes(28+6*60);
    auto be16=[&](unsigned at,unsigned v){bytes[at]=v>>8;bytes[at+1]=v;};
    auto be32=[&](unsigned at,unsigned v){bytes[at]=v>>24;bytes[at+1]=v>>16;bytes[at+2]=v>>8;bytes[at+3]=v;};
    std::memcpy(bytes.data(),"RKMD",4);be32(4,bytes.size());be16(8,1);be16(10,20);std::memcpy(bytes.data()+20,"GOBJ",4);be16(24,6);
    for(unsigned i=0;i<6;++i){const unsigned at=28+i*60;be16(at,115);be16(at+44,i);}
    return bytes;
}
struct Draws {
    unsigned at=0;
    static unsigned next(void* p,unsigned limit){auto& d=*static_cast<Draws*>(p);assert(limit==3&&d.at<3);return d.at++?d.at-1:1;}
};
struct Drop {
    CoinDropPacket packet;unsigned calls=0;
    static CoinBody launch(void* p,unsigned victim,unsigned index,unsigned count){auto& d=*static_cast<Drop*>(p);
        assert(victim==0&&index==d.calls++&&count==3);return CoinBody::launchPacket(d.packet,index,count);}
};
}
int main(){
    // Golden packet reconstructions include source offsets and truncation,
    // rather than expecting the lossless input coordinates of the old port.
    auto packet=CoinDropPacket::capture({0,0,0},0);
    assert(packet.position.x==-57&&packet.position.y==-9&&packet.position.z==-57&&packet.yaw==0);
    packet=CoinDropPacket::capture({1000,1000,-30000},.1234567f);
    assert(packet.position.x==967&&packet.position.y==951&&packet.position.z==-30009&&packet.yaw==.09375f);
    packet=CoinDropPacket::capture({-35000,-5000,-35000},-3.14159265358979323846f);
    assert(packet.position.x==-35000&&packet.position.y==-5000&&packet.position.z==-35000&&packet.yaw==-3.1484375f);
    auto dir=CoinDropPacket::direction({-40,99,0},{0,0,1});assert(dir.x==-1&&dir.y==0&&dir.z==0);
    dir=CoinDropPacket::direction({0,99,0},{1,1,0});assert(dir.x==1&&dir.y==0&&dir.z==0);
    dir=CoinDropPacket::direction({.0001f,99,0},{0,0,0});assert(dir.x==0&&dir.y==0&&dir.z==1);
    dir=CoinDropPacket::direction({.001f,99,0},{0,0,1});assert(dir.x==1);
    for(unsigned count:{1u,3u,7u,71u}){Vec3 sum{};
        for(unsigned i=0;i<count;++i){auto body=CoinBody::launchPacket(packet,i,count);
            assert(body.position.y==packet.position.y+250&&body.previous.y==body.position.y&&near(length(body.velocity),80));sum+=body.velocity;}
        if(count>1)assert(std::hypot(sum.x,sum.z)<.003f);
    }
    BattleRanking ranks;ranks.reset(3);std::array<uint16_t,12> scores{};
    scores[2]=4;ranks.step(scores);assert(ranks.rank(2)==1);
    scores[0]=scores[1]=4;ranks.step(scores);assert(ranks.rank(2)==1&&ranks.rank(0)==2&&ranks.rank(1)==3);
    scores[1]=5;ranks.step(scores);scores[2]=5;ranks.step(scores);assert(ranks.rank(1)==1&&ranks.rank(2)==2);
    scores.fill(0);ranks.step(scores);assert(ranks.rank(1)==1&&ranks.rank(2)==2&&ranks.rank(0)==3);
    ranks.reset(3);ranks.step(scores);assert(ranks.rank(0)==1&&ranks.rank(1)==2&&ranks.rank(2)==3);
    BalloonBattle balloons;balloons.reset(3);assert(balloons.loseBalloon(0,12));balloons.updateRanks();
    assert(balloons.balloons(0)==2&&balloons.balloons(1)==3&&balloons.rank(0)==1); // no balloon-count tie key
    BattleRules rules;rules.initialCoins=6;rules.maxCoins=6;
    std::array<RaceTeam,12> teams{};teams[1]=RaceTeam::Blue;
    CoinBattle coins;auto bytes=course();assert(coins.load(bytes.data(),bytes.size(),rules,3,teams));coins.assignWaves();coins.activate(0);
    Draws draws;assert(coins.initializePickupPriority(Draws::next,&draws)&&draws.at==3);
    assert(coins.collectContacts(0,3)&&coins.score(1)==1&&coins.score(0)==0); // simultaneous contact chooses original random priority
    assert(!coins.collectContacts(0,3)&&!coins.collectContacts(1,1u<<10));
    coins.updateRanks();assert(coins.rank(1)==1);
    assert(coins.collect(1,0));coins.updateRanks();assert(coins.rank(1)==1&&coins.rank(0)==2); // tie keeps prior leader
    assert(coins.collect(2,0)&&coins.collect(3,0)&&coins.collect(4,0)&&coins.collect(5,0));coins.updateRanks();assert(coins.rank(0)==1);
    Drop drop{CoinDropPacket::capture({1000,1000,-30000},.1234567f)};
    assert(coins.hit(0,false,Drop::launch,&drop)==3&&drop.calls==3&&coins.score(0)==2);
    unsigned dropped=0;for(const auto& coin:coins.coins())if(coin.state==CoinBattle::State::Dropped){
        assert(coin.body.position.x==967&&coin.body.position.y==1201&&coin.body.position.z==-30009&&coin.owner==CoinBattle::NoOwner);++dropped;}
    assert(dropped==3);
    std::puts("PASS original-format Coin drop golden coordinates/yaw, actual-velocity/front/Z fallback, radial impulses, core ownership/loss and shuffled simultaneous pickup arbitration; persistent score-only Battle rankings across ties/reset and balloon count changes. ARM/target integration separate.");
}
