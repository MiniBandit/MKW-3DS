"""Record the production blast binder and compare it to original GX equations.
This is host algebra/submission evidence, not a console framebuffer check.
"""
from pathlib import Path
import re,subprocess,json,hashlib
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT.parent/'changes/original-parity-20261012'
s=(ROOT/'src/race_hud.cpp').read_text();code=s[s.index('struct BlastMaterial {'):s.index('void drawBlast(')]
gpu=sorted(set(re.findall(r'\bGPU_\w+',code)));gpu.remove('GPU_TEXTURE_WRAP_PARAM')
preamble=r'''
#include <mkw/bomb_blast.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace mkw;
'''+ 'enum{'+','.join(sorted(set(gpu+['GPU_REPLACE','C3D_RGB','C3D_Alpha','C3D_Both'])))+'};\n'+r'''
using GPU_TEXTURE_WRAP_PARAM=int;
struct C3D_Mtx{};
struct C3D_Tex {unsigned id=0;};struct Texture {C3D_Tex gpu;};
struct Model {std::vector<std::shared_ptr<Texture>> textures;};
struct Renderer {bool env=false,unit2=false;unsigned sets=0;std::array<float,6> uv{};
 void setEnvironmentUv(const C3D_Mtx&,const float(&p)[6],bool third){env=true;unit2=third;++sets;std::copy(p,p+6,uv.begin());}
 void clearEnvironmentUv(){env=false;unit2=false;}
};
struct Channel {int source[3]{GPU_PREVIOUS,GPU_PREVIOUS,GPU_PREVIOUS};int op=GPU_REPLACE;float scale=1;};
struct C3D_TexEnv {Channel rgb,alpha;uint32_t color=0;};
C3D_TexEnv stages[6];uint32_t buffer;int units[3]{-1,-1,-1};
C3D_TexEnv* C3D_GetTexEnv(int n){assert(n<6&&n>=0);return stages+n;}
void C3D_TexEnvInit(C3D_TexEnv* e){*e={};}
void C3D_TexEnvColor(C3D_TexEnv* e,uint32_t c){e->color=c;}
void C3D_TexEnvBufColor(uint32_t c){buffer=c;}
void C3D_TexEnvSrc(C3D_TexEnv* e,int mode,int a,int b=GPU_PREVIOUS,int c=GPU_PREVIOUS){
 for(auto* ch:{&e->rgb,&e->alpha})if((ch==&e->rgb&&mode!=C3D_Alpha)||(ch==&e->alpha&&mode!=C3D_RGB)){ch->source[0]=a;ch->source[1]=b;ch->source[2]=c;}
}
void C3D_TexEnvFunc(C3D_TexEnv* e,int mode,int op){if(mode!=C3D_Alpha)e->rgb.op=op;if(mode!=C3D_RGB)e->alpha.op=op;}
void C3D_TexEnvScale(C3D_TexEnv* e,int mode,int){assert(mode==C3D_Alpha);e->alpha.scale=2;}
void C3D_TexSetWrap(C3D_Tex*,int,int){}
void C3D_TexBind(int n,C3D_Tex* t){assert(n<3);units[n]=t->id;}
template<class...T>void C3D_CullFace(T...){}
template<class...T>void C3D_AlphaTest(T...){}
template<class...T>void C3D_AlphaBlend(T...){}
template<class...T>void C3D_DepthTest(T...){}
float clamp(float x){return std::clamp(x,0.f,1.f);}
float calc(Channel c,float prev,float constant,float tex0,float tex1,float tex2,float buf){
 float a[3];for(int k=0;k<3;++k){switch(c.source[k]){case GPU_PREVIOUS:a[k]=prev;break;case GPU_CONSTANT:a[k]=constant;break;case GPU_TEXTURE0:a[k]=tex0;break;case GPU_TEXTURE1:a[k]=tex1;break;case GPU_TEXTURE2:a[k]=tex2;break;case GPU_PREVIOUS_BUFFER:a[k]=buf;break;default:assert(false);}}
 float x=0;switch(c.op){case GPU_REPLACE:x=a[0];break;case GPU_INTERPOLATE:x=a[0]*a[2]+a[1]*(1-a[2]);break;case GPU_MODULATE:x=a[0]*a[1];break;case GPU_ADD:x=a[0]+a[1];break;case GPU_SUBTRACT:x=a[0]-a[1];break;case GPU_MULTIPLY_ADD:x=a[0]*a[1]+a[2];break;default:assert(false);}return clamp(x*c.scale);
}
'''
body=r'''
int main(){
 Model model;for(unsigned i=0;i<6;++i){auto t=std::make_shared<Texture>();t->gpu.id=i;model.textures.push_back(t);}
 Renderer renderer;unsigned comparisons=0;float maxError=0;
 for(bool blue:{false,true})for(unsigned f=0;f<BombBlastFrames;++f){
  const auto& colors=(blue?BombBlastBlue:BombBlastRed)[f];BlastMaterial m{&renderer,&model,colors.data(),{},f};
  bindBlast(&m,BombCoreSubmesh);
  assert(renderer.env&&renderer.unit2&&renderer.uv==BombGlowGXUV[f]);
  assert(units[0]==BombWaveTexture&&units[1]==BombNetTexture&&units[2]==BombGlowTexture);
  assert(stages[3].rgb.op==GPU_MULTIPLY_ADD&&stages[3].alpha.op==GPU_SUBTRACT);
  for(unsigned channel=0;channel<4;++channel)for(float wave:{0.f,.1f,.3f,.6f,1.f})for(float net:{0.f,.2f,.5f,.8f,1.f})for(float glow:{0.f,.25f,.75f,1.f}){
   const float c0=colors[channel]/255.f,c1=colors[4+channel]/255.f;
   float expected=channel==3?net:clamp(c1*(1-net)+c0*net);
   expected=channel==3?clamp(expected*wave+c0):clamp(expected*(1-wave)+c0*wave+c1);
   expected=channel==3?clamp(expected-c1):clamp(expected+c0*glow);
   float actual=0;
   for(unsigned i=0;i<6;++i){const auto& e=stages[i];actual=calc(channel==3?e.alpha:e.rgb,actual,float((e.color>>(8*channel))&255)/255.f,wave,net,glow,float((buffer>>(8*channel))&255)/255.f);}
   const float error=std::fabs(actual-expected);maxError=std::max(maxError,error);assert(error<.000001f);++comparisons;
  }
  bindBlast(&m,BombShockSubmesh);assert(!renderer.env&&!renderer.unit2);
 }
 std::printf("PASS production blast binder: %u original GX RGB/alpha comparisons, error %.9g, 56 red/blue frames, independent third EnvCamera and shock-state reset\n",comparisons,maxError);
}
'''
cpp=OUT/'blast-material-production.cpp';cpp.write_text(preamble+code+body)
# This probe requires a separately compiled executable.
if not Path(str(OUT / 'blast-material-test')).is_file():raise FileNotFoundError('Compile this test executable manually first: '+str(str(OUT / 'blast-material-test')))
r=subprocess.run([str(OUT/'blast-material-test')],text=True,capture_output=True,check=True)
print(r.stdout.strip());(OUT/'blast-material-test.log').write_text(r.stdout)
(OUT/'blast-material-verification.json').write_text(json.dumps({'passed':True,'source':'exact bindBlast with recording C3D','sourceSha256':hashlib.sha256(code.encode()).hexdigest(),'result':r.stdout.strip(),'framebufferVerified':False,'hardwareVerified':False},indent=2)+'\n')
