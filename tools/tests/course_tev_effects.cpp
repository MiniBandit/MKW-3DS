// Course effects whose GX TEV had no PICA program: Rainbow Road's star ring sparkle
// (rk_StarRing_houshi), Koopa Cape's stone offroad (rk_stone), GCN DK Mountain's
// boulder pieces (rk_DKrockGC0/1) and N64 Sherbet Land's sliding penguin
// (rk_pgnSlider_s). Each cooked material translates, its PICA program (combiner
// buffer delay included) equals the cooked GX TEV evaluated stage by stage, and the
// effect produces draw batches.
// Argument: the cooked race effect bank folder (race-effects-NN.bin).
#include <mkw/race_effects.hpp>
#include <mkw/nw4r_ef_draw.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
using namespace mkw;
namespace mkw::pack {bool read(const char*,std::vector<uint8_t>&){return false;}}
using Color=std::array<float,4>;
using Registers=std::array<Color,7>; // indexed by ef::RegC0..RegK3
constexpr float Unset=std::numeric_limits<float>::quiet_NaN();
static float unit(float v){return std::clamp(v,0.f,1.f);}
static bool near(float a,float b){return std::fabs(a-b)<1e-5f;} // false for NaN

// PICA combiner: each stage clamps; a stage's buffer write reaches the stage after next.
static Color pica(const ef::Material& m,const Registers& reg,const Color tex[2]){
 auto value=[&](uint8_t sel,unsigned c){return sel<7?reg[sel][c]:sel==ef::ValZero?0.f:sel==ef::ValQuarter?.25f:sel==ef::ValHalf?.5f:sel==ef::ValOne?1.f:Unset;};
 auto channel=[&](ef::Channel ch){Color o;for(unsigned k=0;k<3;++k)o[k]=value(ch.rgb,ch.rgbFromAlpha?3:k);o[3]=value(ch.alpha,3);return o;};
 const Color vertex=channel(m.vertex),lookup=channel(m.lookup);
 Color prev,buffer,next;prev.fill(Unset);buffer.fill(Unset);next.fill(Unset);
 for(unsigned s=0;s<m.stages;++s){
  const auto& st=m.stage[s];const Color constant=channel(st.constant);Color out{};
  for(unsigned c=0;c<4;++c){
   float a[3];
   for(unsigned k=0;k<3;++k){
    const auto source=c==3?st.alphaSrc[k]:st.rgbSrc[k];const auto op=c==3?st.alphaOp[k]:st.rgbOp[k];
    const Color* color=&prev;
    switch(source){case ef::Src::Vertex:color=&vertex;break;case ef::Src::Lookup:color=&lookup;break;
     case ef::Src::Tex0:color=&tex[0];break;case ef::Src::Tex1:color=&tex[1];break;
     case ef::Src::Const:color=&constant;break;case ef::Src::Buffer:color=&buffer;break;case ef::Src::Prev:break;}
    a[k]=(*color)[op==ef::Operand::Alpha||op==ef::Operand::OneMinusAlpha?3:c];
    if(op==ef::Operand::OneMinusColor||op==ef::Operand::OneMinusAlpha)a[k]=1.f-a[k];
   }
   const auto f=c==3?st.alphaFunc:st.rgbFunc;float v=0;
   switch(f){case ef::Func::Replace:v=a[0];break;case ef::Func::Modulate:v=a[0]*a[1];break;
    case ef::Func::Add:v=a[0]+a[1];break;case ef::Func::AddSigned:v=a[0]+a[1]-.5f;break;
    case ef::Func::Interpolate:v=a[0]*a[2]+a[1]*(1-a[2]);break;case ef::Func::Subtract:v=a[0]-a[1];break;
    case ef::Func::MultiplyAdd:v=a[0]*a[1]+a[2];break;}
   out[c]=unit(v*(c==3?st.alphaScale:st.rgbScale));
  }
  buffer=next;
  if(st.bufferRgb)for(unsigned c=0;c<3;++c)next[c]=out[c];
  if(st.bufferAlpha)next[3]=out[3];
  prev=out;
 }
 return prev;
}

