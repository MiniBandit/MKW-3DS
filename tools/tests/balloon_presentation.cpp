#include <mkw/balloon_presentation.hpp>
#include <mkw/balloon_battle.hpp>
#include <cassert>
#include <cstdio>

using namespace mkw;
static bool near(float a,float b){return std::fabs(a-b)<1e-4f;}
int main(){
    const auto teams=offlineVsTeams(true,RaceTeam::Red);
    BalloonPresentation p;p.reset(12,teams);assert(p.size()==120);
    for(unsigned i=0;i<12;++i){assert(p.count(i)==3);
        for(unsigned j=0;j<3;++j){const auto& u=p.unit(p.slot(i,j));assert(u.owner==i&&u.slot==j&&u.color==unsigned(teams[i]));}}
    for(unsigned frame=0;frame<120;++frame)p.step();
    const unsigned a=p.slot(0,0),b=p.slot(0,1),c=p.slot(0,2);
    assert(p.unit(a).state==BalloonPresentation::State::Hidden);
    p.step();assert(p.unit(a).state==BalloonPresentation::State::Growing&&near(p.unit(a).scale,.1f));
    assert(p.unit(b).state==BalloonPresentation::State::Hidden);
    while(p.tick()<140)p.step();p.step();
    assert(near(p.unit(a).scale,2.05f)&&near(p.unit(b).scale,.1f));
    while(p.tick()<159)p.step();p.step();assert(near(p.unit(a).scale,3.9025f));
    p.step();assert(p.unit(a).state==BalloonPresentation::State::Attached&&near(p.unit(a).scale,4));
    assert(near(p.unit(b).scale,2.05f)&&near(p.unit(c).scale,.1f));
    while(p.tick()<201)p.step();
    // A stolen blue balloon belongs to red immediately, but changes its
    // visual owner only after the original10-tick transfer delay.
    const unsigned stolen=p.slot(1,2),due=p.tick()+10;
    assert(p.steal(0,1)&&p.count(0)==4&&p.count(1)==2);
    assert(p.unit(stolen).owner==0&&p.unit(stolen).color==1);
    while(p.tick()<due){p.step();assert(p.unit(stolen).visualOwner==1);}
    p.step();assert(p.unit(stolen).visualOwner==0&&p.unit(stolen).visualSlot==3);
    assert(p.steal(0,1)&&p.count(0)==5&&p.count(1)==1);
    const unsigned lastVictim=p.slot(1,0);assert(p.steal(0,1)&&p.count(0)==5&&p.count(1)==0);
    assert(p.unit(lastVictim).owner==BalloonPresentation::Invalid);
    assert(!p.steal(0,1)&&!p.steal(2,0)); // empty victim, same team
    while(p.count(0))assert(p.lose(0));assert(!p.lose(0));
    const unsigned refillStart=p.tick();p.replenish(0);assert(p.count(0)==3);
    for(unsigned j=0;j<3;++j){const auto& u=p.unit(p.slot(0,j));assert(u.color==0&&u.owner==0);}
    p.step();assert(p.unit(p.slot(0,0)).state==BalloonPresentation::State::Growing);
    while(p.tick()<refillStart+20)p.step();p.step();assert(p.unit(p.slot(0,1)).state==BalloonPresentation::State::Growing);
    // Repeated legal logical hits/refills/transfers must keep the visual
    // manager in sync without losing a slot or changing a stolen colour.
    BalloonBattle logical;BattleRules rules;logical.configure(rules,teams);logical.reset(12);p.reset(12,teams);
    for(unsigned frame=0;frame<2400;++frame){
        if(frame>200&&frame%151==0){unsigned victim=(frame/151)%12,attacker=victim^1;
            if(logical.balloons(victim)&&logical.balloons(attacker)){
                assert(logical.stealBalloon(attacker,victim));assert(p.steal(attacker,victim));}
            if(!logical.balloons(victim)){assert(logical.replenish(victim));p.replenish(victim);}
        }
        if(frame>200&&frame%167==0){unsigned victim=(frame/167)%12;
            if(logical.loseBalloon(victim,12))assert(p.lose(victim));
            if(!logical.balloons(victim)){assert(logical.replenish(victim));p.replenish(victim);}
        }
        p.step();std::array<bool,120> owned{};
        for(unsigned i=0;i<12;++i){assert(p.count(i)==logical.balloons(i));
            for(unsigned j=0;j<p.count(i);++j){const unsigned index=p.slot(i,j);assert(index<p.size()&&!owned[index]);owned[index]=true;
                assert(p.unit(index).owner==i&&p.unit(index).slot==j);}}
    }
    BalloonAttachment pose;pose.position={100,200,300};pose.height=80;pose.rear=-40;
    auto anchor=balloonAnchor(pose);assert(near(anchor.x,100)&&near(anchor.y,120)&&near(anchor.z,260));
    const auto centre=balloonFan(pose,1,3);assert(near(centre.x,0)&&near(centre.y,1)&&near(centre.z,0));
    const auto left=balloonFan(pose,0,3),right=balloonFan(pose,2,3);
    assert(near(left.x,-right.x)&&near(left.y,right.y)&&near(length(left),1));
    // Vehicle tilt is cancelled around world-up x body-up, as in native
    // fan placement; its attachment anchor still follows the body pose.
    pose.up={.6f,.8f,0};pose.right={.8f,-.6f,0};
    const auto corrected=balloonFan(pose,1,3);assert(near(corrected.x,0)&&near(corrected.y,1));
    pose={};pose.height=80;pose.rear=-40;pose.scale={1.7f,1.7f,1.7f};
    anchor=balloonAnchor(pose);assert(near(anchor.y,-24)&&near(anchor.z,-68));
    pose.scale={.5f,.5f,.5f};anchor=balloonAnchor(pose);assert(near(anchor.y,-80)&&near(anchor.z,-40));
    pose.front={0,.6f,.8f};anchor=balloonAnchor(pose);assert(near(anchor.y,-44)&&near(anchor.z,-32));
    // Ordinary release waits for an unfinished inflation and its attached
    // follower; a fresh root attachment can interrupt pending state links.
    using S=BalloonPresentation::State;using Q=BalloonPresentation::Sequence;
    Q q;BalloonPresentation::Command command;
    assert(q.push({0,0,0,S::Growing,true},true));assert(q.push({Q::Immediate,0,0,S::Attached,true}));
    assert(q.take(0,command)&&command.state==S::Growing);
    assert(q.push({0,255,0,S::Released,true}));assert(!q.take(1,command));
    q.done=true;assert(q.take(40,command)&&command.state==S::Attached);
    q.done=true;assert(q.take(41,command)&&command.state==S::Released);
    assert(q.push({60,1,0,S::Growing,true},true));assert(q.push({Q::Immediate,1,0,S::Attached,true}));
    assert(!q.take(59,command));assert(q.take(60,command)&&command.state==S::Growing&&command.owner==1);
    assert(!q.push({70,1,0,S::Hidden,false})); // Attached->Hidden is not an authored transition.
    // A stolen unit remains state3 (native Change calc passes clamp=false),
    // and can accept another transfer or release without a settle gate.
    p.reset(2,teams);while(p.tick()<201)p.step();
    const unsigned moved=p.slot(1,2);assert(p.steal(0,1));
    while(p.tick()<500)p.step();assert(p.unit(moved).state==S::Moving&&p.unit(moved).sequence.done);
    assert(p.steal(1,0));while(p.tick()<520)p.step();assert(p.unit(moved).visualOwner==1);
    // Fall/refill byte0=0,byte1=1: burst on entry, hide in the same tick,
    // no release sound or detached body calculations. Hit1/1 instead gets
    // its release cue on entry,90 body updates, then exactly one pop.
    auto exerciseRelease=[&](bool audible){
        p.reset(2,teams);while(p.tick()<201)p.step();const unsigned lost=p.slot(0,2);
        assert(p.lose(0,audible));unsigned pops=0,releaseEnters=0,releaseCalcs=0,hiddenEnters=0;
        auto observe=[&](unsigned i,BalloonPresentation::Event e,const BalloonPresentation::Command& command){
            if(i!=lost)return;
            if(e==BalloonPresentation::Event::Pop){++pops;assert(command.pop);}
            if(e==BalloonPresentation::Event::Enter&&command.state==S::Released){++releaseEnters;assert(command.audible==audible);}
            if(e==BalloonPresentation::Event::Enter&&command.state==S::Hidden)++hiddenEnters;
            if(e==BalloonPresentation::Event::Calculate&&p.unit(i).state==S::Released)++releaseCalcs;
        };
        p.step(observe);
        if(!audible)assert(p.unit(lost).state==S::Hidden&&pops==1&&hiddenEnters==1&&releaseCalcs==0);
        else{assert(releaseEnters==1&&pops==0&&releaseCalcs==1);
            for(unsigned j=1;j<90;++j)p.step(observe);
            assert(pops==1&&releaseCalcs==90);p.step(observe);assert(hiddenEnters==1);}
        for(unsigned j=0;j<110;++j)p.step(observe);assert(pops==1&&releaseEnters==1);
    };
    exerciseRelease(false);exerciseRelease(true);
    // A hit authored during inflation waits for completion, then retains
    // its authored date. The .1 fractional clock catches up gradually;
    // it does not restart at the arrival frame or skip its body callbacks.
    p.reset(2,teams);while(p.tick()<130)p.step();const unsigned late=p.slot(0,0);
    p.unit(late).sequence.push({130,255,0,S::Released,true});
    while(p.tick()<161)p.step();
    assert(p.unit(late).state==S::Released&&p.unit(late).age==1&&p.unit(late).calcTick==131);
    assert(near(p.unit(late).lag,30.9f));
    const auto oldClock=p.unit(late).calcTick;
    for(unsigned i=0;i<20;++i)p.step();
    assert(p.unit(late).calcTick>oldClock+20&&p.unit(late).age==p.unit(late).calcTick-130);
    p.clear();assert(p.size()==0&&p.count(0)==0&&p.tick()==0);
    std::puts("PASS original120/20/40 balloon attachment clocks, five-slot ownership/colour-preserving transfers/full-thief loss, logical hit/refill integration native rest anchors/fan/mega conditions, delayed-event fractional catch-up, same-tick silent pop,90 release updates and persistent stolen state3; target physics/FX/audio not covered");
}
