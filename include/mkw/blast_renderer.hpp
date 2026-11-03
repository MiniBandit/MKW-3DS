#pragma once
#include <mkw/renderer.hpp>
#include <mkw/bomb_blast.hpp>
#include <algorithm>
#include <array>
namespace mkw::blast {
// Retail bombCore blast materials (tables generated from the BRRES by tools/export_blast.ts):
// bombCoreRed for the Bob-omb, bombCoreBlue for the Blue Shell. Core UV0/UV1
// keep their own animated matrices; EnvCamera supplies glowWhite3s on unit 2.
struct BlastMaterial { Renderer* renderer;const Model* model;const uint8_t* colours;C3D_Mtx transform;unsigned frame; };
inline uint32_t rgba8(const uint8_t* c){return uint32_t(c[0])|uint32_t(c[1])<<8|uint32_t(c[2])<<16|uint32_t(c[3])<<24;}
inline void bindBlast(void* context,std::size_t submesh){
    auto& m=*static_cast<BlastMaterial*>(context);
    C3D_TexEnvBufUpdate(C3D_Both,0);
    for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto texture=[&](int index,GPU_TEXTURE_WRAP_PARAM s,GPU_TEXTURE_WRAP_PARAM t){auto* x=&m.model->textures[index]->gpu;C3D_TexSetWrap(x,s,t);return x;};
    C3D_CullFace(GPU_CULL_NONE);
    auto* e0=C3D_GetTexEnv(0);auto* e1=C3D_GetTexEnv(1);
    if(submesh==BombCoreSubmesh){
        const uint8_t *c0=m.colours,*c1=m.colours+4;
        C3D_TexBind(0,texture(BombWaveTexture,GPU_REPEAT,GPU_REPEAT));C3D_TexBind(1,texture(BombNetTexture,GPU_REPEAT,GPU_REPEAT));
        C3D_TexBind(2,texture(BombGlowTexture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE));
        float glow[6];std::copy(BombGlowGXUV[m.frame].begin(),BombGlowGXUV[m.frame].end(),glow);
        // Runs inside the material binder on the render thread, after the
        // submesh's independent UV rows were set (also safe for immediate draws).
        if(m.model->staticLightingBaked&&m.model->staticEnvironment2)m.renderer->setBakedEnvironmentUv();
        else m.renderer->setEnvironmentUv(m.transform,glow,true);
        // GX 0: lerp(C1, C0, ef_ami), alpha ef_ami (C1 rides in the combiner buffer colour).
        C3D_TexEnvBufColor(rgba8(c1));
        C3D_TexEnvColor(e0,rgba8(c0));
        C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS_BUFFER,GPU_TEXTURE1);C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
        C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE1);C3D_TexEnvFunc(e0,C3D_Alpha,GPU_REPLACE);
        // GX 1: lerp(prev, C0, ef_waveIA) + C1, alpha prev * ef_waveIA + A0.
        C3D_TexEnvColor(e1,rgba8(c0));
        C3D_TexEnvSrc(e1,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(e1,C3D_RGB,GPU_INTERPOLATE);
        C3D_TexEnvSrc(e1,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(e1,C3D_Alpha,GPU_MODULATE);
        auto* e2=C3D_GetTexEnv(2);
        C3D_TexEnvColor(e2,(rgba8(c1)&0xffffffu)|uint32_t(c0[3])<<24);
        C3D_TexEnvSrc(e2,C3D_Both,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e2,C3D_Both,GPU_ADD);
        // GX 2 RGB: previous + C0 * glowWhite3s. Its alpha subtracts A1
        // independently, keeping the original dissolve and alpha test.
        auto* e3=C3D_GetTexEnv(3);
        C3D_TexEnvColor(e3,(rgba8(c0)&0xffffffu)|uint32_t(c1[3])<<24);
        C3D_TexEnvSrc(e3,C3D_RGB,GPU_CONSTANT,GPU_TEXTURE2,GPU_PREVIOUS);C3D_TexEnvFunc(e3,C3D_RGB,GPU_MULTIPLY_ADD);
        C3D_TexEnvSrc(e3,C3D_Alpha,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e3,C3D_Alpha,GPU_SUBTRACT);
        C3D_AlphaTest(true,GPU_GEQUAL,1);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
    }else{
        m.renderer->clearEnvironmentUv();
        const uint8_t *c0=m.colours+8,*c1=m.colours+12;
        C3D_TexBind(0,texture(BombShockTexture,GPU_MIRRORED_REPEAT,GPU_CLAMP_TO_EDGE));
        // bombShk: lerp(C0, C1, wave08_3), alpha 2 * texture alpha * A0, blended, no depth write.
        C3D_TexEnvBufColor(rgba8(c0));
        C3D_TexEnvColor(e0,rgba8(c1));
        C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS_BUFFER,GPU_TEXTURE0);C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
        C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE0);C3D_TexEnvFunc(e0,C3D_Alpha,GPU_REPLACE);C3D_TexEnvScale(e0,C3D_Alpha,GPU_TEVSCALE_2);
        C3D_TexEnvColor(e1,uint32_t(c0[3])<<24);
        C3D_TexEnvSrc(e1,C3D_Alpha,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e1,C3D_Alpha,GPU_MODULATE);
        C3D_AlphaTest(false,GPU_ALWAYS,0);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ZERO,GPU_ONE);
        C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_COLOR);
    }
}
inline void drawBlast(Renderer& renderer,const Model& model,const C3D_Mtx& t,unsigned age,bool blue){
    const unsigned frame=std::min(age,BombBlastFrames-1);
    BlastMaterial m{&renderer,&model,(blue?BombBlastBlue:BombBlastRed)[frame].data(),t,frame};
    std::array<SubmeshOverride,2> uv{};
    for(auto& o:uv){o.flags=SubmeshOverride::Uv;o.uv[0]=o.uv[4]=o.uv[6]=o.uv[10]=1;} // identity rows
    for(unsigned k=0;k<12;++k)uv[BombCoreSubmesh].uv[k]=BombCoreUV[frame][k];
    for(unsigned k=0;k<6;++k)uv[BombShockSubmesh].uv[k]=BombShockUV[frame][k];
    renderer.setOverrides(uv.data(),unsigned(uv.size()));
    // The context is copied by value: a deferred frame draws after this returns.
    renderer.draw(model,t,false,bindBlast,m);
    renderer.setOverrides(nullptr,0);
    renderer.clearEnvironmentUv();
}
}
