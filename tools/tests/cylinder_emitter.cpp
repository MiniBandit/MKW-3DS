// Check the actual runtime emitter against independently transcribed source
// position/RNG equations (NW4R EmitterFormCylinder::Emission/EmissionSub).
#include "../../src/nw4r_ef.cpp"
#include <cassert>
#include <cstdio>
using namespace mkw;using namespace mkw::ef;
static float rnd(uint32_t& seed){seed=seed*0x343fdu+0x269ec3u;return float(seed>>16)/65536.f;}
static void near(float a,float b){assert(std::fabs(a-b)<.015f);}
int main(){
 auto sys=std::make_unique<System>();unsigned cases=0,particles=0;
 for(uint32_t seed:{0u,1u,0xdeadbeefu})for(unsigned flags:{0u,0x20000u,0x40000u,0x60000u,0x1060000u,0x3060000u})for(int count:{0,1,3}){
  sys->reset();EmitterResource res{};Emitter em;ParticleManager pm;pm.emitter=&em;pm.res=&res;em.random.seed=seed;
  em.prm.emitDiv=3;em.prm.velPowerNormalDir=1;em.prm.velPowerRadiationDir=2;em.prm.velPowerYAxis=3;
  const float p[6]={30,25,.2f,5.f,20,50};emitCylinder(*sys,em,pm,count,flags,p,30,0,nullptr);
  const bool uniform=flags&0x20000;const int n=uniform?count*count:count;
  assert(sys->liveParticles==unsigned(n));uint32_t expected=seed;Particle* particle=pm.particles.head;
  if(count){
   const float base=flags&0x40000?p[2]:2.f*3.1415927410125732f*rnd(expected);
   const float span=std::fmod(p[3]-p[2],2.f*3.1415927410125732f);
   const float delta=(p[3]-p[2])/float(span<.0001917476038215682f||span>6.282993793487549f?3:2);
   for(int ring=0;ring<(uniform?count:1);++ring){
    float angle=0;
    for(int i=0;i<count;++i){
     float radius=rnd(expected);radius=flags&0x1000000?std::sqrt(radius+.25f*.25f*(1-radius)):radius+.25f*(1-radius);
     if(!uniform)angle=(p[3]-p[2])*rnd(expected);
     const float sa=std::sin(base+angle),ca=std::cos(base+angle);
     const float height=uniform?(count<=1?0:(float(ring)/float(count-1)-.5f)*40):(2*rnd(expected)-1)*20;
     const Vec3 position{sa*radius*30,height,-ca*radius*(flags&0x2000000?30:50)};
     const Vec3 normal=normalized(Vec3{position.x,0,position.z},{}),origin=normalized(position,{});
     const Vec3 velocity=normal+origin*2+Vec3{sa,0,-ca}*3;
     assert(particle);near(particle->p.position.x,position.x);near(particle->p.position.y,position.y);near(particle->p.position.z,position.z);
     near(particle->p.velocity.x,velocity.x);near(particle->p.velocity.y,velocity.y);near(particle->p.velocity.z,velocity.z);
     rnd(expected);rnd(expected);rnd(expected); // momentum, particle seed, alpha flicker
     assert(particle->lifeSpan==30);particle=particle->next;++particles;
     if(uniform)angle+=delta;
    }
   }
  }
  assert(!particle&&em.random.seed==expected);++cases;
 }
 // Porting the form does not enlarge the particle pool; a dense uniform
 // emitter fills the existing pool and safely drops the remaining particles.
 sys->reset();EmitterResource res{};Emitter em;ParticleManager pm;pm.emitter=&em;pm.res=&res;em.prm.emitDiv=64;
 const float p[6]={30,0,0,6.283185307f,20,30};emitCylinder(*sys,em,pm,64,0x60000,p,30,0,nullptr);
 assert(sys->liveParticles==System::ParticleCapacity&&pm.particles.count==System::ParticleCapacity);
 std::printf("PASS %u cylinder source cases, %u positions/velocities/RNG states, unchanged particle-capacity saturation\n",cases,particles);
}
