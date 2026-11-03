// Real NW4R draw output against the retail closed three-point B-spline.
#include <mkw/race_effects.hpp>
#include <mkw/nw4r_ef_draw.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
using namespace mkw;
namespace mkw::pack {bool read(const char*,std::vector<uint8_t>&){return false;}}
struct Point {Vec3 pos;float s,t;};
static void near(float a,float b){if(std::fabs(a-b)>.0002f){std::fprintf(stderr,"mismatch %g %g\n",a,b);assert(false);}}
int main(int argc,char**argv){
 assert(argc==2);auto fx=std::make_unique<RaceEffects>();assert(fx->load(argv[1]));
 const int id=fx->find("rk_niseBoxBiri");assert(id>=0);
 auto&sys=fx->system();auto original=sys.resources[id];
 std::vector<ef::Material> materials(sys.resources.size());
 for(unsigned i=0;i<materials.size();++i)assert(ef::translateMaterial(sys.resources[i].desc.draw,materials[i]));
 std::vector<ef::TextureSize> sizes;for(const auto&t:fx->textures)sizes.push_back({t.width,t.height,t.paddedWidth,t.paddedHeight});
 unsigned comparisons=0,cases=0;
 for(bool reverse:{false,true})for(unsigned divisions:{0u,1u,2u,5u})for(unsigned count:{1u,2u,3u,7u}){
  fx->reset();auto&r=sys.resources[id];r=original;
  r.desc.emit=float(count);r.desc.emitLife=1;r.desc.emitStart=r.desc.emitPast=0;r.desc.emitInterval=0;
  r.desc.draw.typeOption1=divisions;r.desc.draw.pivotX=0;
  if(reverse)r.desc.draw.flags|=ef::DrawSetting::DrawOrder;else r.desc.draw.flags&=~ef::DrawSetting::DrawOrder;
  auto*effect=sys.createEffect(r);assert(effect);fx->step();
  auto*pm=effect->drawHead;assert(pm);pm->modifierScale[0]=pm->modifierScale[1]=1;pm->modifierRotate={};
  std::vector<ef::Particle*> particles;
  for(auto*p=pm->particles.head;p;p=p->next)if(p->life==ef::Active)particles.push_back(p);
  assert(particles.size()==count);
  for(unsigned i=0;i<count;++i){auto&p=*particles[i];
   p.p.position={float(i*i+1),float(i%3)*4,-100.f-float(i*2)};p.p.prevPosition=p.p.position;
   p.p.size[0]=2;p.p.scale[0]=1;p.p.rotate[0]=p.p.rotate[1]=p.p.rotate[2]=0;
   p.p.rotateOffset[0]=p.p.rotateOffset[1]=p.p.rotateOffset[2]=0;
   p.p.textureScale[0][0]=p.p.textureScale[0][1]=1;p.p.textureRotate[0]=0;
   p.p.textureTranslate[0][0]=p.p.textureTranslate[0][1]=0;p.p.textureWrap=0;p.p.textureReverse=0;
  }
  // DrawOrder=0 goes oldest to youngest; references are sampled in that order.
  if(!reverse)std::reverse(particles.begin(),particles.end());
  ef::DrawList draw;ef::buildDrawList(sys,materials,sizes.data(),sizes.size(),ef::Mtx34::identity(),draw);
  if(count<3){assert(draw.vertices.empty());++cases;continue;}
  std::vector<Point> strip;
  const unsigned div=std::max(divisions,1u);
  // Independent quadratic Bernstein weights, equivalent to the original
  // DrawSection_B: half of the outer basis and the remaining central basis.
  for(unsigned section=0;section<count;++section)for(unsigned step=0;step<div;++step){
   const double u=double(step)/div,w0=.5*(1-u)*(1-u),w2=.5*u*u,w1=1-w0-w2;
   Vec3 center=particles[section%count]->p.position*float(w0)+particles[(section+1)%count]->p.position*float(w1)+particles[(section+2)%count]->p.position*float(w2);
   const float t=float((double(section)+u)/count);
   strip.push_back({center+Vec3{2,0,0},1,reverse?1-t:t});strip.push_back({center-Vec3{2,0,0},0,reverse?1-t:t});
  }
  const Vec3 end=(particles[0]->p.position+particles[1]->p.position)*.5f;
  strip.push_back({end+Vec3{2,0,0},1,reverse?0.f:1.f});strip.push_back({end-Vec3{2,0,0},0,reverse?0.f:1.f});
  std::vector<Point> triangles;
  for(unsigned i=2;i<strip.size();++i){triangles.push_back(strip[i&1?i-1:i-2]);triangles.push_back(strip[i&1?i-2:i-1]);triangles.push_back(strip[i]);}
  assert(draw.vertices.size()==triangles.size());
  const auto texture=fx->textures[pm->res->ptcl.texture[0]];
  for(unsigned i=0;i<triangles.size();++i){const auto&a=draw.vertices[i];const auto&b=triangles[i];
   near(a.x,b.pos.x);near(a.y,b.pos.y);near(a.z,b.pos.z);
   near(a.s0,b.s*float(texture.width)/texture.paddedWidth);near(a.t0,1-b.t*float(texture.height)/texture.paddedHeight);comparisons+=5;
  }
  for(unsigned limit:{0u,1u,2u,3u,7u,24u,99u}){
   ef::DrawList limited;ef::buildDrawList(sys,materials,sizes.data(),sizes.size(),ef::Mtx34::identity(),limited,limit);
   assert(limited.vertices.size()==ef::triangleVertexCount(draw.vertices.size(),limit));
   assert(!std::memcmp(limited.vertices.data(),draw.vertices.data(),limited.vertices.size()*sizeof(ef::DrawVertex)));
  }
  ++cases;
 }
 fx->reset();sys.resources[id]=original;fx->burst(id,{});bool seen=false;
 for(unsigned tick=0;tick<180;++tick){fx->step();ef::DrawList out;
  ef::buildDrawList(sys,materials,sizes.data(),sizes.size(),ef::Mtx34::identity(),out);
  seen|=!out.vertices.empty();for(const auto&v:out.vertices)assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
 }
 assert(seen&&fx->liveParticles()==0&&sys.effects.used()==0&&sys.emitters.used()==0&&sys.managers.used()==0);
 printf("PASS %u short/closed/reverse/subdivision cases, %u original position/UV values, bounded triangle prefixes, real electrical particles and pool teardown\n",cases,comparisons);
}
