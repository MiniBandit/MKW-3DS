// Retail course lighting on PICA200 (docs/COURSE_LIGHTING.md).
//
// Course, sky, map and course-object submeshes cooked with GMDL v4 state
// (tools/retail_lighting.ts) are drawn here exactly as Mario Kart Wii does:
//  - GX colour channel 0 in the vertex shader: unlit channels pass the vertex
//    or register colour; lit ones use G3D light set k = {BLIGHT light k,
//    ambient 0} (StaticR 0x8055CD68), ambient = ambient0*material/255;
//  - EGG light textures lm_0/lm_1, rendered on the CPU exactly like
//    EGG::LightTexture into 32x32 sphere maps and sampled through GX
//    EnvCamera coordinates (texture units 1 and 2);
//  - GX fog through a ramp texture on unit 2 indexed by eye depth, or, for
//    light-texture materials (which need unit 2), as a last combiner stage
//    with the fog factor at the object's eye depth.
// Static meshes retain fixed maps; the local racer samples dynamic lighting
// every five simulation ticks and interpolates its colours and environment UVs.
#include "mkw/renderer.hpp"
#include <mkw/normal_matrix.hpp>
#include <mkw/retail_lighting.hpp>
#include <mkw/light_texture_blend.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mkw {
namespace {
constexpr unsigned LightTextureSize=32,FogRampWidth=128,FogRampHeight=8;
// Saturation ceiling for draws outside retail lighting (no clamp in practice).
constexpr float LegacyCeiling=64.f;
// PICA 8x8 tile, Morton order inside; y counts rows from texcoord v=0.
unsigned tiled(unsigned x,unsigned y,unsigned width){
    unsigned m=0;
    for(unsigned b=0;b<3;++b)m|=((x>>b)&1u)<<(2*b)|((y>>b)&1u)<<(2*b+1);
    return ((y>>3)*(width>>3)+(x>>3))*64+m;
}
float unit(uint32_t rgba,unsigned i){return float((rgba>>(24-8*i))&255)/255.f;}
// 0xRRGGBBAA to the TEV constant layout 0xAABBGGRR.
uint32_t tevColor(uint32_t rgba){
    return (rgba>>24)|((rgba>>8)&0xff00)|((rgba<<8)&0xff0000)|((rgba&255)<<24);
}
bool fogVisible(const float* fog){
    const unsigned type=unsigned(fog[0])&7;
    return fog[1]!=0.f&&(type==2||type>=4);
}
}

bool Renderer::retailUniformLocations(){
    auto location=[&](const char* name){return int(shaderInstanceGetUniformLocation(program_.vertexShader,name));};
    lightDiffLoc_=location("lightDiff");lightDirLoc_=location("lightDir");lightFnLoc_=location("lightFn");
    fogParamLoc_=location("fogParam");
    retailBoolLoc_=location("retailLightTexture");
    environmentMappedLoc_=location("environmentMapped");environmentTexture2Loc_=location("environmentTexture2");environmentPrimaryLoc_=location("environmentPrimary");diffuseLightingLoc_=location("diffuseLighting");
    for(unsigned i=0;i<3;++i){char name[16];std::snprintf(name,sizeof name,"normalRow%u",i);normalRowLoc_[i]=location(name);if(normalRowLoc_[i]<0)return false;}
    return lightDiffLoc_>=0&&lightDirLoc_>=0&&lightFnLoc_>=0&&fogParamLoc_>=0&&retailBoolLoc_>=0&&environmentMappedLoc_>=0&&environmentTexture2Loc_>=0&&environmentPrimaryLoc_>=0&&diffuseLightingLoc_>=0;
}

