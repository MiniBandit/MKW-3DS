// Cataquack lock-on rk_poihanaExclm: the cooked material translates, the PICA
// program equals the retail GX TEV, and the effect produces draw batches.
// Argument: the GCN Peach Beach race effect bank (race-effects-16.bin).
#include <mkw/race_effects.hpp>
#include <mkw/nw4r_ef_draw.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
using namespace mkw;
namespace mkw::pack {bool read(const char*,std::vector<uint8_t>&){return false;}}
using Color=std::array<float,4>;
static float unit(float v){return std::clamp(v,0.f,1.f);}
static Color run(const ef::Material& m,const std::array<Color,7>& reg,const Color tex[2]){
 auto value=[&](uint8_t sel,unsigned c){return sel<7?reg[sel][c]:sel==ef::ValHalf?.5f:sel==ef::ValOne?1.f:0.f;};
 auto channel=[&](ef::Channel ch){Color o;for(unsigned k=0;k<3;++k)o[k]=value(ch.rgb,ch.rgbFromAlpha?3:k);o[3]=value(ch.alpha,3);return o;};
 const Color vertex=channel(m.vertex),lookup=channel(m.lookup);Color prev{};
 for(unsigned s=0;s<m.stages;++s){
  const auto& st=m.stage[s];const Color constant=channel(st.constant);Color out{};
  assert(!st.bufferRgb&&!st.bufferAlpha);
  for(unsigned c=0;c<4;++c){
   float a[3];
   for(unsigned k=0;k<3;++k){
    const auto source=c==3?st.alphaSrc[k]:st.rgbSrc[k];const auto op=c==3?st.alphaOp[k]:st.rgbOp[k];
    const Color* color=&prev;
    switch(source){case ef::Src::Vertex:color=&vertex;break;case ef::Src::Lookup:color=&lookup;break;
     case ef::Src::Tex0:color=&tex[0];break;case ef::Src::Tex1:color=&tex[1];break;
     case ef::Src::Const:color=&constant;break;case ef::Src::Prev:break;default:assert(false);}
    a[k]=(*color)[op==ef::Operand::Alpha||op==ef::Operand::OneMinusAlpha?3:c];
    if(op==ef::Operand::OneMinusColor||op==ef::Operand::OneMinusAlpha)a[k]=1.f-a[k];
   }
   const auto f=c==3?st.alphaFunc:st.rgbFunc;float v=0;
   switch(f){case ef::Func::Replace:v=a[0];break;case ef::Func::Modulate:v=a[0]*a[1];break;
    case ef::Func::Interpolate:v=a[0]*a[2]+a[1]*(1-a[2]);break;case ef::Func::MultiplyAdd:v=a[0]*a[1]+a[2];break;
    default:assert(false);}
   out[c]=unit(v*(c==3?st.alphaScale:st.rgbScale));
  }
  prev=out;
 }
 return prev;
}
int main(int argc,char**argv){
 assert(argc==2);
 auto effects=std::make_unique<RaceEffects>();assert(effects->load(argv[1]));
 const auto& resources=effects->system().resources;
 std::vector<ef::Material> materials(resources.size());
 int exclm=-1;
 for(size_t i=0;i<resources.size();++i){
  const bool ok=ef::translateMaterial(resources[i].desc.draw,materials[i]);
  if(!std::strcmp(resources[i].name,"rk_poihanaExclm")){exclm=int(i);assert(ok);}
  if(!std::strcmp(resources[i].name,"rk_poihanaExclm0"))assert(ok);
 }
 assert(exclm>=0);
 // GX: CPREV = lerp(K0, C0, TEX0C), APREV = A0 * TEX0A; C = CPREV + lerp(C1, C2, TEX1C).
 uint32_t rng=0x2468ace;auto sample=[&](){rng=rng*1664525u+1013904223u;return float(rng>>24)/255.f;};
 unsigned checked=0;
 for(unsigned trial=0;trial<4000;++trial){
  std::array<Color,7> reg;for(auto& r:reg)for(float& v:r)v=sample();
  Color tex[2];for(auto& t:tex)for(float& v:t)v=sample();
  const Color got=run(materials[exclm],reg,tex);
  for(unsigned c=0;c<3;++c){
   const float stage0=unit(reg[ef::RegK0][c]*(1-tex[0][c])+reg[ef::RegC0][c]*tex[0][c]);
   const float expected=unit(stage0+reg[ef::RegC1][c]*(1-tex[1][c])+reg[ef::RegC2][c]*tex[1][c]);
   assert(std::fabs(got[c]-expected)<1e-5f);++checked;
  }
  assert(std::fabs(got[3]-reg[ef::RegC0][3]*tex[0][3])<1e-5f);++checked;
 }
 // The effect draws: the lock-on creates it at the Cataquack.
 std::vector<ef::TextureSize> sizes;
 for(const auto& t:effects->textures)sizes.push_back({t.width,t.height,t.paddedWidth,t.paddedHeight});
 const int root=effects->find("rk_poihanaExclm");assert(root>=0);
 EffectFrame frame;frame.origin={0,0,-500};
 effects->create(0,root,frame,0);
 unsigned drawn=0;ef::DrawList list;
 for(unsigned tick=0;tick<40;++tick){
  effects->move(0,frame);effects->step();
  ef::buildDrawList(effects->system(),materials,sizes.data(),sizes.size(),ef::Mtx34::identity(),list);
  for(const auto& b:list.batches)if(!std::strcmp(b.res->name,"rk_poihanaExclm")){assert(b.count%3==0);drawn+=b.count>0;}
 }
 assert(drawn>0);
 std::printf("cataquack_effects: %u equations, %u frames drawn\n",checked,drawn);
}
