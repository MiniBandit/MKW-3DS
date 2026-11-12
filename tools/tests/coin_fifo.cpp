#include <mkw/coin_battle.hpp>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace mkw;
namespace {
std::vector<uint8_t> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);assert(f);return {(std::istreambuf_iterator<char>(f)),{}};}
std::vector<unsigned> offsets;
ItemSurface timed(Vec3,Vec3,float,unsigned offset){offsets.push_back(offset);return {};}
CoinBody launch(void*,unsigned,unsigned i,unsigned n){return CoinBody::launch({1000,5000,1000},{0,0,1},i,n);}
}
int main(int argc,char**argv){
    assert(argc==2);const std::filesystem::path assets=argv[1];
    const auto kmg=read(assets/"common/minigame.kmg"),kmp=read(assets/"courses/block_battle/course.kmp");
    BattleRules rules;assert(rules.load(kmg.data(),kmg.size(),33,12,BattleMode::Coin));
    std::array<RaceTeam,12> teams;for(unsigned i=0;i<12;++i)teams[i]=i&1?RaceTeam::Blue:RaceTeam::Red;
    auto fresh=[&]{CoinBattle c;assert(c.load(kmp.data(),kmp.size(),rules,12,teams));c.assignWaves();c.activate(0);return c;};
    const auto packet=CoinDropPacket::capture({1000,5000,1000},.2f);
    auto c=fresh();assert(c.collect(0,0));
    assert(c.score(0)==1&&c.readyHeld(0)==0&&!c.requestHit(0,false,packet));
    c.setHeldReady(0,true);assert(c.readyHeld(0)==1&&c.requestHit(0,false,packet));
    assert(c.pendingEvents()==2&&c.score(0)==1);assert(c.stepRequests());
    assert(c.pendingEvents()==0&&c.score(0)==0&&c.coins()[0].state==CoinBattle::State::Dropped);
    assert(!c.stepRequests());
    // Actual pool: a transfer and a later damage transaction must not both
    // take the first three units. Only one body event advances per update.
    c=fresh();unsigned n=0;
    for(unsigned i=0;i<c.coins().size();++i)if(c.coins()[i].state==CoinBattle::State::Available){assert(c.collect(i,0));c.setHeldReady(i,true);++n;}
    assert(n==40&&c.requestSteal(1,0)&&c.requestHit(0,false,packet)&&c.pendingEvents()==4);
    assert(c.score(0)==40&&c.score(1)==0&&c.stepRequests());
    assert(c.pendingEvents()==2&&c.score(0)==37&&c.score(1)==3&&c.readyHeld(1)==0);
    for(unsigned i=0;i<3;++i)assert(c.coins()[i].owner==1);
    assert(c.stepRequests()&&c.pendingEvents()==0&&c.score(0)==17);
    for(unsigned i=3;i<23;++i){const auto& unit=c.coins()[i];assert(unit.state==CoinBattle::State::Dropped);
        assert(unit.body.position.x==packet.position.x&&unit.body.position.y==packet.position.y+250);}
    // Atomic capacity rejection, wraparound, no partial selection left in
    // the64-slot ring when a full transaction cannot fit.
    c=fresh();assert(c.collect(0,0));c.setHeldReady(0,true);
    for(unsigned i=0;i<31;++i)assert(c.requestHit(0,false,packet));
    assert(c.pendingEvents()==62&&!c.requestHit(0,false,packet)&&c.pendingEvents()==62);
    for(unsigned i=0;i<31;++i)assert(c.stepRequests());
    assert(c.pendingEvents()==0&&!c.stepRequests());
    assert(c.collect(1,0));c.setHeldReady(1,true);assert(c.requestSteal(1,0)&&c.stepRequests()&&c.score(1)==1);
    // Late state entry has the original .5 lag decay, not teleporting to
    // today's kart position or querying all delayed steps at offset0.
    c=fresh();assert(c.collect(0,0));c.setHeldReady(0,true);assert(c.requestHit(0,false,packet));
    for(unsigned i=0;i<4;++i)c.stepDropped(nullptr,33);
    assert(c.stepRequests());offsets.clear();
    c.stepDropped(nullptr,33,CoinBody::NoWater,timed);
    assert((offsets==std::vector<unsigned>{4}));
    c.stepDropped(nullptr,33,CoinBody::NoWater,timed);
    assert((offsets==std::vector<unsigned>{4,4}));
    c.stepDropped(nullptr,33,CoinBody::NoWater,timed);
    assert((offsets==std::vector<unsigned>{4,4,4,3}));
    for(unsigned i=0;i<10;++i)c.stepDropped(nullptr,33,CoinBody::NoWater,timed);
    assert(offsets.back()==0&&c.coins()[0].ticks==17); //4 delay updates recovered plus13 live updates
    // A unit acquired after the damage request's timestamp cannot become
    // its replacement victim merely because the older units left Held.
    c=fresh();assert(c.collect(0,0));c.setHeldReady(0,true);assert(c.requestHit(0,false,packet));
    assert(c.hit(0,false,launch)==1);c.stepDropped(nullptr,33);
    assert(c.collect(1,0));c.setHeldReady(1,true);assert(c.stepRequests());
    assert(c.score(0)==1&&c.coins()[1].state==CoinBattle::State::Held);
    c=fresh();assert(c.pendingEvents()==0&&c.collect(0,0));c.setHeldReady(0,true);
    c.finish(0);assert(!c.requestHit(0,false,packet)&&!c.requestSteal(1,0));
    std::puts("PASS actual40-coin pool: state7 eligibility, captured-time FIFO selection/transfer/drop and one body batch/update,63-slot atomic capacity/wrap, .5-lag golden timed-collision offsets/catchup, future-acquisition exclusion, finished and reset boundaries. Target rendering/physics remain separate.");
}