void Renderer::setRetailLighting(const CourseLighting* course,bool fixedLightTextures){
    if(course==retail_&&fixedLightTextures==fixedLightTextures_)return;
    // Light textures and the fog ramp belong to the drawing side.
    drain();
    retail_=course;fixedLightTextures_=fixedLightTextures;retailTexturesValid_=false;lightTextureAppliedTick_=~0u;
    retailFog_[7]=-1.f;
    if(!course)return;
    for(unsigned which=0;which<2;++which)retailTextureDynamic_[which]=retailLightTextureDynamic(*course,which);
    for(auto* pair:{retailTexture_,fixedRetailTexture_})for(unsigned index=0;index<2;++index){auto& t=pair[index];
        if(t.data)continue;
        if(!C3D_TexInit(&t,LightTextureSize,LightTextureSize,GPU_RGBA8)){retail_=nullptr;return;}
        // CapTexture: bilinear, clamped (LightTexture::configure).
        C3D_TexSetFilter(&t,GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(&t,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    }
    if(!retailFogRamp_.data){
        if(!C3D_TexInit(&retailFogRamp_,FogRampWidth,FogRampHeight,GPU_A8)){retail_=nullptr;return;}
        C3D_TexSetFilter(&retailFogRamp_,GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(&retailFogRamp_,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    }
    RetailFogState initial;initial.reset(*course);retailFogNow(initial.value);
}

void Renderer::retailFogNow(const RetailFog& fog){
    const float ramp=retailFog_[7];
    retailFog_[0]=float(fog.type);retailFog_[1]=fog.enabled?1.f:0.f;retailFog_[2]=fog.start;retailFog_[3]=fog.end;
    for(unsigned i=0;i<3;++i)retailFog_[4+i]=unit(fog.color,i);
    retailFog_[7]=ramp;
}

void Renderer::retailShutdown(){
    for(auto* pair:{retailTexture_,fixedRetailTexture_})for(unsigned i=0;i<2;++i){auto& t=pair[i];if(t.data)C3D_TexDelete(&t);t={};}
    if(retailFogRamp_.data)C3D_TexDelete(&retailFogRamp_);
    retailFogRamp_={};retail_=nullptr;retailTexturesValid_=false;
}

void Renderer::retailBeginFrame(){
    // Defaults for every draw: no diffuse term, no saturation, fog off.
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentTexture2Loc_,false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentPrimaryLoc_,false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,diffuseLightingLoc_,false);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightDiffLoc_,0,0,0,0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightDirLoc_,0,0,1,LegacyCeiling);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightFnLoc_,1,0,0,0);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,retailBoolLoc_,false);
    retailLightDirty_=retailFogBound_=retailTextureDirty_=false;
    if(!retail_){C3D_FVUnifSet(GPU_VERTEX_SHADER,fogParamLoc_,0,0,0,0);return;}
    // EGG::LightObject::CalcView for this frame's camera.
    std::fill(std::begin(retailLightViewReady_),std::end(retailLightViewReady_),false);
    // Static meshes never need a camera-space light direction. A local held
    // item resolves only the light sets it actually uses, below in retailLight.
    if(!fixedLightTextures_){
        float rows[3][3];
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)rows[r][c]=view_.r[r].c[3-c];
        for(unsigned i=0;i<16;++i){retailLightView_[i]=retailLightViewDirection(retail_->lights[i],rows);retailLightViewReady_[i]=true;}
    }
    // Independently cache each light texture at GX's existing S1.10 direction
    // precision: camera motion below a quantization boundary changes no texel.
    // C3D_FrameBegin has retired every earlier use of these textures.
    // Sample the local racer maps every five simulation ticks. Both eyes
    // share the result; static objects retain their separate fixed map pair.
    Vec3 textureDirections[16];
    const Vec3* directions=retailLightView_;
    if(fixedLightTextures_&&!retailTexturesValid_){
        const float neutral[3][3]={{1,0,0},{0,1,0},{0,0,1}};
        for(unsigned i=0;i<16;++i)textureDirections[i]=retailLightViewDirection(retail_->lights[i],neutral);
        directions=textureDirections;
    }
    if(!retailTexturesValid_){
        Vec3 fixed[16];const float identity[3][3]={{1,0,0},{0,1,0},{0,0,1}};
        for(unsigned i=0;i<16;++i)fixed[i]=retailLightViewDirection(retail_->lights[i],identity);
        for(unsigned which=0;which<2;++which){uint8_t rgba[32*32*4];retailLightTexture(*retail_,which,fixed,rgba);
            auto* out=static_cast<uint8_t*>(fixedRetailTexture_[which].data);
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)for(unsigned c=0;c<4;++c)
                out[tiled(x,31-y,32)*4+3-c]=rgba[(y*32+x)*4+c];
            C3D_TexFlush(&fixedRetailTexture_[which]);
        }
    }
    const unsigned elapsed=execFrame_-lightTextureTick_;
    const bool sample=!retailTexturesValid_||(!fixedLightTextures_&&elapsed>=5);
    if(sample){
        for(unsigned which=0;which<2;++which){
            std::memcpy(lightTextureFrom_[which],lightTextureTo_[which],sizeof lightTextureTo_[which]);
            int key[12];retailLightTextureKey(*retail_,which,directions,key);
            if(!retailTexturesValid_||std::memcmp(key,retailTextureKeys_[which],sizeof key))
                retailLightTexture(*retail_,which,directions,lightTextureTo_[which]);
            if(!retailTexturesValid_||elapsed>10)std::memcpy(lightTextureFrom_[which],lightTextureTo_[which],sizeof lightTextureTo_[which]);
            lightTextureTransition_[which]=std::memcmp(lightTextureFrom_[which],lightTextureTo_[which],sizeof lightTextureTo_[which])!=0;
            std::memcpy(retailTextureKeys_[which],key,sizeof key);
        }
        lightTextureTick_=execFrame_;
    }
    const unsigned blendTick=std::min<unsigned>(5u,execFrame_-lightTextureTick_);
    if(!retailTexturesValid_||(!fixedLightTextures_&&execFrame_!=lightTextureAppliedTick_)){
    for(unsigned which=0;which<2;++which){
        // Commit on a new sample even when its endpoints match: the preceding
        // frame may still hold the old transition's 4/5 value. Afterwards a
        // stable pair needs neither conversion nor another cache flush.
        if(retailTexturesValid_&&!sample&&!lightTextureTransition_[which])continue;
        auto* out=static_cast<uint8_t*>(retailTexture_[which].data);
        blendLightTexture32(out,lightTextureFrom_[which],lightTextureTo_[which],blendTick);
        C3D_TexFlush(&retailTexture_[which]);
    }
    lightTextureAppliedTick_=execFrame_;
    }
    retailTexturesValid_=true;
    // GX fog: ramp of the fog function over eye depth normalized to
    // [start,end] (texel x holds f(x/127); u = (127*ze + 0.5)/128).
    const unsigned type=unsigned(retailFog_[0])&7;
    if(retailFog_[7]!=float(type)){
        auto* out=static_cast<uint8_t*>(retailFogRamp_.data);
        for(unsigned y=0;y<FogRampHeight;++y)for(unsigned x=0;x<FogRampWidth;++x)
            out[tiled(x,y,FogRampWidth)]=uint8_t(std::lround(std::clamp(retailFogFactor(type,float(x)/float(FogRampWidth-1)),0.f,1.f)*255.f));
        C3D_TexFlush(&retailFogRamp_);
        retailFog_[7]=float(type);
    }
    const float range=std::max(retailFog_[3]-retailFog_[2],1e-3f);
    // View space looks down -z: GX eye depth is -z.
    const float scale=-1.f/range,bias=-retailFog_[2]/range;
    C3D_FVUnifSet(GPU_VERTEX_SHADER,fogParamLoc_,scale*float(FogRampWidth-1)/float(FogRampWidth),
                  (bias*float(FogRampWidth-1)+0.5f)/float(FogRampWidth),scale,bias);
}

