#include <mkw/battle_search.hpp>
#include <mkw/battle_route.hpp>
#include <mkw/battle_rules.hpp>
#include <mkw/coin_battle.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace mkw;
struct TestPoint {Vec3 position;float width=1;std::array<unsigned,12> next{};unsigned nextCount=0;};
struct TestRoute {std::vector<TestPoint> points;unsigned size()const{return unsigned(points.size());}const auto& point(unsigned i)const{return points[i];}};
static std::vector<uint8_t> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);assert(f);return {(std::istreambuf_iterator<char>(f)),{}};}
static unsigned be16(const uint8_t* p){return unsigned(p[0])<<8|p[1];}
static unsigned be32(const uint8_t* p){return unsigned(p[0])<<24|unsigned(p[1])<<16|unsigned(p[2])<<8|p[3];}
static float beFloat(const uint8_t* p){unsigned v=be32(p);float f;std::memcpy(&f,&v,4);return f;}
static TestRoute load(const std::vector<uint8_t>& d){
    const uint8_t *points=nullptr,*paths=nullptr;
    for(unsigned i=0;i<be16(d.data()+8);++i){const auto* p=d.data()+be16(d.data()+10)+be32(d.data()+16+i*4);
        if(!std::memcmp(p,"ENPT",4))points=p;if(!std::memcmp(p,"ENPH",4))paths=p;}
    assert(points&&paths);TestRoute r;r.points.resize(be16(points+4));
    for(unsigned i=0;i<r.size();++i){const auto* p=points+8+i*20;auto& q=r.points[i];q.position={beFloat(p),beFloat(p+4),beFloat(p+8)};q.width=beFloat(p+12);
        assert(battleRouteSuccessors(paths+8,be16(paths+4),r.size(),i,q.next,q.nextCount));}
    return r;
}
int main(int argc,char** argv){
    assert(argc==2);
    TestRoute route;route.points={{{0,0,0},1,{1},1},{{1000,0,0},1,{0,2,3},3},{{2000,0,0},1,{1},1},{{1000,0,1000},1,{1},1}};
    BattleSearchGraph graph;assert(graph.build(route)&&graph.edges.size()==3);
    assert(graph.edge(0,1)==graph.edge(1,0)&&graph.edge(1,3)==2);
    assert((graph.edges[0].adjacent==std::vector<uint16_t>{1,2}));
    BattleSearchField field;assert(field.reset(graph,3000,2,2));
    assert(field.update(0,{500,0,0},1000));
    assert(field.weight(0)==1000&&field.weight(1)==444&&field.weight(2)==444);
    assert(field.choose(1,0,true)==1&&field.choose(1,2,true)==0); // exact-score ties retain authored order
    assert(field.choose(1,2,false)==2);
    assert(field.update(0,{1500,0,0},1000));
    assert(field.weight(0)==444&&field.weight(1)==1000&&field.weight(2)==444);
    assert(field.update(1,{500,0,0},1000));
    assert(field.weight(0)==1444&&field.weight(1)==1444&&field.weight(2)==888);
    field.remove(0);assert(field.weight(0)==1000&&field.weight(1)==444&&field.weight(2)==444);
    assert(!field.object(0)->active&&field.object(1)->active);
    // Full3D reach prevents attaching targets on an overlapping upper floor.
    assert(!field.update(1,{500,1000,0},1000));
    assert(field.weight(0)==0&&field.weight(1)==0&&field.weight(2)==0);
    field.clear();assert(!field.ready()&&field.size()==0);
    const auto assets=std::filesystem::path(argv[1]);const auto kmg=read(assets/"common/minigame.kmg");
    constexpr const char* keys[]={"venice_battle","block_battle","casino_battle","skate_battle","sand_battle",
        "old_CookieLand_gc","old_House_ds","old_battle4_sfc","old_battle3_gba","old_matenro_64"};
    const std::array<RaceTeam,12> teams{};
    for(unsigned i=0;i<10;++i){
        const auto kmp=read(assets/"courses"/keys[i]/"course.kmp");auto r=load(kmp);assert(graph.build(r));
        const float radius=battleSearchRadius(32+i);assert(radius>0&&field.reset(graph,radius,128,10));
        BattleRules rules;assert(rules.load(kmg.data(),kmg.size(),32+i,12,BattleMode::Coin));
        CoinBattle coins;assert(coins.load(kmp.data(),kmp.size(),rules,12,teams));
        unsigned grounded=0,attached=0;
        for(const auto& p:coins.placements())if(p.kind<2&&grounded<128){
            if(field.update(grounded,p.position+Vec3{0,150,0},1000))++attached;++grounded;}
        assert(grounded&&attached);int total=0;
        for(unsigned e=0;e<graph.edges.size();++e){assert(field.weight(e)>=0);total+=field.weight(e);
            for(auto next:graph.edges[e].adjacent)assert(next<graph.edges.size());}
        assert(total>0);std::printf("arena %u: %zu graph edges, %.0f radius, %u/%u ground placements attach to actual route\n",32+i,graph.edges.size(),radius,attached,grounded);
    }
    std::puts("PASS native edge influence, falloff, removal/stable target identity, fork ties, height separation; ten original arena graphs/coin placements");
}