// GX TEV from the cooked DrawSetting: out = clamp((d +- ((1-c)a + cb) + bias) * scale),
// written to PREV, C0, C1 or C2; colour and alpha of a stage read the registers first.
static Color gx(const ef::DrawSetting& d,const Registers& reg,const Color tex[2]){
 Color r[4]{{},reg[ef::RegC0],reg[ef::RegC1],reg[ef::RegC2]};r[0].fill(Unset);
 auto konst=[&](uint8_t sel,unsigned c){
  if(sel<8)return float(8-sel)/8.f;
  if(sel>=12&&sel<16)return reg[ef::RegK0+sel-12][c];
  assert(sel>=16&&sel<32);return reg[ef::RegK0+(sel-16)%4][(sel-16)/4];
 };
 for(unsigned s=0;s<d.numTevs;++s){
  Color t;if(d.tevTexture[s]<2)t=tex[d.tevTexture[s]];else t.fill(Unset);
  auto colorArg=[&](uint8_t a,unsigned c)->float{
   switch(a){case 0:return r[0][c];case 1:return r[0][3];case 2:return r[1][c];case 3:return r[1][3];
    case 4:return r[2][c];case 5:return r[2][3];case 6:return r[3][c];case 7:return r[3][3];
    case 8:return t[c];case 9:return t[3];case 12:return 1.f;case 13:return .5f;
    case 14:return konst(d.kColorSel[s],c);case 15:return 0.f;}
   assert(false);return Unset; // RASC / RASA: not used by these emitters
  };
  auto alphaArg=[&](uint8_t a)->float{
   switch(a){case 0:return r[0][3];case 1:return r[1][3];case 2:return r[2][3];case 3:return r[3][3];
    case 4:return t[3];case 6:return konst(d.kAlphaSel[s],3);case 7:return 0.f;}
   assert(false);return Unset;
  };
  auto combine=[](const uint8_t op[5],float a,float b,float c,float dd){
   assert(op[0]<=1&&op[1]<=2&&op[2]<=3&&op[3]==1); // add / sub, clamped
   const float lerp=(1-c)*a+c*b;
   const float v=(op[0]?dd-lerp:dd+lerp)+(op[1]==1?.5f:op[1]==2?-.5f:0.f);
   return unit(v*(op[2]==1?2.f:op[2]==2?4.f:op[2]==3?.5f:1.f));
  };
  Color out;
  for(unsigned c=0;c<3;++c){const uint8_t* a=d.tevColor[s];out[c]=combine(d.tevColorOp[s],colorArg(a[0],c),colorArg(a[1],c),colorArg(a[2],c),colorArg(a[3],c));}
  const uint8_t* a=d.tevAlpha[s];out[3]=combine(d.tevAlphaOp[s],alphaArg(a[0]),alphaArg(a[1]),alphaArg(a[2]),alphaArg(a[3]));
  Color& colorOut=r[d.tevColorOp[s][4]];for(unsigned c=0;c<3;++c)colorOut[c]=out[c];
  r[d.tevAlphaOp[s][4]][3]=out[3];
 }
 assert(d.tevColorOp[d.numTevs-1][4]==0&&d.tevAlphaOp[d.numTevs-1][4]==0);
 return r[0];
}

using Formula=std::function<Color(const Registers&,const Color*)>;
struct Case {const char* bank;const char* emitter;const char* root;Formula formula;};
static float lerp(float a,float b,float c){return a*(1-c)+b*c;}

