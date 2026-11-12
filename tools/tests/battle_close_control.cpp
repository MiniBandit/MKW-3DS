#include <mkw/battle_close_target.hpp>
#include <mkw/battle_item_policy.hpp>
#include <mkw/battle_item_ai.hpp>
#include <mkw/battle_attack_table.hpp>
#include <mkw/battle_navigation.hpp>
#include <cassert>
#include <cstdio>

using namespace mkw;
struct Point {Vec3 position;float width=20;std::array<unsigned,4> next{};unsigned nextCount=0;};
struct Route {std::vector<Point> points;unsigned size()const{return unsigned(points.size());}const auto& point(unsigned i)const{return points[i];}};
struct View {unsigned item=20;bool held=false,roulette=false;};
int main(){
    Route route;route.points={{{0,0,0},20,{1},1},{{1000,0,0},20,{0,2,3},3},
        {{2000,0,0},20,{1},1},{{1000,1000,0},20,{1},1}};
    BattleSearchGraph graph;assert(graph.build(route));BattleSearchField field;assert(field.reset(graph,5000,6,10));
    for(unsigned i=0;i<6;++i)assert(field.update(i,{100.f+100.f*i,0,0},1000));
    std::array<bool,6> available{true,true,true,true,true,true};
    auto valid=[&](unsigned id){assert(id<6);return available[id];};
    BattleCloseTarget coin;
    auto step=[&](BattleCloseTarget& t,Vec3 p,bool switched=false,bool action=false,bool wall=false,bool enabled=true,bool again=true){
        return t.step(graph,field,0,1,switched,p,action,wall,enabled,again,valid);};
    assert(step(coin,{0,0,0},true)&&coin.object()==0);
    assert(coin.travelRemaining()==150&&coin.timeRemaining()==74);
    // Coin collection advances next tick, rather than discarding all other
    // candidates until the next route boundary.
    available[0]=false;assert(!step(coin,{0,0,0}));
    assert(step(coin,{0,0,0})&&coin.object()==1);
    assert(field.update(1,{800,0,0},1000));assert(step(coin,{0,0,0}));assert(coin.position().x==200); // cached chase position
    available[2]=false; // object1 now farther than2/3/4;2 invalid ->3
    BattleCloseTarget box;assert(step(box,{0,0,0},true,false,false,true,false));
    assert(box.object()==3);available[3]=false;
    assert(!step(box,{0,0,0},false,false,false,true,false)&&box.state()==BattleCloseTarget::State::Idle);
    assert(!step(box,{0,0,0},false,false,false,true,false));
    // Fixed five-candidate input: a sixth source is not promoted when the
    // first five are invalid, even if its world position is closer.
    available={false,false,false,false,false,true};assert(field.update(5,{1,0,0},1000));
    BattleCloseTarget bounded;assert(!step(bounded,{0,0,0},true));
    available={true,true,true,true,true,true};
    BattleCloseTarget timer;assert(step(timer,{0,0,0},true));const float left=timer.timeRemaining();
    for(unsigned i=0;i<100;++i)assert(step(timer,{0,0,0},false,true));assert(timer.timeRemaining()==left);
    for(unsigned i=0;i<unsigned(left);++i)assert(step(timer,{0,0,0}));assert(!step(timer,{0,0,0}));
    assert(timer.state()==BattleCloseTarget::State::Idle);
    BattleCloseTarget wall;assert(step(wall,{0,0,0},true));
    for(unsigned i=0;i<30;++i)assert(step(wall,{0,0,0},false,false,true));
    assert(!step(wall,{0,0,0},false,false,true));assert(wall.state()==BattleCloseTarget::State::Select);
    assert(step(wall,{10,0,0})&&wall.object()==2); // next valid nearest cached candidate
    BattleCloseTarget travel;assert(step(travel,{0,0,0},true));assert(!step(travel,{151,0,0}));
    BattleCloseTarget atTarget;assert(step(atTarget,{100,0,0},true));
    assert(atTarget.object()==0&&atTarget.travelRemaining()==0);assert(step(atTarget,{100,0,0})); // pickup validity ends it
    const float paused=atTarget.timeRemaining();assert(!step(atTarget,{0,0,0},false,false,false,false));assert(atTarget.timeRemaining()==paused);
    assert(battleSteering({0,0,100},{1,0,0},{0,0,1})==0);
    assert(std::fabs(battleSteering({100,0,0},{1,0,0},{0,0,1})+.7853982f)<1e-5f);
    assert(std::fabs(battleSteering({-100,0,0},{1,0,0},{0,0,1})-.7853982f)<1e-5f);
    assert(battleOpponentClass({0,0,3999},{0,0,1})==3);
    assert(battleOpponentClass({0,0,-3999},{0,0,1})==4);
    assert(battleOpponentClass({3999,0,0},{0,0,1})==1);
    assert(battleOpponentClass({0,4000,0},{0,0,1})==1);
    assert(battleOpponentClass({0,6000,0},{0,0,1})==2);
    assert(battleOpponentClass({0,7000,0},{0,0,1})==0);
    assert(battleAttackChoice(4,0)==1&&battleAttackChoice(4,9)==2);
    assert(battleAttackChoice(9,0)==1&&battleAttackChoice(3,9)==0);
    assert(battleAttackBranch(route,1,0,{2000,0,0},true)==1);
    assert(battleAttackBranch(route,1,0,{2000,0,0},false)==2); // height contributes
    assert(battleAttackBranch(route,1,0,{1000,500,0},true)==2);
    unsigned randomCalls=0;auto random=[&](unsigned limit){assert(limit==100);++randomCalls;return 0u;};
    // Easy waits even when an opponent is perfectly aligned. The policy
    // pauses use during stuck recovery but keeps its countdown running.
    BattleItemPolicy easy;BattleItemPolicy::Intent intent;
    for(unsigned i=0;i<17;++i){intent=easy.step(0,0,true,true,true,random);assert(!intent.use);}
    for(unsigned i=0;i<181;++i){intent=easy.step(0,0,false,true,true,random);assert(!intent.use);}
    intent=easy.step(0,0,true,true,true,random);assert(intent.use&&intent.direction==1&&randomCalls==0);
    BattleItemPolicy normal;
    for(unsigned i=0;i<17;++i)assert(!normal.step(1,2,true,true,false,random).use); // min wait15 then choose rear
    assert(randomCalls==1);assert(!normal.step(1,2,true,true,false,random).use);
    intent=normal.step(1,2,true,false,true,random);assert(intent.use&&intent.direction==2);
    BattleItemPolicy hard;
    hard.step(2,3,true,false,false,random);hard.step(2,3,true,false,false,random);
    intent=hard.step(2,3,true,true,true,random);assert(intent.use&&intent.direction==1&&randomCalls==1); // front priority
    BattleItemPolicy reactive;
    reactive.step(1,3,true,false,false,random);
    reactive.step(1,3,true,false,false,[&](unsigned){++randomCalls;return 75u;});
    intent=reactive.step(1,3,true,false,true,random);assert(intent.use&&intent.direction==2);
    // Real deployment/release sequences: three frames per triple-item tap,
    // cached held kind for policy, and delayed directional throw entry.
    BattleItemAi items;View view{16,false,false};
    assert(items.step(view,false,0).use);assert(!items.step(view,false,0).use);assert(!items.step(view,false,0).use);
    assert(items.step(view,false,0).use);view={20,true,false};
    assert(!items.step(view,false,0).use&&!items.active());
    assert(!items.step(view,false,0).use&&items.active());
    assert(!items.step(view,true,2).use&&!items.releasing());
    auto out=items.step(view,false,0);assert(out.use&&out.backwards&&items.releasing());
    out=items.step(view,false,0);assert(!out.use&&out.backwards);
    out=items.step(view,false,0);assert(!out.use&&out.backwards);
    out=items.step(view,false,0);assert(!out.use&&!out.backwards&&items.state()==BattleItemAi::State::Stock);
    view.held=false;items.step(view,false,0);items.step(view,false,0);assert(items.state()==BattleItemAi::State::Idle);
    view={5,false,false};items.step(view,false,0);assert(!items.step(view,false,0).use);
    assert(items.step(view,true,2).use&&items.state()==BattleItemAi::State::Mushroom);
    for(unsigned i=0;i<100;++i)assert(!items.step(view,false,0).use); // no race random mushroom repeat
    assert(items.step(view,true,1).use);assert(!items.step(view,true,1,true).use&&items.state()==BattleItemAi::State::Goal);
    using S=BattleNavigation::State;BattleNavigation nav;
    nav.step(1,false,false,2,false,random);nav.step(1,true,false,2,false,random);
    nav.step(1,true,false,2,false,random);assert(nav.state()==S::Attack);
    nav.requestAttack(2);nav.step(1,true,false,2,false,random);assert(nav.state()==S::Retreat&&nav.mode()==10);
    nav.step(1,false,false,2,false,random,true);nav.step(1,false,false,2,false,random,true);
    assert(nav.state()==S::Retreat);nav.step(1,false,false,2,false,random);nav.step(1,false,false,2,false,random);
    assert(nav.state()==S::Decide);
    std::puts("PASS close box/coin pickup, cached target lifetime/budgets/action pause/wall escape, attack interaction/3D branches, three-difficulty item policy and native deployment/directional-release/goal flows");
}
