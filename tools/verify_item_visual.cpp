#include <mkw/item_visual.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
int main(){
    unsigned poses=0;
    for(auto kind:{ItemKind::GreenShell,ItemKind::RedShell,ItemKind::BlueShell,ItemKind::Star,ItemKind::Banana}){
        ItemActor actor;actor.kind=kind;actor.yaw=.7f;actor.up=normalized({.2f,1.f,.3f});actor.scale=2.1f;
        const auto start=releasedItemVisual(actor);
        for(unsigned age=0;age<2400;++age){
            actor.age=age;const auto pose=releasedItemVisual(actor);
            assert(std::fabs(length(pose.right)-1)<1e-5&&std::fabs(length(pose.up)-1)<1e-5&&std::fabs(length(pose.forward)-1)<1e-5);
            assert(std::fabs(dot(pose.right,pose.up))<1e-5&&std::fabs(dot(pose.right,pose.forward))<1e-5);
            assert(length(cross(pose.right,pose.up)-pose.forward)<1e-5);
            if(kind==ItemKind::Banana)assert(length(pose.forward-start.forward)<1e-5);
            ++poses;
        }
        actor.age=8;
        if(spinningShell(kind))assert(length(releasedItemVisual(actor).forward-start.forward)>.5f);
        actor.explosion=true;const auto blast=releasedItemVisual(actor);actor.age=80;
        assert(length(releasedItemVisual(actor).forward-blast.forward)<1e-5);
        assert(actor.yaw==.7f&&actor.scale==2.1f); // presentation never changes guidance or size
    }
    assert(std::fabs(shellVisualSpin(0)-shellVisualSpin(65536))<1e-6);
    std::printf("PASS %u item poses: tilted floor, spin, fixed traps, stable blast and unchanged actor state\n",poses);
}
