#include <mkw/coin_battle.hpp>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace mkw;
static std::vector<uint8_t> read(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary);assert(file);
    return {(std::istreambuf_iterator<char>(file)),{}};
}
static CoinBody launch(void*,unsigned,unsigned i,unsigned){
    CoinBody b;b.previous=b.position={1000.f+float(i)*20.f,10000.f,1000.f};b.velocity={30.f,60.f,0};return b;
}
static CoinBody spawn(void*,const CoinBattle::Placement& p){
    CoinBody b;b.previous=b.position=p.position+Vec3{0,p.kind<2?150.f:10150.f,0};return b;
}
static ItemSurface ground(Vec3,Vec3 p,float radius){
    ItemSurface s;
    if(p.y<radius){s.correction.y=radius-p.y;s.floorNormal={0,1,0};s.floorTypes=1;}
    return s;
}
static ItemSurface wall(Vec3,Vec3 p,float radius){
    ItemSurface s;
    if(p.x+radius>2000.f){s.correction.x=2000.f-radius-p.x;s.wallNormal={-1,0,0};s.floorTypes=1u<<12;}
    return s;
}
static ItemSurface kill(Vec3,Vec3,float){ItemSurface s;s.kill=true;return s;}

int main(int argc,char** argv){
    assert(argc==2);
    const std::filesystem::path assets=argv[1];
    const auto kmg=read(assets/"common/minigame.kmg");
    constexpr const char* keys[]={"venice_battle","block_battle","casino_battle","skate_battle","sand_battle",
        "old_CookieLand_gc","old_House_ds","old_battle4_sfc","old_battle3_gba","old_matenro_64"};
    constexpr unsigned waves[]={11,9,9,10,9,11,9,9,9,9};
    // KMG +394 is an authored placement-index cutoff, not a command to
    // synthesize exactly N primary units. Keep duplicate/sparse KMP indices.
    constexpr unsigned primaries[]={42,40,28,42,28,21,30,22,30,28};
    std::array<RaceTeam,12> teams;for(unsigned i=0;i<12;++i)teams[i]=RaceTeam(i&1);
    CoinBattle battle;BattleRules rules;
    for(unsigned i=0;i<10;++i){
        assert(rules.load(kmg.data(),kmg.size(),32+i,12,BattleMode::Coin));
        assert(rules.coinWaveSize==waves[i]);
        auto kmp=read(assets/"courses"/keys[i]/"course.kmp");
        assert(battle.load(kmp.data(),kmp.size(),rules,12,teams));
        assert(battle.coins().size()==rules.maxCoins);
        battle.assignWaves();
        assert(battle.activate(0)==primaries[i]);
        assert(battle.activate(0)==0); // no duplicate spawn
        unsigned activated=primaries[i];
        for(unsigned wave=1;wave<255;++wave){const auto n=battle.activate(wave);assert(n<=rules.coinWaveSize);activated+=n;}
        assert(activated==rules.maxCoins);
        std::vector<bool> used(battle.placements().size());
        for(const auto& c:battle.coins()){assert(!used[c.placement]);used[c.placement]=true;}
        const auto before=battle.coins().size();
        assert(!battle.load(kmp.data(),kmp.size()-1,rules,12,teams));
        assert(battle.coins().size()==before); // malformed input cannot clear a live pool
        printf("arena %u: %zu candidate placements, %zu units, %u initial, wave %u\n",32+i,
            battle.placements().size(),battle.coins().size(),primaries[i],rules.coinWaveSize);
    }
    // Normal replenishment waits150 ticks; sky variants start physics now.
    assert(rules.load(kmg.data(),kmg.size(),32,12,BattleMode::Coin));
    auto timingKmp=read(assets/"courses/venice_battle/course.kmp");
    assert(battle.load(timingKmp.data(),timingKmp.size(),rules,12,teams));battle.assignWaves();
    battle.activate(0,spawn,nullptr,150);
    unsigned waiting=0;
    for(unsigned i=0;i<battle.coins().size();++i){const auto& c=battle.coins()[i];
        if(c.group)continue;
        if(battle.placements()[c.placement].kind<2){++waiting;assert(c.state==CoinBattle::State::Spawning&&!battle.collect(i,0));}
        else assert(c.state==CoinBattle::State::Available&&c.dynamic);
    }
    assert(waiting);
    for(unsigned tick=0;tick<149;++tick)battle.stepDropped(nullptr,32);
    for(const auto& c:battle.coins())if(!c.group&&battle.placements()[c.placement].kind<2)assert(c.state==CoinBattle::State::Spawning);
    battle.stepDropped(nullptr,32);
    for(const auto& c:battle.coins())if(!c.group&&battle.placements()[c.placement].kind<2)assert(c.state==CoinBattle::State::Available);
    const auto shot=CoinBody::launch({0,1000,0},{0,0,1},0,4);
    assert(shot.position.y==1250&&std::abs(length(shot.velocity)-80)<.001f&&shot.velocity.y>0);
    const auto opposite=CoinBody::launch({0,1000,0},{0,0,1},2,4);
    assert(std::abs(opposite.velocity.z+shot.velocity.z)<.001f);
    // A real arena pool drives the ownership/loss scenario.
    assert(rules.load(kmg.data(),kmg.size(),32,12,BattleMode::Coin));
    const auto kmp=read(assets/"courses/venice_battle/course.kmp");
    assert(battle.load(kmp.data(),kmp.size(),rules,12,teams));battle.assignWaves();battle.activate(0);
    unsigned collected=0;
    for(unsigned i=0;i<battle.coins().size()&&collected<20;++i)
        if(battle.collect(i,0)){++collected;assert(!battle.collect(i,1));}
    assert(collected==20&&battle.score(0)==20&&battle.takeCues()==1&&battle.takeCues()==0);
    assert(battle.steal(2,0)==0); // same team
    assert(battle.steal(1,0)==3&&battle.score(0)==17&&battle.score(1)==3);
    assert(battle.hit(0,false,launch)==9&&battle.score(0)==8); // ceil17/2
    assert(battle.hit(1,true,launch)==3&&battle.score(1)==0);
    assert(battle.hit(1,false,launch)==0&&battle.hit(12,false,launch)==0);
    assert(battle.stepWaves()==0); // 22 still-available initial coins > threshold16
    // Collect enough to reach the original low-water mark, then one wave.
    for(unsigned i=0;i<battle.coins().size()&&battle.score(2)<6;++i)battle.collect(i,2);
    assert(battle.score(2)==6&&battle.stepWaves()==11&&battle.stepWaves()==0);
    unsigned flying=0;
    for(const auto& c:battle.coins())if(c.state==CoinBattle::State::Dropped)++flying;
    assert(flying==12);
    for(unsigned tick=0;tick<30;++tick)battle.stepDropped(nullptr,32);
    unsigned airborne=0;float y=0;
    for(const auto& c:battle.coins())if(c.dynamic){++airborne;y=c.body.position.y;assert(c.state==CoinBattle::State::Available);}
    assert(airborne==12);
    battle.stepDropped(nullptr,32);
    bool moved=false;for(const auto& c:battle.coins())if(c.dynamic&&c.body.position.y!=y)moved=true;
    assert(moved); // becoming collectible must never freeze a coin in midair
    // The intentionally high launch fixture is still bouncing at tick281.
    // Allow its actual trajectory to settle rather than freezing it to meet
    // a synthetic flight/expiry deadline (the PSV shortcut).
    for(unsigned tick=0;tick<1000;++tick)battle.stepDropped(ground,32);
    for(const auto& c:battle.coins())if(c.owner==CoinBattle::NoOwner&&c.ticks){
        if(c.state!=CoinBattle::State::Available||c.dynamic)std::fprintf(stderr,
            "unsettled state=%u dynamic=%u tick=%u quiet=%u pos=%.3f,%.3f,%.3f velocity=%.3f,%.3f,%.3f\n",
            unsigned(c.state),c.dynamic,c.ticks,c.quietTicks,c.body.position.x,c.body.position.y,c.body.position.z,
            c.body.velocity.x,c.body.velocity.y,c.body.velocity.z);
        assert(c.state==CoinBattle::State::Available&&!c.dynamic);
        assert(std::abs(c.body.position.y-CoinBody::Radius)<.01f&&length(c.body.velocity)==0);
    }
    // Ground and wall normals/push are exercised independently of pickup.
    CoinBody b;b.position={1000,1000,1000};
    assert(b.step(nullptr)==CoinBody::Contact::Air);
    assert(std::abs(b.velocity.y+3.366f)<.0001f);
    b.position={1840,1000,1000};b.velocity={50,0,0};
    assert(b.step(wall)==CoinBody::Contact::Wall&&b.velocity.x<0&&b.position.x<=1850);
    b.position={1000,151,1000};b.velocity={0,-20,0};
    assert(b.step(ground)==CoinBody::Contact::Floor&&b.velocity.y>0&&b.position.y>=150);
    b.position={1000,1000,1000};assert(b.step(kill)==CoinBody::Contact::Lost);
    b.position={0,1000,0};assert(b.step(nullptr,false,36)==CoinBody::Contact::Hazard);
    b.position={1000,10000,1000};b.velocity={1000,1000,0};b.step(nullptr);
    assert(std::abs(length(b.velocity)-200)<.001f);
    puts("PASS all10 original KMP/KMG pools/waves, atomic bad-input rejection, unique ownership, collection/transfer/percentage/fall loss, continued airborne motion, floor settling, wall reflection, hazard/kill and speed limit");
}
