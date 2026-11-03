// Exercise the actual form with retail fireworks dimensions and a fixed
// source-equation fixture, then check hollow-volume and face-only bounds.
#include "../../src/nw4r_ef.cpp"
#include <cassert>
#include <cstdio>
using namespace mkw;using namespace mkw::ef;
int main(){
 auto system=std::make_unique<System>();
 for(float inner:{0.f,25.f,100.f}){
  system->reset();EmitterResource resource{};Emitter emitter;ParticleManager manager;
  manager.emitter=&emitter;manager.res=&resource;emitter.random.seed=0;
  emitter.prm.velPowerNormalDir=1;
  const float params[]={18000.f,1.f,5800.f,inner,0.f,0.f};
  emitCube(*system,emitter,manager,256,0x1040005,params,30,0,nullptr);
  assert(system->liveParticles==256);
  if(inner==0.f){
   const auto p=manager.particles.head->p.position;
   // USA 8003b840..8003b954, seed 0: four LCG draws select the
   // Y slab, then X/Y/Z. Values calculated separately from runtime.
   assert(std::fabs(p.x-(-13759.82666015625f))<.01f);
   assert(std::fabs(p.y-.3518741571044922f)<.00001f);
   assert(std::fabs(p.z-(-5368.646240234375f))<.01f);
  }
  unsigned faces=0;
  for(auto* particle=manager.particles.head;particle;particle=particle->next){
   const auto p=particle->p.position;
   const float x=std::fabs(p.x/18000),y=std::fabs(p.y),z=std::fabs(p.z/5800);
   assert(x<=1&&y<=1&&z<=1);
   assert(std::max({x,y,z})+0.00001f>=inner*.01f);
   const auto v=particle->p.velocity;
   assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
   assert(std::fabs(dot(v,v)-1.f)<.0001f);
   if(inner==100.f){
    assert(x==1||y==1||z==1);
    faces|=x==1?(p.x>0?1:2):y==1?(p.y>0?4:8):(p.z>0?16:32);
   }
  }
  if(inner==100.f)assert(faces==63);
 }
 std::puts("PASS retail cube fixture, hollow-volume bounds, all six faces and finite unit normals");
}