bool Renderer::retailLight(const Submesh& s){
    if(!retail_||!(s.lighting&retail::Retail))return false;
    const unsigned channel=(s.lighting&retail::ChannelMask)>>retail::ChannelShift;
    if(!channel||(s.lighting&retail::StaticLighting)){
        // Unlit GX channel: the vertex (or register) colour as authored.
        C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);
        return true;
    }
    const unsigned set=(s.lighting&retail::LightSetMask)>>retail::LightSetShift;
    const bool alphaLit=s.lighting&retail::AlphaLit;
    // G3DState::LoadResMatChan: ambient = 0.5 + ambient0*material/255 (u8).
    float ambient[4];
    for(unsigned i=0;i<4;++i)
        ambient[i]=float(unsigned(0.5f+float((retail_->ambient[0]>>(24-8*i))&255)*float((s.ambient>>(24-8*i))&255)*(1.f/255.f)))/255.f;
    if(!alphaLit)ambient[3]=1.f;
    if(!retailLightViewReady_[set]){
        float rows[3][3];
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)rows[r][c]=view_.r[r].c[3-c];
        retailLightView_[set]=retailLightViewDirection(retail_->lights[set],rows);
        retailLightViewReady_[set]=true;
    }
    const RetailLight& light=retail_->lights[set];
    // G3D light: enabled with the GX flag; colour/alpha only when flagged.
    const bool on=(light.flags&0x41)==0x41;
    const float diffuse[4]={on&&(light.flags&0x200)?unit(light.color,0):0.f,on&&(light.flags&0x200)?unit(light.color,1):0.f,
                            on&&(light.flags&0x200)?unit(light.color,2):0.f,alphaLit&&on&&(light.flags&0x400)?unit(light.color,3):0.f};
    // Directional light at -1e10*direction: L points back toward the light.
    const Vec3 toward=-retailLightView_[set];
    const unsigned fn=(s.lighting&retail::DiffuseMask)>>retail::DiffuseShift;
    const float function[3][3]={{0,1,0},{1,0,-2},{1,0,0}}; // none, sign, clamp
    const float* f=function[fn<3?fn:2];
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,ambient[0],ambient[1],ambient[2],ambient[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightDiffLoc_,diffuse[0],diffuse[1],diffuse[2],diffuse[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightDirLoc_,toward.x,toward.y,toward.z,1.f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightFnLoc_,f[0],f[1],f[2],0);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,diffuseLightingLoc_,diffuse[0]!=0.f||diffuse[1]!=0.f||diffuse[2]!=0.f||diffuse[3]!=0.f);
    retailLightDirty_=true;
    return true;
}

