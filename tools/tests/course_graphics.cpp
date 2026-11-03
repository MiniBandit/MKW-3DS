#include <mkw/lakitu_goal.hpp>
#include <mkw/normal_matrix.hpp>
#include <mkw/kart_item_effects.hpp>
#include <cassert>
#include <cstdio>
using namespace mkw;
struct Sink:KartFxSink{
 bool live[KartFxCount]{};unsigned created=0;EffectFrame frame{};
 bool alive(KartFx f)const override{return live[unsigned(f)];}
 void create(KartFx f,const EffectFrame& p)override{live[unsigned(f)]=true;++created;frame=p;}
 void setFrame(KartFx,const EffectFrame&)override{}
 void followFade(KartFx)override{}
 void kill(KartFx f)override{live[unsigned(f)]=false;}
 void setColor(KartFx,const uint8_t*)override{}
};
Vec3 mul(const Vec3 (&r)[3],Vec3 p){return {dot(r[0],p),dot(r[1],p),dot(r[2],p)};}
int main(){
 // Finish-follow remains near a racer after driving kilometres and turning the
 // goal camera; the vertical component of camera direction never moves it away.
 for(Vec3 kart: {Vec3{},Vec3{10000,40,-20000}})for(Vec3 view:{Vec3{0,-20,10},Vec3{-10,40,0},Vec3{0,10,0}}){
  const Vec3 p=lakituGoalAnchor(kart,view,120),delta=p-kart;
  assert(std::fabs(length({delta.x,0,delta.z})-700)<.001f&&std::fabs(delta.y-120)<.001f);
 }
 // Normals must stay orthogonal to transformed tangents under nonuniform
 // scaling, shear, rotation and a reflected axis (not just uniform models).
 for(float sign:{-1.f,1.f}){
  Vec3 a[3]={{2*sign,1,0},{0,3,1},{1,0,4}},n[3];normalMatrix(a,n);
  Vec3 tangent0={1,-1,0},tangent1={1,0,-1},normal=normalized(cross(tangent0,tangent1));
  const Vec3 out=normalized(mul(n,normal));
  assert(std::fabs(dot(out,mul(a,tangent0)))<1e-5f);
  assert(std::fabs(dot(out,mul(a,tangent1)))<1e-5f);
 }
 // One collision burst at the actual course-object contact; persistent contact
 // is rate-limited, and respawn/ghost/remote racers cannot create local bursts.
 for(unsigned gate=0;gate<4;++gate){
  KartItemEffects fx;fx.reset(gate==2?1:0,gate!=2,gate==3);Sink sink;KartFxInput in;
  for(int i=0;i<40;++i)fx.step(in,sink);
  in.objectHit=true;in.objectContact={20,30,40};in.beforeRespawn=gate==1;
  fx.step(in,sink);assert(sink.created==(gate==0?1:0));
  if(gate==0){
   assert(sink.frame.origin.x==20&&sink.frame.origin.y==30&&sink.frame.origin.z==40);
   for(int i=0;i<20;++i)fx.step(in,sink);assert(sink.created==1);
   for(int i=0;i<20;++i)fx.step(in,sink);assert(sink.created==2);
  }
 }
 puts("PASS moving finish anchor, inverse-transpose normal orthogonality, object-contact effect location/gates/retrigger");
}
