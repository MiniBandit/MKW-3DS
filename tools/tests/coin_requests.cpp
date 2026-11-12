#include <mkw/coin_battle.hpp>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace mkw;
namespace {
std::vector<uint8_t> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);assert(f);return {(std::istreambuf_iterator<char>(f)),{}};}
CoinBody launch(void*,unsigned,unsigned i,unsigned count){return CoinBody::launch({0,1000,0},{0,0,1},i,count);}
unsigned shuffle(void*,unsigned n){return n-1;}
}
int main(int argc,char**argv){
    assert(argc==2);const std::filesystem::path assets=argv[1];
    auto kmg=read(assets/"common/minigame.kmg"),kmp=read(assets/"courses/block_battle/course.kmp");
    BattleRules rules;assert(rules.load(kmg.data(),kmg.size(),33,12,BattleMode::Coin));
    std::array<RaceTeam,12> teams;for(unsigned i=0;i<12;++i)teams[i]=i&1?RaceTeam::Blue:RaceTeam::Red;
    auto fresh=[&]{CoinBattle c;assert(c.load(kmp.data(),kmp.size(),rules,12,teams));c.assignWaves();c.activate(0);assert(c.initializePickupPriority(shuffle));return c;};
    auto forward=fresh(),reverse=fresh();unsigned available=0;
    for(unsigned c=0;c<forward.coins().size();++c){
        if(forward.coins()[c].state!=CoinBattle::State::Available)continue;++available;
        for(unsigned r=0;r<12;++r)forward.requestCollect(c,r);
        for(unsigned r=12;r--;)reverse.requestCollect(c,r);
        assert(forward.score(11)==0&&reverse.score(11)==0); // requests cannot publish ownership
        assert(forward.coins()[c].owner==CoinBattle::NoOwner);
    }
    assert(available>3&&forward.resolveCollectRequests()==available&&reverse.resolveCollectRequests()==available);
    for(unsigned r=0;r<12;++r)assert(forward.score(r)==reverse.score(r));
    for(unsigned c=0;c<forward.coins().size();++c)assert(forward.coins()[c].owner==reverse.coins()[c].owner);
    assert(forward.resolveCollectRequests()==0&&reverse.resolveCollectRequests()==0);
    auto c=fresh();assert(c.requestCollect(0,0));c.finish(0);
    assert(c.resolveCollectRequests()==0&&c.score(0)==0); // finish between callback and commit rejects request
    assert(!c.requestCollect(0,0)&&!c.collect(0,0)&&c.collectContacts(0,3)&&c.score(1)==1);
    assert(c.hit(1,false,launch)==1); // active victim remains eligible
    assert(c.collect(1,1)&&c.collect(2,1)&&c.collect(3,1));c.finish(1);
    assert(c.hit(1,false,launch)==0&&c.hit(1,true,launch)==0&&c.steal(2,1)==0&&c.score(1)==3);
    assert(c.collect(4,2)&&c.steal(1,2)==0&&c.score(2)==1); // finished thief rejected too
    c=fresh();assert(c.collect(0,0));assert(!c.requestCollect(0,1));
    assert(c.hit(0,false,launch)==1&&!c.requestCollect(0,1)); // dropped state8 not eligible
    assert(c.resolveCollectRequests()==0);c=fresh();assert(c.requestCollect(0,0)&&c.resolveCollectRequests()==1);
    std::puts("PASS actual Block Plaza pool: deferred contact publication, order-independent12-racer arbitration, no duplicate commits, finish before/after request, finished victim/thief gates, dropped exclusion and fresh-round reset. No extra damage pickup cooldown.");
}
