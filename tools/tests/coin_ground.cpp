#include <mkw/coin_ground.hpp>
#include <mkw/coin_shadow.hpp>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace mkw;
namespace {
float height=1000;unsigned queries=0;int support=0;
ItemSurface plane(Vec3,Vec3 p,float radius){
    ++queries;ItemSurface s;s.collided=p.y<height+radius;
    if(s.collided){s.correction={17,height+radius-p.y,-23};s.floorNormal={0,1,0};
        s.floorTypes=1;s.floorObject=&support;s.floorObjectY=height;}
    return s;
}
ItemSurface missing(Vec3,Vec3,float){++queries;return {};}
ItemSurface inside(Vec3,Vec3,float){++queries;ItemSurface s;s.collided=true;return s;}
CoinBody spawn(void*,const CoinBattle::Placement& p){CoinBody b;b.previous=b.position=p.position+Vec3{0,150,0};CoinGround::place(b,plane);return b;}
bool follow(void*,CoinBody& b){return CoinGround::follow(b,36,height);}
std::vector<uint8_t> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);assert(f);return {(std::istreambuf_iterator<char>(f)),{}};}
}
int main(int argc,char** argv){
    assert(argc==3);
    // Outside/inside/exact-start contact, high placements and edge cases:
    // the real helper searches and refines, then the real place appliesY only.
    for(float y:{10.f,1149.f,1150.f,1300.f,10000.f}){
        CoinBody b;b.position=b.previous={123,y,456};queries=0;
        assert(CoinGround::place(b,plane));assert(b.position.x==123&&b.position.z==456);
        assert(b.position.y==1150&&b.previous.y==1150&&b.floorObject==&support);
        assert(queries>1&&queries<1000);
        height=2500;assert(CoinGround::follow(b,36,height)&&b.position.y==2650);
        height=-50;assert(CoinGround::follow(b,36,height)&&b.position.y==100);
        assert(b.position.x==123&&b.position.z==456);
        assert(!CoinGround::follow(b,35,9999)&&b.position.y==100);height=1000;
    }
    CoinBody absent;absent.position=absent.previous={5,900,7};queries=0;
    assert(!CoinGround::place(absent,missing)&&absent.position.y==900&&queries<1000);
    const auto saturated=CoinGround::probe({0,500,0},inside);assert(saturated.iterations==1000);
    assert(!CoinGround::follow(absent,36,5000));
    // Production finite pool: resting/spawning follows without a collision
    // scan every tick; logical Held units keep their own completed pickup.
    const std::filesystem::path assets=argv[1];const auto kmg=read(assets/"common/minigame.kmg");
    const auto kmp=read(assets/"courses/sand_battle/course.kmp");BattleRules rules;
    assert(rules.load(kmg.data(),kmg.size(),36,12,BattleMode::Coin));
    std::array<RaceTeam,12> teams{};CoinBattle battle;
    assert(battle.load(kmp.data(),kmp.size(),rules,12,teams));battle.assignWaves();assert(battle.activate(0,spawn,nullptr,150));
    const auto before=battle.coins();const unsigned oldQueries=queries;height=3000;battle.followResting(follow);
    for(unsigned i=0;i<before.size();++i)if(before[i].state==CoinBattle::State::Spawning){
        const auto& b=battle.coins()[i].body;assert(b.position.y==3150);
        assert(b.position.x==before[i].body.position.x&&b.position.z==before[i].body.position.z);}
    assert(queries==oldQueries);
    for(unsigned i=0;i<150;++i)battle.stepDropped(nullptr,36);
    unsigned picked=0;while(battle.coins()[picked].state!=CoinBattle::State::Available)++picked;
    assert(battle.collect(picked,0));const auto heldY=battle.coins()[picked].body.position.y;
    height=4000;battle.followResting(follow);assert(battle.coins()[picked].body.position.y==heldY&&battle.score(0)==1);
    // Actual authored shadow-volume geometry, not a synthetic quad: keep all
    //80 triangles and vertical span, then apply the production1:4:1 scale.
    const auto model=read(argv[2]);auto u32=[&](size_t i){uint32_t n;std::memcpy(&n,model.data()+i,4);return n;};
    assert(!std::memcmp(model.data(),"GMDL",4)&&u32(4)==4);
    const auto nv=u32(8),ni=u32(12),ns=u32(16),nt=u32(20);assert(ni==240);
    const size_t start=24+ns*56+nt*64;assert(start+nv*44+ni*2==model.size());
    float low=1e9f,high=-1e9f;
    for(unsigned i=0;i<nv;++i){float y;std::memcpy(&y,model.data()+start+i*44+4,4);low=std::min(low,y);high=std::max(high,y);}
    assert(low==-100&&high==50);const auto s=CoinShadow::scale(1);assert(s.x==1&&s.y==4&&s.z==1);
    assert(150+low*s.y==-250&&150+high*s.y==350&&CoinShadow::Darkness==.5f);
    std::puts("PASS native up-escape/down-refine/placeY/negative reject/1000 budget, support translation/course gate, real Sandcone finite-pool following without collision rescan or Held motion, authored80-triangle shadow volume and1:4:1 transform; target GPU/physics pending");
}