int main(int argc,char**argv){
 assert(argc==2);
 using namespace ef;
 const Case cases[]={
  {"race-effects-13.bin","rk_StarRing_houshi","rk_StarRing",[](const Registers& g,const Color* t){
    // C2 = K0 * TEX0C + C0, A2 = A0 * TEX0A; CPREV = lerp(K1, C1, TEX1C), APREV = A1 * TEX1A;
    // C = C2 + CPREV, A = A2 * APREV.
    Color o;for(unsigned c=0;c<3;++c)o[c]=unit(unit(g[RegC0][c]+g[RegK0][c]*t[0][c])+lerp(g[RegK1][c],g[RegC1][c],t[1][c]));
    o[3]=g[RegC0][3]*t[0][3]*(g[RegC1][3]*t[1][3]);return o;}},
  {"race-effects-15.bin","rk_stone","rk_stone",[](const Registers& g,const Color* t){
    // CPREV = lerp(A0, C0, TEX0C), A = A0 * TEX0A; C = C1 * CPREV.
    Color o;for(unsigned c=0;c<3;++c)o[c]=g[RegC1][c]*lerp(g[RegC0][3],g[RegC0][c],t[0][c]);
    o[3]=g[RegC0][3]*t[0][3];return o;}},
  {"race-effects-19.bin","rk_DKrockGC0","rk_DKrockGC",[](const Registers& g,const Color* t){
    Color o;for(unsigned c=0;c<3;++c)o[c]=g[RegC0][c]*t[0][c];o[3]=g[RegC0][3]*t[0][3];return o;}},
  {"race-effects-19.bin","rk_DKrockGC1","rk_DKrockGC",[](const Registers& g,const Color* t){
    Color o;for(unsigned c=0;c<3;++c)o[c]=g[RegC0][c]*t[0][c];o[3]=g[RegC0][3]*t[0][3];return o;}},
  {"race-effects-27.bin","rk_pgnSlider_s","rk_pgnSlider_s",[](const Registers& g,const Color* t){
    // CPREV = lerp(C0, C1, TEX0C), APREV = A0 * TEX0A; C = CPREV, A = A1 * APREV.
    Color o;for(unsigned c=0;c<3;++c)o[c]=lerp(g[RegC0][c],g[RegC1][c],t[0][c]);
    o[3]=g[RegC1][3]*(g[RegC0][3]*t[0][3]);return o;}},
 };
 unsigned checked=0;
 for(const Case& k:cases){
  auto effects=std::make_unique<RaceEffects>();
  assert(effects->load((std::string(argv[1])+"/"+k.bank).c_str()));
  const auto& resources=effects->system().resources;
  std::vector<Material> materials(resources.size());
  int index=-1;
  for(size_t i=0;i<resources.size();++i){
   const bool ok=translateMaterial(resources[i].desc.draw,materials[i]);
   if(!std::strcmp(resources[i].name,k.emitter)){index=int(i);assert(ok);}
  }
  assert(index>=0);
  const DrawSetting& d=resources[index].desc.draw;
  assert(!materials[index].projected&&!materials[index].swapUnits);
  // Registers _SetupTevReg leaves alone hold stale GX values: NaN, so reading one fails.
  uint32_t rng=0x13579bd;auto sample=[&](){rng=rng*1664525u+1013904223u;return float(rng>>24)/255.f;};
  for(unsigned trial=0;trial<4000;++trial){
   Registers reg;
   for(unsigned i=0;i<7;++i){
    const unsigned input=i+1;const bool rgb=d.colorInput[input],alpha=d.alphaInput[input];
    for(unsigned c=0;c<3;++c)reg[i][c]=rgb?sample():alpha?0.f:Unset;
    reg[i][3]=alpha?sample():rgb?0.f:Unset;
   }
   Color tex[2];for(auto& t:tex)for(float& v:t)v=sample();
   const Color want=gx(d,reg,tex),got=pica(materials[index],reg,tex),hand=k.formula(reg,tex);
   for(unsigned c=0;c<4;++c){
    if(!near(got[c],want[c])||!near(hand[c],want[c])){
     std::printf("%s trial %u channel %u: pica %f gx %f formula %f\n",k.emitter,trial,c,got[c],want[c],hand[c]);
     return 1;
    }
    ++checked;
   }
  }
  // The effect draws.
  std::vector<TextureSize> sizes;
  for(const auto& t:effects->textures)sizes.push_back({t.width,t.height,t.paddedWidth,t.paddedHeight});
  const int root=effects->find(k.root);assert(root>=0);
  EffectFrame frame;frame.origin={0,0,-500};
  effects->create(0,root,frame,0);
  unsigned drawn=0;DrawList list;
  for(unsigned tick=0;tick<120;++tick){
   effects->move(0,frame);effects->step();
   buildDrawList(effects->system(),materials,sizes.data(),sizes.size(),Mtx34::identity(),list);
   bool any=false;
   for(const auto& b:list.batches)if(!std::strcmp(b.res->name,k.emitter)){assert(b.count%3==0);any|=b.count>0;}
   drawn+=any;
  }
  std::printf("%s: %u frames drawn\n",k.emitter,drawn);
  assert(drawn>0);
 }
 std::printf("course_tev_effects: %u channel checks\n",checked);
}
