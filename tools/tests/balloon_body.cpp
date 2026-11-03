#include <mkw/balloon_body.hpp>
#include <cassert>
#include <cstdio>

using namespace mkw;
static bool near(float a,float b,float tolerance=1e-4f){return std::fabs(a-b)<tolerance;}
static void finite(Vec3 v){assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));}
static void basis(const BalloonBody& b){
    finite(b.anchor);finite(b.knot);finite(b.centre);finite(b.centreDelta);
    assert(near(length(b.right),1,2e-4f)&&near(length(b.up),1,2e-4f)&&near(length(b.front),1,2e-4f));
    assert(std::fabs(dot(b.right,b.up))<2e-4f&&std::fabs(dot(b.right,b.front))<2e-4f&&std::fabs(dot(b.up,b.front))<2e-4f);
}
int main(){
    BalloonAttachment p;p.height=165;p.rear=-40;
    BalloonBody b;b.init(p,1,3,4);
    assert(near(b.anchor.y,5)&&near(b.knot.y,165)&&near(b.centre.y,215));
    unsigned calls=0;auto random=[&]{++calls;return .5f;};
    const auto before=b.centre;b.step(&p,1,3,4,{},true,nullptr,0,random);
    assert(calls==0&&b.centre.y!=before.y);assert(near(b.previousCentre.y,215)); // separate publication barrier
    b.publish(4);assert(b.previousCentre.y==b.centre.y&&b.previousCentreDelta.y==b.centreDelta.y);
    b.step(&p,1,3,4,{},true,nullptr,0,random);assert(calls==3); // velocity jitter only
    BalloonBody growing;growing.init(p,1,3,.1f);calls=0;
    growing.step(&p,1,3,.2f,{},false,nullptr,0,random);assert(calls==3);
    growing.publish(.2f);growing.step(&p,1,3,.3f,{},false,nullptr,0,random);assert(calls==9); // growth+velocity three draws each
    assert(near(length(BalloonBody::stretch({0,200,0},160)),40));
    assert(near(length(BalloonBody::compress({0,30,0},50)),20));
    assert(near(length(BalloonBody::compress({},50)),0));
    // A close approaching peer adds separation and contact impulse. Neither
    // solver may change the other unit's previous-frame snapshot.
    BalloonBody peer,withPeer,alone;peer.init(p,1,3,4);withPeer.init(p,1,3,4);alone=withPeer;
    peer.previousCentre=withPeer.previousCentre-Vec3{30,0,0};peer.previousCentreDelta={1,0,0};
    const BalloonBody* peers[]={&peer};const auto peerSnapshot=peer.previousCentre;
    withPeer.step(&p,1,3,4,{},true,peers,1,random);alone.step(&p,1,3,4,{},true,nullptr,0,random);
    assert(withPeer.centre.x>alone.centre.x&&withPeer.contact.x>0);
    assert(peer.previousCentre.x==peerSnapshot.x&&peer.previousCentre.y==peerSnapshot.y);
    // Actual six standard-vehicle anchor pairs, recorded from both original
    // Disp tables. Follow motion through flat turns, stop, then detach.
    constexpr float heights[]={165,180,200,160,170,185},rears[]={-40,-50,-60,-60,-65,-65};
    for(unsigned vehicle=0;vehicle<6;++vehicle){
        p={};p.height=heights[vehicle];p.rear=rears[vehicle];
        std::array<BalloonBody,3> units;for(unsigned i=0;i<3;++i)units[i].init(p,i,3,4);
        Vec3 oldPosition{};
        for(unsigned frame=0;frame<900;++frame){
            const float angle=float(std::min(frame,600u))*.015f;
            p.position={1000*std::sin(angle),0,1000*(1-std::cos(angle))};
            p.right={std::cos(angle),0,-std::sin(angle)};p.front={std::sin(angle),0,std::cos(angle)};
            const Vec3 velocity=p.position-oldPosition;oldPosition=p.position;
            for(unsigned i=0;i<3;++i){const BalloonBody* group[]={&units[0],&units[1],&units[2]};
                units[i].step(&p,i,3,4,velocity,true,group,3,random);basis(units[i]);
                assert(length(units[i].knot-balloonAnchor(p))<600);}
            for(auto& unit:units)unit.publish(4);
        }
        for(auto& unit:units){assert(length(unit.centre-unit.knot)>20&&length(unit.centre-unit.knot)<90);}
        const auto origin=units[0].knot;
        for(unsigned frame=0;frame<90;++frame){units[0].step(nullptr,0,0,4,{},false,nullptr,0,random);basis(units[0]);units[0].publish(4);}
        assert(length(units[0].knot-origin)>0&&length(units[0].knot-origin)<10000);
    }
    // Teleport reinitialization discards previous velocity/impulse and uses
    // the new authored anchor, rather than stretching across the arena.
    p.position={10000,2000,-3000};b.init(p,1,3,4);
    assert(length(b.knotDelta)==0&&length(b.centreDelta)==0&&length(b.contact)==0);
    assert(length(b.knot-(balloonAnchor(p)+balloonFan(p,1,3)*160))<1e-3f);
    std::puts("PASS native balloon three-substep movement, separate snapshots, jitter RNG cadence, peer contact/separation, six original vehicle anchor motion/stop/free-flight sequences and teleport reset; target visuals not tested");
}
