#include <mkw/battle_navigation.hpp>
#include <mkw/battle_attack_table.hpp>
#include <mkw/battle_search.hpp>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace mkw;
struct Point {Vec3 position;float width=20;std::array<unsigned,4> next{};unsigned nextCount=0;};
struct Route {std::vector<Point> points;unsigned size()const{return unsigned(points.size());}const auto& point(unsigned i)const{return points[i];}};
int main(){
    unsigned calls=0,draw=0;
    auto direction=[&](unsigned n){assert(n==4);++calls;return draw;};
    for(draw=0;draw<4;++draw)assert(battleTrickDirection(false,true,false,false,direction)==draw+1);
    assert(calls==4);
    for(unsigned i=0;i<100;++i)assert(battleTrickDirection(true,true,false,false,direction)==1);
    for(bool bike:{false,true}){
        assert(!battleTrickDirection(bike,false,false,false,direction));
        assert(!battleTrickDirection(bike,true,true,false,direction));
        assert(!battleTrickDirection(bike,true,false,true,direction));
    }
    assert(calls==4); // no Battle percentage roll, no random bike direction
    Route route;route.points={{{0,0,0},20,{1},1},{{1000,0,0},20,{0,2,3},3},
        {{2000,0,0},20,{1},1},{{1000,1000,0},20,{1},1}};
    BattleSearchGraph graph;assert(graph.build(route));BattleSearchField field;
    assert(field.reset(graph,5000,1,10)&&field.update(0,{0,0,0},1000));
    for(unsigned difficulty=0;difficulty<3;++difficulty)for(bool majority:{false,true}){
        BattleNavigation nav;
        auto random=[&](unsigned n){assert(n==100);++calls;return 0u;};
        auto step=[&](bool item,bool releasing=false){nav.step(0,item,false,difficulty,majority,random,releasing);};
        step(false);step(false);step(true);step(true);assert(nav.state()==BattleNavigation::State::Attack);
        assert(nav.excludePrevious());nav.requestAttack(1);step(true);
        assert(nav.state()==BattleNavigation::State::Approach&&!nav.excludePrevious());
        // The previous point is the closest destination. Approach must be
        // allowed to return there, rather than silently keeping the old gate.
        assert(battleAttackBranch(route,1,0,{0,0,0},true,nav.excludePrevious())==0);
        assert(battleAttackBranch(route,1,0,{0,0,0},true,true)!=0);
        assert(field.choose(1,0,true,nav.excludePrevious())==0);
        step(false,true);assert(nav.state()==BattleNavigation::State::Approach&&!nav.excludePrevious());
        step(false);assert(!nav.excludePrevious());step(false);
        assert(nav.state()==BattleNavigation::State::Decide&&nav.excludePrevious());
        step(true);step(true);assert(nav.state()==BattleNavigation::State::Attack);
        nav.requestAttack(2);step(true);
        assert(nav.state()==BattleNavigation::State::Retreat&&nav.excludePrevious());
        assert(battleAttackBranch(route,1,0,{0,0,0},false,nav.excludePrevious())==1);
        nav.reset();assert(nav.excludePrevious());
    }
    std::puts("PASS Battle car four-direction table/bike Up/no percentage RNG, native takeoff gates, queued Approach enter/exit Path21 incoming-edge policy across all difficulties/teams, release hold, restored Retreat/reset, production distance/influence selectors; target control timing pending");
}