void Renderer::retailEnvironment(const C3D_Mtx& modelView){
    // GX EnvCamera: view-space normal; the shader normalizes the rows' result.
    normalMatrixNow(modelView,true);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,1,0,1,0);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,true);
}

void Renderer::normalMatrixNow(const C3D_Mtx& modelView,bool environment){
    Vec3 rows[3],normal[3];
    for(unsigned r=0;r<3;++r)rows[r]={modelView.r[r].x,modelView.r[r].y,modelView.r[r].z};
    normalMatrix(rows,normal);
    for(unsigned r=0;r<3;++r){
        C3D_FVUnifSet(GPU_VERTEX_SHADER,normalRowLoc_[r],normal[r].x,normal[r].y,normal[r].z,0);
        if(environment)C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[r],normal[r].x,normal[r].y,normal[r].z,0);
    }
}

bool Renderer::retailCombiners(const Submesh& s,const C3D_Mtx& modelView,bool backdrop,bool additive,uint32_t additiveRgb,uint32_t constant){
    // The sky pass (backdrop) is outside the course fog object in retail.
    const bool fog=!backdrop&&(s.lighting&retail::Fog)&&fogVisible(retailFog_);
    const unsigned channel=(s.lighting&retail::ChannelMask)>>retail::ChannelShift;
    const uint32_t fogColor=uint32_t(std::lround(retailFog_[4]*255.f))|uint32_t(std::lround(retailFog_[5]*255.f))<<8|
                            uint32_t(std::lround(retailFog_[6]*255.f))<<16|0xff000000u;
    if(s.menuReflection==8||s.menuReflection==10){
        // All three texture units hold the source statue layers. As with
        // existing light-texture objects, apply fog at the object's eye depth.
        if(fog){
            const float depth=-modelView.r[2].w;
            const float f=retailFogFactor(unsigned(retailFog_[0]),(depth-retailFog_[2])/std::max(retailFog_[3]-retailFog_[2],1e-3f));
            auto* e=C3D_GetTexEnv(5);
            C3D_TexEnvColor(e,(fogColor&0xffffffu)|uint32_t(std::lround((1.f-f)*255.f))<<24);
            C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_CONSTANT);
            C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
            C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);
        }
        retailFogBound_=retailTextureDirty_=true;
        return true;
    }
    if(channel==2){
        // Light-texture materials (tools/retail_lighting.ts patterns). The
        // vertex colour is the GX channel (rasC, rasA); lm_0/lm_1 are the EGG
        // light textures. Fog is evaluated at the object's eye depth (these
        // are small characters) and applied as the last stage.
        static const GPU_TEVSCALE scales[]={GPU_TEVSCALE_1,GPU_TEVSCALE_2,GPU_TEVSCALE_4,GPU_TEVSCALE_1};
        const unsigned primary=(s.lighting&retail::PrimaryMask)>>retail::PrimaryShift;
        const unsigned primaryScale=(s.lighting&retail::PrimaryScaleMask)>>retail::PrimaryScaleShift;
        const unsigned secondary=(s.lighting&retail::SecondaryMask)>>retail::SecondaryShift;
        const unsigned albedoScale=(s.lighting&retail::AlbedoScaleMask)>>retail::AlbedoScaleShift;
        for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
        int n=0;
        auto next=[&](){return C3D_GetTexEnv(n++);};
        C3D_TexEnv* e=nullptr;
        if(primary&&primaryScale==3){
            // (rasC + lm_0*rasA) * 0.5 without saturating the sum first.
            e=next();C3D_TexEnvColor(e,0x80808080u);C3D_TexEnvSrc(e,C3D_Both,GPU_PRIMARY_COLOR,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_Both,GPU_MODULATE);
            if(primary==2){C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_ALPHA);}
            e=next();C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE1,GPU_PREVIOUS,GPU_PREVIOUS);
            C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR);C3D_TexEnvFunc(e,C3D_RGB,GPU_MULTIPLY_ADD);
        }else if(primary){
            // P = rasC + lm_0*rasA (1) or rasA + lm_0*rasA (2), GX-scaled.
            e=next();C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE1,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
            C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,primary==2?GPU_TEVOP_RGB_SRC_ALPHA:GPU_TEVOP_RGB_SRC_COLOR);
            C3D_TexEnvFunc(e,C3D_RGB,GPU_MULTIPLY_ADD);C3D_TexEnvScale(e,C3D_RGB,scales[primaryScale]);
        }
        const auto previous=primary?GPU_PREVIOUS:GPU_PRIMARY_COLOR;
        e=next();
        if(s.lighting&retail::AlbedoLerp){
            // lerp(C2, albedo, P): albedo*P + C2*(1-P).
            C3D_TexEnvColor(e,s.constant);C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,previous);C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);
        }else{C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,previous);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);}
        C3D_TexEnvScale(e,C3D_RGB,scales[albedoScale]);
        // APREV = albedo alpha * rasA, the lm_1 mask.
        C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e,C3D_Alpha,GPU_MODULATE);
        if(s.lighting&retail::AlbedoAdd){
            // GX (d=C1 + lerp)*scale saturates once; adding C1*scale after the
            // saturated stage gives the same result for a non-negative C1.
            static const float factor[]={1.f,2.f,4.f,.5f};
            uint32_t add=0;
            for(unsigned i=0;i<3;++i){
                const float c=additive?float((additiveRgb>>(8*i))&255):unit(s.add,i)*255.f;
                add|=uint32_t(std::min(255.f,std::floor(c*factor[albedoScale]+.5f)))<<(8*i);
            }
            e=next();C3D_TexEnvColor(e,add|0xff000000u);
            C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_ADD);
            C3D_TexEnvSrc(e,C3D_Alpha,GPU_PREVIOUS);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
        }
        if(s.lighting&retail::AlbedoTint){
            // GX C0 is the per-identity clothing/skin colour. Preserve
            // APREV for the following lm_1 mask; do not tint specular light.
            e=next();C3D_TexEnvColor(e,constant);
            C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
            C3D_TexEnvSrc(e,C3D_Alpha,GPU_PREVIOUS);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
        }
        if(secondary==2){
            e=next();C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE2,GPU_PRIMARY_COLOR,GPU_PREVIOUS);C3D_TexEnvFunc(e,C3D_RGB,GPU_MULTIPLY_ADD);
        }else if(secondary==1){
            e=next();C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE2,GPU_PREVIOUS,GPU_PREVIOUS);
            C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR);C3D_TexEnvFunc(e,C3D_RGB,GPU_MULTIPLY_ADD);
        }
        if(additive&&!(s.lighting&retail::AlbedoAdd)){
            // Star animates GX C1. Rebuilding the light-texture stages must
            // retain it, before fog, without changing the material alpha.
            e=next();C3D_TexEnvColor(e,additiveRgb);
            C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_RGB,GPU_ADD);
            C3D_TexEnvSrc(e,C3D_Alpha,GPU_PREVIOUS);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
        }
        if(fog){
            // GX fog at the object's eye depth: lerp(fog colour, colour, 1-f).
            const float depth=-modelView.r[2].c[0];
            const float ze=(depth-retailFog_[2])/std::max(retailFog_[3]-retailFog_[2],1e-3f);
            const float f=retailFogFactor(unsigned(retailFog_[0]),ze);
            const uint32_t keep=uint32_t(std::lround((1.f-f)*255.f));
            e=next();C3D_TexEnvColor(e,(fogColor&0xffffffu)|keep<<24);
            C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_CONSTANT);
            C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);
        }
        // Output alpha: the albedo alpha (konst 1 or APREV in retail; these
        // materials are opaque unless they alpha-test it).
        C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE0);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
        if((s.lighting&retail::Menu)&&((s.flags>>20)&7)==4){
            // Retail menu drivers end with a fixed konst alpha. Their lit
            // raster alpha remains an internal light-texture mask only.
            auto* alpha=next();C3D_TexEnvColor(alpha,s.constant);
            C3D_TexEnvSrc(alpha,C3D_Alpha,GPU_CONSTANT);C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_REPLACE);
        }
        auto* maps=drawSampledLighting_?retailTexture_:fixedRetailTexture_;
        C3D_TexBind(1,&maps[0]);C3D_TexBind(2,&maps[1]);retailFogBound_=true;
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,retailBoolLoc_,true);retailTextureDirty_=true;
        if(s.lighting&retail::FixedEnvironment)return false;
        retailEnvironment(modelView);
        return true;
    }
    if(!fog)return false;
    if(additive){
        // The additive (CLR0/Star) stage moves before fog: GX fog follows the TEV.
        auto* add=C3D_GetTexEnv(4);C3D_TexEnvColor(add,additiveRgb);
        C3D_TexEnvSrc(add,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvOpRgb(add,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(add,C3D_RGB,GPU_ADD);
    }
    auto* e=C3D_GetTexEnv(5);C3D_TexEnvInit(e);C3D_TexEnvColor(e,fogColor);
    C3D_TexEnvSrc(e,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE2);
    C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);
    C3D_TexBind(2,&retailFogRamp_);retailFogBound_=true;
    return false;
}

void Renderer::retailEndSubmesh(){
    if(retailLightDirty_){
        C3D_FVUnifSet(GPU_VERTEX_SHADER,lightDiffLoc_,0,0,0,0);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,lightDirLoc_,0,0,1,LegacyCeiling);
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,diffuseLightingLoc_,false);
        retailLightDirty_=false;
    }
    if(retailFogBound_){C3D_TexBind(2,nullptr);retailFogBound_=false;}
    if(retailTextureDirty_){
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,retailBoolLoc_,false);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[0],1,0,0,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[1],0,1,0,0);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[2],0,0,1,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,1,0,0,0);
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,false);
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentTexture2Loc_,false);
        retailTextureDirty_=false;
    }
}
}
