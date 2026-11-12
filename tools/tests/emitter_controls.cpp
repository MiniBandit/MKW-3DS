// EGG SetLife/SetEmitRatio and ExpParticleManager::Calc (original DOL
// 8022302c,802230ec,80226058,80225f84); exercises the real NW4R port.
#include "../../src/nw4r_ef.cpp"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace mkw::ef;
int main(){
 auto s=std::make_unique<System>();s->reset();EmitterResource r{};
 r.desc.commonFlag=EmitterDesc::Relocated|EmitterDesc::MaxLife;r.desc.emitFlag=9;
 r.desc.emit=1;r.desc.ptclLife=60;r.desc.scale={1,1,1};r.desc.emitDiv=1;
 r.ptcl.texture[0]=r.ptcl.texture[1]=r.ptcl.texture[2]=0xffff;
 auto* e=s->createEffect(r);assert(e&&e->emitters.count==1);auto* em=e->emitters.head;auto* pm=em->managers.head;assert(pm);
 s->setEmissionRatio(*e,2);s->setParticleLife(*e,3);s->calc();
 assert(pm->particles.count==2);auto* old=pm->particles.head;assert(old->tick==1&&old->lifeSpan==3);
 s->setEmissionRatio(*e,0);s->setParticleLife(*e,7);s->calc();
 assert(pm->particles.count==2&&old->tick==2&&old->lifeSpan==3); // never reset old lifetime
 s->setEmissionRatio(*e,1);s->calc();assert(pm->particles.count==3);
 assert(old->tick==3&&old->lifeSpan==3);auto* born=pm->particles.tail;assert(born->tick==1&&born->lifeSpan==7);
 s->setEmissionRatio(*e,0);s->calc();assert(pm->particles.count==1&&born->tick==2&&born->lifeSpan==7);
 s->setEmissionRatio(*e,1);s->setParticleLife(*e,0);s->calc();assert(pm->particles.count==2&&pm->particles.tail->lifeSpan==0);
 s->setEmissionRatio(*e,0);s->calc();assert(pm->particles.count==1); // original zero is not clamped to 1
 s->retireEffect(*e);s->garbageCollection();s->reset();e=s->createEffect(r);assert(e);pm=e->emitters.head->managers.head;
 assert(!pm->overrideLife);s->calc();assert(pm->particles.head->lifeSpan==60);
 // EGG controls enumerate current managers. A manager created afterward
 // has its original descriptor until the next explicit setter call.
 s->setParticleLife(*e,4);s->setEmissionRatio(*e,2);s->setParticleScale(*e,.7f,.8f);
 assert(pm->modifierScale[0]==.7f&&pm->modifierScale[1]==.8f);
 auto* child=effectCreateEmitter(*s,*e,r,128,0);assert(child);auto* late=child->managers.head;
 assert(late&&!late->overrideLife&&late->particleLife==0&&child->prm.emitRatio==1);
 assert(late->modifierScale[0]==1&&late->modifierScale[1]==1);
 s->calc();assert(late->particles.head->lifeSpan==60);
 puts("PASS real EF emission count, first-calculation lifetime override, unchanged older waves, zero life, reset and descriptor defaults");
}
