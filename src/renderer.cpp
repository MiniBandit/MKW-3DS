#include "mkw/env_animation.hpp"
#include "mkw/road_sampler_policy.hpp"
#include "mkw/course_phase_ranges.hpp"
#include "mkw/rainbow_shadow.hpp"
#include "mkw/renderer.hpp"
#include <mkw/normal_matrix.hpp>
#include "mkw/castle_regions.hpp"
#include "mkw/stadium_screen.hpp"
#include "mkw/render_quality.hpp"
#include "mkw/coin_hud_camera.hpp"
#include "mkw/bloom_math.hpp"
#include "mkw/race_effects.hpp"
#include "mkw/nw4r_ef_draw.hpp"
#include "mkw/retail_lighting.hpp"
#include <array>
#include <mkw/menu_ui.hpp>
#include <mkw/render_metrics.hpp>
#include <mkw/asset_pack.hpp>
#include "shader_data.hpp"
#include <tex3ds.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <algorithm>
#include <cstdlib>
#include <cstddef>
namespace mkw {
static_assert((retail::FixedEnvironment&rainbowShadow::Flag)==0);
static_assert((retail::StaticLighting&rainbowShadow::Flag)==0);
namespace {
GPU_CULLMODE cameraCull(GPU_CULLMODE mode,const C3D_Mtx& v){
    const float determinant=v.r[0].x*(v.r[1].y*v.r[2].z-v.r[1].z*v.r[2].y)
        -v.r[0].y*(v.r[1].x*v.r[2].z-v.r[1].z*v.r[2].x)
        +v.r[0].z*(v.r[1].x*v.r[2].y-v.r[1].y*v.r[2].x);
    if(determinant<0){
        if(mode==GPU_CULL_FRONT_CCW)return GPU_CULL_BACK_CCW;
        if(mode==GPU_CULL_BACK_CCW)return GPU_CULL_FRONT_CCW;
    }
    return mode;
}
void uploadProjection(int location,const C3D_Mtx& p){
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,location,&p);
}
void sceneDepthMap(const C3D_Mtx&,uint32_t layer=0){
    // HW08 factorization did not fix the physical-console symptom. Keep
    // GX's finite perspective/near/far clipping, not that failed workaround.
    const float steps=layer&retail::RaisedRoadLayer?512.f:layer&retail::CoplanarLayer?32.f:0.f;
    C3D_DepthMap(true,-1.f,steps/16777215.f);
}
}
// Translated combiner programs and texture sizes for the loaded RaceEffects table.
struct EffectDrawState {
    const RaceEffects* owner=nullptr;
    size_t resources=0;
    std::vector<ef::Material> materials;
    std::vector<ef::TextureSize> sizes;
    ef::DrawList list;
    unsigned generation=~0u,diagnosticMask=0;
};
}

namespace mkw {
namespace {
// Shared by the main thread and the menu asset loader thread.
struct TextureCache {
    LightLock lock;
    std::map<std::string,std::weak_ptr<Texture>> entries;
    TextureCache(){LightLock_Init(&lock);}
};
TextureCache& textureCache() {
    static TextureCache value;
    return value;
}
// One FS request per file. Buffered stdio would issue a request per 1 KiB,
// each with the SD card's fixed command latency.
bool readFile(const char* path,std::vector<uint8_t>& out) {
    FILE* f=std::fopen(path,"rb"); if(!f) return false;
    std::setvbuf(f,nullptr,_IONBF,0);
    bool ok=std::fseek(f,0,SEEK_END)==0;const long size=ok?std::ftell(f):-1;
    ok=ok&&size>=0&&std::fseek(f,0,SEEK_SET)==0;
    if(ok){out.resize(size_t(size));ok=std::fread(out.data(),1,out.size(),f)==out.size();}
    std::fclose(f);return ok;
}
bool readExact(FILE* f,void* p,size_t size) { return std::fread(p,1,size,f)==size; }
// A 3D screen: New 3DS or New 3DS XL. Old models never draw a second eye; the
// New 2DS XL reports New 3DS but has no parallax barrier. On a 3D screen,
// also the console's stereo display settings (config block 0x50005), which
// Mario Kart 7's stereo camera reads; the defaults stand if they are absent.
bool stereoScreen(mkw::stereo::Display& display){
    bool n3ds=false;
    if(R_FAILED(APT_CheckNew3DS(&n3ds))||!n3ds||R_FAILED(cfguInit()))return false;
    u8 model=CFG_MODEL_N2DSXL,threeD=0;
    const bool ok=R_SUCCEEDED(CFGU_GetSystemModel(&model))&&R_SUCCEEDED(CFGU_GetModelNintendo2DS(&threeD))
        &&threeD&&(model==CFG_MODEL_N3DS||model==CFG_MODEL_N3DSXL);
    float block[8]{};
    if(ok&&R_SUCCEEDED(CFGU_GetConfigInfoBlk2(sizeof block,0x50005,block))){
        const mkw::stereo::Display read{block[0],block[1],block[2],block[3],block[4]};
        if(read.valid())display=read;
    }
    cfguExit();
    return ok;
}
constexpr u32 TopTransfer=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
}
Texture::~Texture() { reset(); }
void Texture::reset() {
    if(ready) C3D_TexDelete(&gpu);
    gpu={};noMipSampler={};materialSamplers={};ready=false;
}
C3D_Tex* Texture::sampledGpu(){
    if(renderQuality().mipmaps||!gpu.maxLevel)return &gpu;
    // A sampler view, not a smaller allocation. Keep gpu.maxLevel unchanged
    // for byte accounting, VRAM moves and deletion of the actual full chain.
    noMipSampler=gpu;noMipSampler.maxLevel=noMipSampler.minLevel=0;
    C3D_TexSetFilterMipmap(&noMipSampler,GPU_NEAREST);
    return &noMipSampler;
}
C3D_Tex* Texture::sampledGpu(const MaterialSampler* s,unsigned unit){
    if(!s||!s->valid)return sampledGpu();
    auto& view=materialSamplers[unit];view=gpu;
    C3D_TexSetFilter(&view,s->magFilter?GPU_LINEAR:GPU_NEAREST,
                    s->minFilter&1?GPU_LINEAR:GPU_NEAREST);
    C3D_TexSetFilterMipmap(&view,s->minFilter>=4?GPU_LINEAR:GPU_NEAREST);
    C3D_TexSetLodBias(&view,s->lodBias);
    // GX NEAR/LINEAR have no mip sampling, even when another material shares
    // this texture and needs its chain. The allocation always retains it.
    if(!renderQuality().mipmaps||s->minFilter<2)view.minLevel=view.maxLevel=0;
    return &view;
}
bool Texture::load(const char* path) {
    std::vector<uint8_t> file;
    return (pack::read(path,file)||readFile(path,file))&&loadMemory(file.data(),file.size());
}
bool Texture::loadVram(const char* path) {
    std::vector<uint8_t> file;
    return (pack::read(path,file)||readFile(path,file))&&loadMemory(file.data(),file.size(),true);
}
bool Texture::loadMemory(const void* data,size_t size,bool vram) {
    reset();
    auto t=Tex3DS_TextureImport(data,size,&gpu,nullptr,vram);
    if(!t){gpu={};return false;}
    Tex3DS_TextureFree(t);
    C3D_TexFlush(&gpu); // Make decoded LINEAR pixels visible before first sampling.
    C3D_TexSetFilter(&gpu,GPU_LINEAR,GPU_LINEAR);
    // HW10: retain the full mip chain and bilinear texel filtering, but
    // sample one bounded mip level instead of the hardware's two-level
    // interpolation path. HW09 LOD0 removes the physical-console corruption;
    // this isolates inter-level filtering without discarding mip reduction.
    if(gpu.maxLevel)C3D_TexSetFilterMipmap(&gpu,GPU_NEAREST);
    C3D_TexSetWrap(&gpu,GPU_REPEAT,GPU_REPEAT);
    ready=true; return true;
}
uint32_t Texture::bytes() const { return ready?C3D_TexCalcTotalSize(gpu.size,gpu.maxLevel):0; }
void Renderer::clearUnusedTextureCache(){
    auto& cache=textureCache();LightLock_Lock(&cache.lock);
    std::erase_if(cache.entries,[](const auto& entry){return entry.second.expired();});
    LightLock_Unlock(&cache.lock);
}
bool Texture::initMovieEtc1(std::uint16_t width, std::uint16_t height) {
    if (ready || width == 0 || height == 0 || !C3D_TexInit(&gpu, width, height, GPU_ETC1))
        return false;
    C3D_TexSetFilter(&gpu, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&gpu, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    ready = true;
    return true;
}
bool Texture::initRgba8(unsigned width,unsigned height,const std::uint8_t* pixels,std::size_t bytes) {
    if(ready||!pixels||width<8||height<8||width>1024||height>1024||(width&(width-1))||(height&(height-1))||bytes!=std::size_t(width)*height*4)return false;
    if(!C3D_TexInit(&gpu,width,height,GPU_RGBA8))return false;
    auto* out=static_cast<std::uint8_t*>(gpu.data);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        // Match tex3ds RGBA8 tile rows. Screen/model UV conversion already
        // handles the texture V convention; flipping rows here inverts Mii icons.
        const unsigned ty=y;unsigned morton=0;
        for(unsigned b=0;b<3;++b)morton|=((x>>b)&1u)<<(2*b)|((ty>>b)&1u)<<(2*b+1);
        auto* q=out+4*(((ty>>3)*(width>>3)+(x>>3))*64+morton);
        const auto* p=pixels+4*(y*width+x);
        q[0]=p[3];q[1]=p[2];q[2]=p[1];q[3]=p[0];
    }
    C3D_TexFlush(&gpu);C3D_TexSetFilter(&gpu,GPU_LINEAR,GPU_LINEAR);
    C3D_TexSetWrap(&gpu,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);ready=true;return true;
}
void Model::enableStaticCulling(bool contact){
    visibility.clear();visibility.reserve(submeshes.size());
    for(const auto& s:submeshes)visibility.push_back(buildVisibility(vertices,indices,s.first,s.count));
    contactVisibility.clear();contactCoarseVisibility.clear();
    if(contactIndices){linearFree(contactIndices);contactIndices=nullptr;}
    if(contact&&indexCount){
        contactIndices=static_cast<uint16_t*>(linearAlloc(indexCount*sizeof(uint16_t)));
        // Optional acceleration: allocation failure keeps the old safe path.
        if(contactIndices){
            std::memcpy(contactIndices,indices,indexCount*sizeof(uint16_t));
            contactVisibility.reserve(submeshes.size());contactCoarseVisibility.reserve(submeshes.size());
            for(const auto& s:submeshes){
                spatialShadowIndices(vertices,indices,contactIndices,s.first,s.count);
                contactVisibility.push_back(buildVisibility(vertices,contactIndices,s.first,s.count,16));
                contactCoarseVisibility.push_back(buildVisibility(vertices,contactIndices,s.first,s.count));
            }
            flushGpuData(contactIndices,indexCount*sizeof(uint16_t));
        }
    }
}
bool Model::enableCastleRegions(){
    if(vertexCount!=38077||indexCount!=71049||submeshes.size()!=24)return false;
    auto hash=[](const void* p,unsigned bytes){const auto* b=static_cast<const uint8_t*>(p);uint32_t h=2166136261u;for(unsigned i=0;i<bytes;++i)h=(h^b[i])*16777619u;return h;};
    // Any resource mismatch keeps the full original mesh, never a wrong mask.
    if((staticLightingBaked?sourceVertexHash:hash(vertices,vertexCount*sizeof(Vertex)))!=castleRegion::VertexHash||hash(indices,indexCount*sizeof(uint16_t))!=castleRegion::IndexHash)return false;
    if(regionIndices[0]&&regionIndices[1])return true;
    for(unsigned z=0;z<2;++z){
        std::vector<uint16_t> packed;packed.reserve(indexCount);
        regionVisibility[z].clear();regionVisibility[z].reserve(submeshes.size());
        unsigned run=0;
        for(const auto& sub:submeshes){
            const unsigned first=packed.size();
            for(unsigned at=sub.first;at<sub.first+sub.count;at+=3){
                while(at/3>=castleRegion::Runs[run].end)++run;
                if(castleRegion::Runs[run].mask&(1u<<z))packed.insert(packed.end(),indices+at,indices+at+3);
            }
            regionVisibility[z].push_back(buildVisibility(vertices,packed.data(),first,packed.size()-first));
        }
        regionIndices[z]=static_cast<uint16_t*>(linearAlloc(packed.size()*sizeof(uint16_t)));
        if(!regionIndices[z]){
            for(auto*& p:regionIndices){if(p)linearFree(p);p=nullptr;}
            for(auto& v:regionVisibility)v.clear();return false;
        }
        std::memcpy(regionIndices[z],packed.data(),packed.size()*sizeof(uint16_t));
        flushGpuData(regionIndices[z],packed.size()*sizeof(uint16_t));
    }
    return true;
}
void Model::enableSkinCulling(unsigned bones){
    skinVisibility.clear();skinVisibility.resize(submeshes.size());
    for(size_t n=0;n<submeshes.size();++n){const auto& sub=submeshes[n];
        for(unsigned at=sub.first;at<sub.first+sub.count;at+=384){
            SkinVisibilityBlock block;block.indices={at,std::min<uint32_t>(384,sub.first+sub.count-at)};
            for(unsigned k=at;k<at+block.indices.count;++k){unsigned i=indices[k];
                unsigned matrix=unsigned(skinBoneIndices?skinBoneIndices[i]:vertices[i].normal.x)/3;
                if(matrix>=bones){skinVisibility.clear();return;}
                auto found=std::find_if(block.bones.begin(),block.bones.end(),[&](const SkinBounds& b){return b.matrix==matrix;});
                if(found==block.bones.end())block.bones.push_back({matrix,{vertices[i].position,vertices[i].position}});
                else found->bounds.include(vertices[i].position);
            }
            skinVisibility[n].push_back(std::move(block));
        }
    }
}
namespace {thread_local const CourseLighting* lightingCourse=nullptr;}
void Model::setLightingCourse(const CourseLighting* course){lightingCourse=course;}
void Model::bakeStaticLighting(){bakeStaticLighting(lightingCourse);}
void Model::bakeStaticLighting(const CourseLighting* course){
    if(staticLightingBaked)return;
    std::vector<uint8_t> done(vertexCount,0);
    bool third=false;for(const auto& sub:submeshes)third|=sub.menuReflection==8;
    if(third){
        staticEnvironment2=static_cast<float*>(linearAlloc(size_t(vertexCount)*2*sizeof(float)));
        if(!staticEnvironment2)return;
        for(unsigned i=0;i<vertexCount;++i){staticEnvironment2[i*2]=vertices[i].uv1[0];staticEnvironment2[i*2+1]=vertices[i].uv1[1];}
    }
    for(auto& sub:submeshes){
        const bool env=normalEnvironment(sub.lighting,sub.menuReflection,sub.reflectionConstant)||((sub.flags&(1u<<25))&&((sub.flags>>12)&3)==3);
        for(unsigned j=sub.first;j<sub.first+sub.count;++j){const unsigned i=indices[j];if(done[i])continue;done[i]=1;
            auto& v=vertices[i];const Vec3 n=normalized(v.normal);
            const auto light=staticLightFactor(course,sub.lighting,sub.ambient,n);
            for(unsigned c=0;c<4;++c)v.color[c]=uint8_t(std::clamp(std::lround(v.color[c]*light[c]),0l,255l));
            if(env){const float u=.5f-.5f*n.x,w=.5f+.5f*(.948683298f*n.y+.316227766f*n.z);
                if(sub.menuReflection==8){const auto* t=sub.reflectionUv;staticEnvironment2[2*i]=t[0]*u-t[1]*w+t[1]+t[2];staticEnvironment2[2*i+1]=-t[3]*u+t[4]*w+1.f-t[4]-t[5];}
                else if(sub.lighting&retail::PrimaryEnvironment){const auto* t=sub.reflectionUv;v.uv[0]=t[0]*u-t[1]*w+t[1]+t[2];v.uv[1]=-t[3]*u+t[4]*w+1.f-t[4]-t[5];}
                else {v.uv1[0]=u;v.uv1[1]=w;}
            }
        }
        sub.lighting|=retail::StaticLighting;
        if(env)sub.lighting|=retail::FixedEnvironment;
    }
    if(skinBoneIndices)for(unsigned i=0;i<vertexCount;++i)vertices[i].normal={skinBoneIndices[i],0,0};
    staticLightingBaked=true;
    flushGpuData(vertices,vertexCount*sizeof(Vertex));
    if(staticEnvironment2)flushGpuData(staticEnvironment2,size_t(vertexCount)*2*sizeof(float));
}
void Model::bakeEnvironment(const float* palette,unsigned stride,Vec3 referenceUp){
    referenceUp=normalized(referenceUp,{0,1,0});
    if(staticLightingBaked)return;
    // Stable rear-follow reference in model coordinates. Course lm_0/lm_1
    // are already generated once on load; retain their authored TEV colours.
    // UV1 is independent of face UV0/visibility and of animated wheel spin.
    std::vector<uint8_t> done(vertexCount,0);
    for(auto& sub:submeshes){
        if(((sub.lighting&retail::ChannelMask)>>retail::ChannelShift)!=2&&sub.menuReflection!=1&&sub.menuReflection!=2)continue;
        sub.lighting|=retail::FixedEnvironment;
        for(unsigned j=sub.first;j<sub.first+sub.count;++j){
            const unsigned i=indices[j];if(done[i])continue;done[i]=1;
            auto& v=vertices[i];Vec3 n=v.normal;
            if(palette&&skinBoneIndices){const auto* m=palette+unsigned(skinBoneIndices[i]/3)*stride;
                n={m[12]*n.x+m[13]*n.y+m[14]*n.z,m[15]*n.x+m[16]*n.y+m[17]*n.z,m[18]*n.x+m[19]*n.y+m[20]*n.z};}
            n=normalized(n,{0,1,0});
            v.uv1[0]=.5f-.5f*n.x;
            v.uv1[1]=.5f+.5f*dot(referenceUp,n);
        }
    }
    flushGpuData(vertices,vertexCount*sizeof(Vertex));
}
void Model::freezeLighting(const float* palette,unsigned stride){
    if(fixedLighting||staticLightingBaked)return;
    fixedLighting=true;
    for(unsigned i=0;i<vertexCount;++i){auto& v=vertices[i];Vec3 n=v.normal;
        if(palette&&skinBoneIndices){const auto* m=palette+unsigned(skinBoneIndices[i]/3)*stride;
            n={m[12]*n.x+m[13]*n.y+m[14]*n.z,m[15]*n.x+m[16]*n.y+m[17]*n.z,m[18]*n.x+m[19]*n.y+m[20]*n.z};}
        n=normalized(n);const float shade=.78f+.22f*std::max(0.f,dot(n,Vec3{-.35f,.8f,.4873397f}));
        for(unsigned c=0;c<3;++c)v.color[c]=uint8_t(std::clamp(std::lround(v.color[c]*shade),0l,255l));
        // Unlit skinning only needs the position palette; omit inverse-transpose normals.
        if(skinBoneIndices)v.normal={skinBoneIndices[i],0,0};
    }
    for(auto& sub:submeshes){
        sub.lighting&=~(retail::ChannelMask|retail::PrimaryEnvironment|retail::AlphaLit);
    }
    flushGpuData(vertices,vertexCount*sizeof(Vertex));
}
Model::~Model() { if(staticEnvironment2)linearFree(staticEnvironment2); for(auto* p:regionIndices)if(p)linearFree(p);if(contactIndices)linearFree(contactIndices);if(vertices)linearFree(vertices);if(indices)linearFree(indices);if(skinBoneIndices)linearFree(skinBoneIndices); }
bool Model::load(const char* path,bool dynamicLighting) {
    // Menu models come from the open menu.pak (one seek+read). Others stream
    // large payloads straight into LINEAR memory; a 32 KiB stdio buffer
    // coalesces the header/submesh/name reads into few FS requests.
    struct File {
        FILE* f=nullptr;char buffer[32*1024];
        std::vector<uint8_t> packed;size_t offset=0;
        explicit File(const char* path){
            if(pack::read(path,packed))return;
            f=std::fopen(path,"rb");if(f)std::setvbuf(f,buffer,_IOFBF,sizeof buffer);
        }
        ~File(){if(f)std::fclose(f);}
        bool open()const{return f||!packed.empty();}
        bool read(void* p,size_t size){
            if(f)return std::fread(p,1,size,f)==size;
            if(size>packed.size()-offset)return false;
            std::memcpy(p,packed.data()+offset,size);offset+=size;return true;
        }
        bool atEnd(){
            if(!f)return offset==packed.size();
            const int next=std::fgetc(f);if(next!=EOF)std::ungetc(next,f);
            return next==EOF;
        }
    };
    auto file=std::make_unique<File>(path);auto& f=*file;
    if(!f.open()){
#ifndef MKW_RELEASE
std::printf("Missing model: %s\n",path);
#endif
return false;}
    uint32_t h[6];
    if(!f.read(h,sizeof h)||std::memcmp(h,"GMDL",4)||(h[1]!=3&&h[1]!=4&&h[1]!=5)||!h[2]||h[2]>65535||h[3]>3000000||h[4]>2048||h[5]>512) return false;
    vertexCount=h[2];indexCount=h[3]; submeshes.resize(h[4]);
    // v3 records end at animationFrames; v4 adds the retail lighting state.
    const size_t record=h[1]==5?sizeof(Submesh):h[1]==4?offsetof(Submesh,menuReflection):offsetof(Submesh,lighting);
    for(auto& s:submeshes)if(!f.read(&s,record))return false;
    for(const auto& s:submeshes) if(s.first>indexCount||s.count>indexCount-s.first||s.count%3||s.texture< -1||s.texture>=static_cast<int32_t>(h[5])||s.secondary< -1||s.secondary>=static_cast<int32_t>(h[5]))return false;
    for(const auto& s:submeshes){
        if(s.menuReflection>31||s.menuReflection==5||(s.menuReflection&&s.menuReflection<7&&!(s.lighting&retail::Menu)))return false;
        if(s.menuReflection&&s.menuReflection!=3&&s.menuReflection!=7&&s.menuReflection!=16&&s.menuReflection!=21&&s.menuReflection!=22&&s.menuReflection!=24&&s.menuReflection!=25&&s.secondary<0)return false;
        if(s.menuReflection==8&&(!(s.reflectionConstant&0xffff)||(s.reflectionConstant&0xffff)>h[5]||(s.reflectionConstant>>20)))return false;
        for(float x:s.reflectionUv)if(!std::isfinite(x))return false;
        if((s.lighting&retail::PrimaryEnvironment)&&(h[1]!=5||(s.menuReflection&&s.menuReflection!=16&&s.menuReflection!=18)||s.texture<0))return false;
    }
    for(uint32_t i=0;i<h[5];++i){
        char name[64]; if(!f.read(name,sizeof name)||!std::memchr(name,0,sizeof name)||std::strchr(name,'/')||std::strchr(name,'\\')||std::strstr(name,".."))return false;
        std::string texPath="textures/";texPath+=name;texPath+=".t3x";
        // Partitioned model parts share one GPU texture allocation. Weak cache
        // ownership lets each scene release its textures when its models die.
        auto& cache=textureCache();
        std::shared_ptr<Texture> t;
        {
            // The weak cache spans authored scene transitions; keep its map
            // nodes and control blocks out of the discardable scene heap.
            LightLock_Lock(&cache.lock);t=cache.entries[texPath].lock();LightLock_Unlock(&cache.lock);
            if(!t) {
                // Loaded outside the lock; a concurrent duplicate load of the
                // same file is harmless and the first published entry wins.
                auto loaded=std::make_shared<Texture>();
                if(!loaded->load(texPath.c_str())){
#ifndef MKW_RELEASE
std::printf("Missing texture: %s\n",name);
#endif
return false;}
                LightLock_Lock(&cache.lock);
                t=cache.entries[texPath].lock();
                if(!t){t=std::move(loaded);cache.entries[texPath]=t;}
                LightLock_Unlock(&cache.lock);
            }
        }
        textures.push_back(std::move(t));
    }
    vertices=static_cast<Vertex*>(linearAlloc(vertexCount*sizeof(Vertex)));
    indices=static_cast<uint16_t*>(linearAlloc(indexCount*sizeof(uint16_t)));
    if(!vertices||!indices||!f.read(vertices,vertexCount*sizeof(Vertex))||!f.read(indices,indexCount*sizeof(uint16_t)))return false;
    if(std::any_of(submeshes.begin(),submeshes.end(),[](const Submesh& s){return s.menuReflection==11;})){
        uint32_t header[2];
        if(!f.read(header,sizeof header)||std::memcmp(header,"WIN2",4)||!header[1]||header[1]>32)return false;
        waterIndirect.resize(header[1]);
        for(auto& field:waterIndirect)if(!field.read(f))return false;
        for(const auto& s:submeshes)if(s.menuReflection==11&&(!s.reflectionConstant||s.reflectionConstant>waterIndirect.size()))return false;
    }
    while(!f.atEnd()){
        uint32_t header[2];
        if(!f.read(header,sizeof header)||!header[1]||header[1]>submeshes.size())return false;
        if(!std::memcmp(header,"SMP1",4)){
            if(!samplers.empty()||header[1]!=submeshes.size())return false;
            samplers.resize(header[1]);
            if(!f.read(samplers.data(),samplers.size()*sizeof(samplers[0])))return false;
            for(const auto& row:samplers)for(const auto& s:row)
                if(!std::isfinite(s.lodBias)||s.lodBias< -16||s.lodBias>=16||s.minFilter>5||s.magFilter>1||s.valid>1||s.reserved)return false;
            continue;
        }
        if(std::memcmp(header,"UVM1",4)||!uvTracks.empty())return false;
        uvTracks.resize(submeshes.size());
        for(unsigned n=0;n<header[1];++n){
            uint32_t h[3];
            if(!f.read(h,sizeof h)||h[0]>=uvTracks.size()||!h[1]||h[1]>65535||h[2]>1)return false;
            auto& t=uvTracks[h[0]];if(t.frames)return false;
            t.frames=h[1];t.loop=h[2];t.samples.resize(size_t(t.frames)*12);
            if(!f.read(t.samples.data(),t.samples.size()*sizeof(float)))return false;
            for(float v:t.samples)if(!std::isfinite(v))return false;
        }
    }
    if(!f.atEnd())return false;
    file.reset();
    for(uint32_t i=0;i<indexCount;++i)if(indices[i]>=vertexCount)return false;
    minimum=maximum=vertices[0].position;
    for(uint32_t i=0;i<vertexCount;++i){auto p=vertices[i].position;if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return false;
        minimum={std::min(minimum.x,p.x),std::min(minimum.y,p.y),std::min(minimum.z,p.z)};
        maximum={std::max(maximum.x,p.x),std::max(maximum.y,p.y),std::max(maximum.z,p.z)};}
    sourceVertexHash=2166136261u;
    for(size_t i=0;i<size_t(vertexCount)*sizeof(Vertex);++i)sourceVertexHash=(sourceVertexHash^reinterpret_cast<const uint8_t*>(vertices)[i])*16777619u;
    coursePhase::apply(*this,path);
    rainbowShadow::apply(*this,path);
    roadSamplerPolicy=roadSampling::apply(*this,path);
    // Pokey's 128px face/body atlas has a retail +2 mip bias. On the
    // smaller 3DS viewport this merges the dark eye rim into the pale face
    // too early. Keep original texture/wrap/trilinear filtering, but sample
    // this actor's atlas without that extra two-level blur (head parts too).
    if(std::strstr(path,"_sanbo__"))for(auto& row:samplers)
        for(auto& sampler:row)if(sampler.valid&&sampler.lodBias>0.f)sampler.lodBias=0.f;
    // Original kumo/thunder_m: T0*raster + lerp(K1,C1,EnvCamera(kumo)).
    // Older item exports kept only T0*raster. Restore the authored second
    // stage before the remote model bakes its fixed environment coordinates.
    if(std::strstr(path,"models/item_14.gmdl")&&submeshes.size()==4&&textures.size()==6){
        auto& bolt=submeshes[3];bolt.secondary=0;bolt.menuReflection=26;
        bolt.reflectionConstant=0x0000636c;bolt.reflectionAdd=0x00e5f9fd;
        constexpr float boltSrt[6]={-.359517634f,.175348461f,.412084579f,-.416452587f,-.853854358f,.875153482f};
        std::copy(std::begin(boltSrt),std::end(boltSrt),bolt.reflectionUv);
    }
    if(!dynamicLighting){
        // Bake both FlagB LODs with the authored near material's diffuse
        // response. The far unlit material would otherwise pop brighter.
        if(std::strstr(path,"obj07_flagb1__"))for(auto& sub:submeshes){
            sub.lighting=519;sub.ambient=0xa3a3a3ff;
        }
        bakeStaticLighting(lightingCourse);if(!staticLightingBaked)return false;
        // bombCore has a custom third-texture binder, so no generic reflection
        // mode allocates its UV2. Static shaders still require that attribute.
        // Original glowWhite3s uses an identity SRT at all 56 frames.
        if(std::strstr(path,"models/bomb_blast.gmdl")){
            staticEnvironment2=static_cast<float*>(linearAlloc(size_t(vertexCount)*2*sizeof(float)));
            if(!staticEnvironment2)return false;
            for(unsigned i=0;i<vertexCount;++i){const auto n=normalized(vertices[i].normal);
                staticEnvironment2[2*i]=.5f-.5f*n.x;
                staticEnvironment2[2*i+1]=.5f+.5f*(.948683298f*n.y+.316227766f*n.z);
            }
            flushGpuData(staticEnvironment2,size_t(vertexCount)*2*sizeof(float));
        }
    }
    flushGpuData(vertices,vertexCount*sizeof(Vertex));flushGpuData(indices,indexCount*sizeof(uint16_t));
    return true;
}
Scene::~Scene(){for(auto m:models)delete m;}
bool Scene::load(const char* path){
    FILE* f=std::fopen(path,"rb");if(!f)return false;
    uint32_t h[3];
    if(!readExact(f,h,sizeof h)||std::memcmp(h,"GSCN",4)||h[1]!=1||h[2]>10000||!readExact(f,&spawn,sizeof spawn)||!readExact(f,&spawnRotation,sizeof spawnRotation)){std::fclose(f);return false;}
    std::map<std::string,Model*> cache;
    for(uint32_t i=0;i<h[2];++i){
        char name[64];float m[12];
        if(!readExact(f,name,sizeof name)||!readExact(f,m,sizeof m)||!std::memchr(name,0,sizeof name)||std::strstr(name,"..")||std::strchr(name,'/')||std::strchr(name,'\\')){std::fclose(f);return false;}
        Model* model=nullptr;auto it=cache.find(name);
        if(it!=cache.end())model=it->second;
        else{model=new Model;models.push_back(model);std::string p="models/";p+=name;p+=".gmdl";
            if(!model->load(p.c_str())){std::fclose(f);return false;}cache[name]=model;}
        Instance instance;instance.model=model;Mtx_Identity(&instance.transform);
        for(int row=0;row<3;++row)for(int col=0;col<4;++col){if(!std::isfinite(m[row*4+col])){std::fclose(f);return false;}instance.transform.r[row].c[3-col]=m[row*4+col];}
        instances.push_back(instance);
    }
    bool valid=std::fgetc(f)==EOF;std::fclose(f);return valid;
}
bool Renderer::init(){
    if(!C3D_Init(0x100000))return false;
    deviceReady_=true;
    auto fail=[this](){shutdown();return false;};
    target_=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
    if(!target_)return fail();
    C3D_RenderTargetSetOutput(target_,GFX_TOP,GFX_LEFT,TopTransfer);
    // libctru's gfxInit already allocates both top-screen eyes (800 rows) and
    // gfxSet3D only switches the display mode: the right eye adds this VRAM
    // target and no LINEAR. It is transferred only in frames that draw it.
    if(stereoScreen(stereoDisplay_)&&(targetRight_=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8)))
        C3D_RenderTargetSetOutput(targetRight_,GFX_TOP,GFX_RIGHT,TopTransfer);
    {C3D_Mtx tilt;Mtx_PerspTilt(&tilt,C3D_AngleFromDegrees(90.f),1.f,1.f,2.f,false);horizontalSign_=tilt.r[1].x<0?-1.f:1.f;verticalSign_=tilt.r[0].y<0?-1.f:1.f;}
    shader_=DVLB_ParseFile(reinterpret_cast<u32*>(const_cast<uint8_t*>(mkw_shader)),sizeof mkw_shader);if(!shader_)return fail();
    shaderProgramInit(&program_);programReady_=true;
    if(R_FAILED(shaderProgramSetVsh(&program_,&shader_->DVLE[0])))return fail();
    projectionLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"projection");
    modelViewLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"modelView");
    lightLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"light");
    uvScrollLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"uvScroll");
    envRowLoc_[0]=shaderInstanceGetUniformLocation(program_.vertexShader,"envRow0");
    envRowLoc_[1]=shaderInstanceGetUniformLocation(program_.vertexShader,"envRow1");
    envRowLoc_[2]=shaderInstanceGetUniformLocation(program_.vertexShader,"envRow2");
    envParamLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"envParam");
    staticLightingLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"staticLighting");
    projectedWaterLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"projectedWater");
    playerShadowProjectLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"playerShadowProject");
    shadowCaptureLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"shadowCapture");
    shadowReceiverFadeLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"shadowReceiverFade");
    shadowDepthLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"shadowDepth");
    shadowFloorLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"shadowFloor");
    if(staticLightingLoc_<0||projectedWaterLoc_<0||playerShadowProjectLoc_<0||shadowCaptureLoc_<0||shadowDepthLoc_<0||shadowFloorLoc_<0||shadowReceiverFadeLoc_<0)return fail();
    static const char* uvRows[]={"uvRow0","uvRow1","uvRow2","uvRow3"};
    for(int i=0;i<4;++i)uvRowLoc_[i]=shaderInstanceGetUniformLocation(program_.vertexShader,uvRows[i]);
    tintLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"tint");
    uv1RowLoc_[0]=shaderInstanceGetUniformLocation(program_.vertexShader,"uv1Row0");
    uv1RowLoc_[1]=shaderInstanceGetUniformLocation(program_.vertexShader,"uv1Row1");
    if(projectionLoc_<0||modelViewLoc_<0||lightLoc_<0||uvScrollLoc_<0||envRowLoc_[0]<0||envRowLoc_[1]<0||envRowLoc_[2]<0||envParamLoc_<0||
       uv1RowLoc_[0]<0||uv1RowLoc_[1]<0)return fail();
    if(uvRowLoc_[0]<0||uvRowLoc_[1]<0||uvRowLoc_[2]<0||uvRowLoc_[3]<0||tintLoc_<0)return fail();
    if(!retailUniformLocations())return fail();
    bonesLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"bones");
    skinnedLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"skinned");
    skinnedNormalsLoc_=shaderInstanceGetUniformLocation(program_.vertexShader,"skinnedNormals");
    if(bonesLoc_<0||skinnedLoc_<0||skinnedNormalsLoc_<0)return fail();
    if(!C3D_TexInit(&white_,8,8,GPU_RGBA8))return fail();
    // Blackout, cinema bars and fades may all submit in one frame. Each draw
    // keeps distinct vertices until the next FrameBegin drains GPU use.
    overlay_=static_cast<Vertex*>(linearAlloc(2*kOverlayCapacity*sizeof(Vertex)));if(!overlay_)return fail();
    sprites_=static_cast<Vertex*>(linearAlloc(kSpriteCapacity*6*sizeof(Vertex)));if(!sprites_)return fail();
    text_=static_cast<Vertex*>(linearAlloc(kTextVertices*sizeof(Vertex)));if(!text_)return fail();
    particles_=static_cast<Vertex*>(linearAlloc(DriftEffects::Capacity*12*sizeof(Vertex)));if(!particles_)return fail();
    effectVertices_=linearAlloc(kEffectVertices*sizeof(ef::DrawVertex));if(!effectVertices_)return fail();
    if(shader_->numDVLE<3)return fail();
    shaderProgramInit(&miiProgram_);miiProgramReady_=true;
    if(R_FAILED(shaderProgramSetVsh(&miiProgram_,&shader_->DVLE[2])))return fail();
    // The variant deliberately retains all scene uniform declarations/slots.
    // Reject incompatible layouts instead of drawing with misbound matrices.
    for(unsigned i=0;i<shader_->DVLE[0].uniformTableSize;++i){
        const auto& u=shader_->DVLE[0].uniformTableData[i];
        const char* name=shader_->DVLE[0].symbolTableData+u.symbolOffset;
        if(shaderInstanceGetUniformLocation(program_.vertexShader,name)!=shaderInstanceGetUniformLocation(miiProgram_.vertexShader,name))return fail();
    }
    shaderProgramInit(&effectProgram_);effectProgramReady_=true;
    if(R_FAILED(shaderProgramSetVsh(&effectProgram_,&shader_->DVLE[1])))return fail();
    effectProjectionLoc_=shaderInstanceGetUniformLocation(effectProgram_.vertexShader,"projection");
    if(effectProjectionLoc_<0)return fail();
    if(!C3D_TexInit(&effectLookup_,ef::DrawList::LookupSize,ef::DrawList::LookupSize,GPU_RGBA8))return fail();
    C3D_TexSetFilter(&effectLookup_,GPU_NEAREST,GPU_NEAREST);C3D_TexSetWrap(&effectLookup_,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    effectDraw_=new EffectDrawState;
    std::memset(white_.data,255,8*8*4);C3D_TexFlush(&white_);
    Mtx_PerspTilt(&projection_,C3D_AngleFromDegrees(55.f),mkw::display::Aspect,10.f,400000.f,false);
    mainProjection_=cullProjection_=projection_;
    ready_=true;return true;
}
bool Renderer::enableBottom(){
    if(bottomTarget_)return true;
    if(!deviceReady_)return false;
    // citro3d's linked-target table is read by FrameEnd on the render thread.
    waitForGpu();
    // Shared by the map, menu vehicle icons and the continuous intro view.
    // Depth/stencil adds 320*240*4 bytes; it is cleared once at frame begin.
    bottomTarget_=C3D_RenderTargetCreate(240,320,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
    if(!bottomTarget_)return false;
    // A text console changes the LCD to RGB565/single buffering. SetOutput
    // only links the target: its RGB8 transfer does not restore the LCD format.
    gfxSetScreenFormat(GFX_BOTTOM,GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_BOTTOM,true);
    presentedBottom_=nullptr;
    C3D_RenderTargetSetOutput(bottomTarget_,GFX_BOTTOM,GFX_LEFT,TopTransfer);
    return true;
}
void Renderer::releaseBottom(){
    if(!bottomTarget_)return;
    // No queued frame or transfer still reads the target (or the framebuffers).
    waitForGpu();
    C3D_RenderTargetDelete(bottomTarget_);bottomTarget_=nullptr;
    presentedBottom_=nullptr;
    // The lower LCD shows its last transferred frame until something replaces
    // it: blank both BGR8 buffers when releasing lower-screen rendering.
    for(unsigned i=0;i<2;++i){
        u16 w=0,h=0;u8* framebuffer=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);
        if(framebuffer){std::memset(framebuffer,0,size_t(w)*h*3);GSPGPU_FlushDataCache(framebuffer,u32(w)*h*3);}
        gfxScreenSwapBuffers(GFX_BOTTOM,false);
    }
}
bool Renderer::reserveBloom(){
    if(!deviceReady_)return false;
    if(sceneTarget_)return true;
    drain();
    return initBloomTargets();
}
bool Renderer::loadVramTexture(Texture& texture,const char* path,GPU_TEXTURE_WRAP_PARAM wrap){
    texture.reset();
    if(!ready_||!text_)return false;
    // The GPU and the render thread are idle: nothing reads the staging region.
    waitForGpu();
    // Packed (race.pak) entries are read by range; loose files are streamed.
    const bool packed=pack::contains(path);
    FILE* f=packed?nullptr:std::fopen(path,"rb");
    if(!packed&&!f)return false;
    std::size_t offset=0;
    auto read=[&](void* out,std::size_t bytes){
        const bool ok=packed?pack::readRange(path,offset,out,bytes):std::fread(out,1,bytes,f)==bytes;
        offset+=bytes;return ok;
    };
    // tex3ds: u16 subtextures, u8 log2 sizes and type, u8 format, u8 mipmaps;
    // 12-byte subtexture records; a 4-byte compression header (0x00 = none).
    uint8_t header[5]{},sub[12]{},compression[4]{};
    bool ok=read(header,5)&&(header[0]|header[1]<<8)==1&&read(sub,12)&&read(compression,4)&&compression[0]==0;
    const unsigned width=8u<<(header[2]&7),height=8u<<((header[2]>>3)&7),format=header[3];
    const std::size_t bytes=std::size_t(compression[1])|std::size_t(compression[2])<<8|std::size_t(compression[3])<<16;
    // Offline minimap object atlases use RGBA4 (two bytes per texel).
    // Validate its payload before allocating, just like the existing HUD formats.
    const std::size_t baseBytes=format==GPU_ETC1?width*height/2:
        format==GPU_RGBA4?width*height*2:width*height*4;
    ok=ok&&!(header[2]&0x40)&&header[4]==0&&width<=1024&&height<=1024&&
        (format==GPU_ETC1||format==GPU_RGBA8||format==GPU_RGBA4)&&
        bytes==C3D_TexCalcTotalSize(baseBytes,0);
    // VRAM only: a failure leaves the texture empty and never falls back to LINEAR.
    ok=ok&&C3D_TexInitVRAM(&texture.gpu,uint16_t(width),uint16_t(height),GPU_TEXCOLOR(format));
    if(!ok){texture.gpu={};if(f)std::fclose(f);return false;}
    uint8_t* staging=reinterpret_cast<uint8_t*>(text_);
    for(std::size_t done=0;ok&&done<bytes;){
        const std::size_t chunk=std::min(kStagingBytes,bytes-done);
        ok=read(staging,chunk);
        if(ok){
            GSPGPU_FlushDataCache(staging,u32(chunk));
            C3D_SyncTextureCopy(reinterpret_cast<u32*>(staging),0,reinterpret_cast<u32*>(static_cast<uint8_t*>(texture.gpu.data)+done),0,u32(chunk),8);
        }
        done+=chunk;
    }
    if(f)std::fclose(f);
    if(!ok){C3D_TexDelete(&texture.gpu);texture.gpu={};return false;}
    C3D_TexSetFilter(&texture.gpu,GPU_LINEAR,GPU_LINEAR);
    C3D_TexSetWrap(&texture.gpu,wrap,wrap);
    texture.ready=true;
    return true;
}
bool Renderer::moveToVram(Texture& texture,std::size_t reserve){
    auto& t=texture.gpu;
    const u32 at=reinterpret_cast<u32>(t.data);
    if(!texture.ready||!t.data||(at>=OS_VRAM_VADDR&&at<OS_VRAM_VADDR+OS_VRAM_SIZE))return false;
    const std::size_t bytes=C3D_TexCalcTotalSize(t.size,t.maxLevel);
    if(vramSpaceFree()<bytes+reserve)return false;
    C3D_Tex moved{};
    // Keep the full mip chain in VRAM too. Refusing mipmapped textures
    // would undo the Old 3DS LINEAR-memory optimization for world assets.
    const C3D_TexInitParams params{t.width,t.height,t.maxLevel,t.fmt,GPU_TEX_2D,true};
    if(!C3D_TexInitWithParams(&moved,nullptr,params))return false;
    // The LINEAR copy was flushed when it was loaded; the copy engine reads it.
    C3D_SyncTextureCopy(static_cast<u32*>(t.data),0,static_cast<u32*>(moved.data),0,u32(bytes),8);
    moved.param=t.param;moved.border=t.border;moved.lodParam=t.lodParam;
    C3D_TexDelete(&t);t=moved;
    return true;
}
void Renderer::waitForGpu(){
    if(!deviceReady_)return;
    // The render thread has drawn every recorded frame; this thread owns citro3d.
    drain();
    if(!C3D_FrameBegin(0))std::abort();
    retired_.clear();
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    gpuIdle_=true;
}
void Renderer::shutdown(){
    if(!deviceReady_)return;
    if(thread_){
        aptUnhook(&aptCookie_);
        drain();quit_.store(true,std::memory_order_release);LightEvent_Signal(&wake_);
        threadJoin(thread_,U64_MAX);threadFree(thread_);thread_=nullptr;quit_.store(false);deferred_=false;
    }
    waitForGpu();
    // Scene models are gone before shutdown. Remove their expired weak owners
    // while the allocation group that created them is still valid.
    retired_.clear();
    LightLock_Lock(&textureCache().lock);textureCache().entries.clear();LightLock_Unlock(&textureCache().lock);
    linearFree(particles_);particles_=nullptr;
    linearFree(effectVertices_);effectVertices_=nullptr;
    if(effectLookup_.data)C3D_TexDelete(&effectLookup_);
    effectLookup_={};delete effectDraw_;effectDraw_=nullptr;
    if(miiProgramReady_)shaderProgramFree(&miiProgram_);
    miiProgramReady_=false;miiProgram_={};
    if(effectProgramReady_)shaderProgramFree(&effectProgram_);
    effectProgramReady_=false;effectProgram_={};
    linearFree(sprites_);sprites_=nullptr;linearFree(overlay_);overlay_=nullptr;
    linearFree(text_);text_=nullptr;
    retailShutdown();
    if(white_.data)C3D_TexDelete(&white_);
    white_={};
    if(programReady_)shaderProgramFree(&program_);
    programReady_=false;program_={};
    if(shader_)DVLB_Free(shader_);
    shader_=nullptr;
    if(playerShadowTarget_)C3D_RenderTargetDelete(playerShadowTarget_);
    playerShadowTarget_=nullptr;if(playerShadowTex_.data)C3D_TexDelete(&playerShadowTex_);playerShadowTex_={};
    if(playerShadowClipTex_.data)C3D_TexDelete(&playerShadowClipTex_);playerShadowClipTex_={};
    playerContactTex_.reset();
    if(bottomTarget_)C3D_RenderTargetDelete(bottomTarget_);
    bottomTarget_=nullptr;
    freeBloom();freeStadiumScreen();
    if(targetRight_)C3D_RenderTargetDelete(targetRight_);
    targetRight_=nullptr;
    if(stereoShown_)gfxSet3D(false);
    stereoShown_=false;
    if(target_)C3D_RenderTargetDelete(target_);
    target_=nullptr;
    C3D_Fini();ready_=deviceReady_=false;
}
void Renderer::beginNow(const C3D_Mtx& view,const C3D_Mtx& projection,bool sync,bool begun){
    if(!begun){
        const auto waitStart=svcGetSystemTick();
        if(!C3D_FrameBegin(sync?C3D_FRAME_SYNCDRAW:0))std::abort();
        stats_.beginWaitMs=float((svcGetSystemTick()-waitStart)*1000.0/SYSCLOCK_ARM11);
        // Every queued command list has completed: the GPU reads nothing of earlier frames.
        stats_.previousGpuMs=C3D_GetDrawingTime();
        renderMetrics={};
    }
    // The previous frame has been presented. The display mode follows this
    // frame's eyes before its FrameEnd transfers the right eye (gfxGetFramebuffer
    // addresses the right half only in 3D mode).
    const bool stereo=execStereo_.active()&&targetRight_;
    if(stereo!=stereoShown_){gfxSet3D(stereo);stereoShown_=stereo;}
    eye_=0;
    // Dad's capture cadence: one sample per two rendered frames, independently
    // of simulation ticks, eye replays and the lower-screen intro pass.
    stadiumCaptureFrame_=stadiumRequested_&&((stadiumRenderFrames_++&1u)==0);
    stadiumVisible_=true;
    if(stadiumRequested_&&!stadiumScreens_.empty()&&!execDualScreenIntro_){
        stadiumVisible_=false;C3D_Mtx identity;Mtx_Identity(&identity);
        for(unsigned eye=0;eye<(execStereo_.active()?2u:1u)&&!stadiumVisible_;++eye){
            eye_=eye;
            const auto clip=ViewFrustum{view,eyeProjection(projection)}.model(identity);
            for(const auto& bounds:stadiumScreens_)if(clip.intersects(bounds)){stadiumVisible_=true;break;}
        }
        eye_=0;
    }
    view_=view;projection_=eyeProjection(projection);
    beginFrame();
}
// The right eye replays the frame's commands after the left eye's.
void Renderer::beginEyeNow(const C3D_Mtx& view,const C3D_Mtx& projection){
    eye_=1;
    view_=view;projection_=eyeProjection(projection);
    beginFrame();
}
void Renderer::beginBottomWorldNow(const C3D_Mtx& view,const C3D_Mtx& projection){
    // Camera axes stay identical, so models, sparks and EF view-space vertices
    // can be read again without writes while the top draws are queued.
    view_=view;projection_=eyeProjection(projection);
    beginFrame();
}
C3D_Mtx Renderer::eyeProjection(const C3D_Mtx& center) const{
    C3D_Mtx cropped=center;
    if(execDualScreenIntro_){
        // Top keeps the authored framing, including the camera's subject.
        // Lower starts at virtual Y320: the 80px hinge is never rendered.
        const float offset=bottomWorldPass_?display::IntroBottomClipOffset*verticalSign_:0.f;
        for(unsigned c=0;c<4;++c){
            cropped.r[0].c[c]+=offset*center.r[3].c[c];
            if(bottomWorldPass_)cropped.r[1].c[c]*=400.f/320.f;
        }
    }
    if(!execStereo_.active()||bottomWorldPass_)return cropped;
    float m[4][4];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)m[r][c]=cropped.r[r].c[3-c];
    stereo::perspectiveEye(m,1,horizontalSign_,eye_?1.f:-1.f,execStereo_);
    C3D_Mtx out;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)out.r[r].c[3-c]=m[r][c];
    return out;
}
// Top-screen pixels (origin top-left). A stereo layer moves each eye by half its disparity.
C3D_Mtx Renderer::screenProjection(float parallax) const{
    C3D_Mtx out;Mtx_OrthoTilt(&out,0,screenWidth_,240,0,-1,1,false);
    if(!execStereo_.active()||bottomPass_||parallax==0)return out;
    float m[4][4];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)m[r][c]=out.r[r].c[3-c];
    stereo::screenEye(m,1,horizontalSign_,eye_?1.f:-1.f,parallax);
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)out.r[r].c[3-c]=m[r][c];
    return out;
}
void Renderer::screenDepthNow(float parallax){
    if(parallax==screenParallax_)return;
    // The next sprite run uploads the new projection.
    flushSprites();
    screenParallax_=parallax;
}
void Renderer::beginFrame(){
    execSceneRegion_=3;
    // The right eye repeats the left eye's buffer offsets: its sprites, text,
    // overlays, sparks and effect vertices reuse the data the left eye wrote.
    screenWidth_=bottomWorldPass_?320.f:400.f;screenParallax_=0;
    spriteBase_=bottomWorldPass_?text_+kTextVertexLimit:sprites_;
    spriteCapacity_=bottomWorldPass_?kBottomSpriteCapacity:kSpriteCapacity;
    topSpriteCount_=0;bottomPass_=bottomWorldPass_;
    spriteCount_=0;spriteRunFirst_=0;spriteRunTexture_=nullptr;spriteStateReady_=false;
    menuIconDrawCount_=0;
    if(!bottomWorldPass_)textCount_=0;
    overlayCount_=0;
    // The local SDK permits unbinding all units, including inherited samplers.
    for(int unit=0;unit<3;++unit)C3D_TexBind(unit,nullptr);
    if(!eye_&&!bottomWorldPass_){
        C3D_RenderTargetClear(target_,C3D_CLEAR_ALL,0x071326FF,0);
        if(execStereo_.active())C3D_RenderTargetClear(targetRight_,C3D_CLEAR_ALL,0x071326FF,0);
        if(bottomTarget_)C3D_RenderTargetClear(bottomTarget_,C3D_CLEAR_ALL,0x071326FF,0);
    }
    // Only the left eye captures. The right eye must render directly to its
    // LCD target unless its own bloom pass needs the shared scene scratch.
    // captureStadiumScreenNow intentionally ignores the right eye, so sending
    // it to scratch here would leave that LCD blank on capture frames.
    stadiumFrame_=!eye_&&!bottomWorldPass_&&stadiumRequested_&&stadiumCaptureFrame_&&stadiumVisible_&&stadiumTex_[0].data;stadiumRequested_=false;
    bloomFrame_=!bottomWorldPass_&&bloomRequested_&&initBloom();bloomRequested_=false;
    // FrameEnd submits both eyes together. Keep all post-process vertices
    // immutable until then: the capture quad exists only in the left eye,
    // so the two eyes do not necessarily use matching quad slots.
    if(!eye_&&!bottomWorldPass_)bloomCount_=0;
    if(stadiumFrame_&&!initBloom())stadiumFrame_=false;
    if(bloomFrame_||stadiumFrame_){
        if(!eye_){
            // Memory fills run in queue order, ahead of this frame's command list.
            C3D_RenderTargetClear(sceneTarget_,C3D_CLEAR_ALL,0x071326FF,0);
            for(auto* t:bloomTarget_)C3D_RenderTargetClear(t,C3D_CLEAR_COLOR,0,0);
        }else{
            // The right eye reuses the scene target the left eye's bloom has
            // read: queue the left eye's commands first, then the clear. The
            // queue only runs at FrameEnd. Bloom targets are overwritten
            // inside their viewport and keep the frame's clear outside it.
            C3D_FrameSplit(GX_CMDLIST_FLUSH);
            C3D_RenderTargetClear(sceneTarget_,C3D_CLEAR_ALL,0x071326FF,0);
        }
        C3D_FrameDrawOn(sceneTarget_);C3D_SetViewport(0,0,240,400);
    }else C3D_FrameDrawOn(bottomWorldPass_?bottomTarget_:topTarget());
    C3D_BindProgram(&program_);
    sceneDepthMap(projection_);
    uploadProjection(projectionLoc_,projection_);
    // Environment rows stay finite; mix zero keeps every vertex's own UV1.
    C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[0],1,0,0,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[1],0,1,0,0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[2],0,0,1,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,1,0,0,0);
    uvDirty_=tintDirty_=true;resetMaterialUniforms();
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[0],1,0,0,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[1],0,1,0,0);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,shadowCaptureLoc_,false);
    retailBeginFrame();
    Vec3 sun=normalized(Vec3{0.3f,0.8f,0.5f});
    light_={view_.r[0].x*sun.x+view_.r[0].y*sun.y+view_.r[0].z*sun.z,
            view_.r[1].x*sun.x+view_.r[1].y*sun.y+view_.r[1].z*sun.z,
            view_.r[2].x*sun.x+view_.r[2].y*sun.y+view_.r[2].z*sun.z};
    C3D_AttrInfo* attr=C3D_GetAttrInfo();AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_FLOAT,3);AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);AttrInfo_AddLoader(attr,3,GPU_UNSIGNED_BYTE,4);
    for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
    auto* env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
    C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
    if(!eye_&&!bottomWorldPass_)stats_.triangles=stats_.drawCalls=stats_.culledTriangles=stats_.culledSubmeshes=0;
}
C3D_Mtx ViewFrustum::cameraFacing(Vec3 position,float scale) const {
    C3D_Mtx transform;Mtx_Identity(&transform);
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)transform.r[row].c[3-col]=view.r[col].c[3-row]*scale;
    transform.r[0].w=position.x;transform.r[1].w=position.y;transform.r[2].w=position.z;
    return transform;
}
C3D_Mtx Renderer::cameraFacing(Vec3 position,float scale) const {return ViewFrustum{mainView_,mainProjection_}.cameraFacing(position,scale);}
Vec3 Renderer::cameraPosition() const {
    // view = [R|t] with R orthonormal: camera = -R^T t.
    const auto& v=mainView_;
    const float t[3]={v.r[0].w,v.r[1].w,v.r[2].w};
    Vec3 p{};
    p.x=-(v.r[0].x*t[0]+v.r[1].x*t[1]+v.r[2].x*t[2]);
    p.y=-(v.r[0].y*t[0]+v.r[1].y*t[1]+v.r[2].y*t[2]);
    p.z=-(v.r[0].z*t[0]+v.r[1].z*t[1]+v.r[2].z*t[2]);
    return p;
}
namespace {
ClipFrustum objectFrustum(const C3D_Mtx& view,const C3D_Mtx& projection,const C3D_Mtx& transform){
    C3D_Mtx mv,clip;Mtx_Multiply(&mv,&view,&transform);Mtx_Multiply(&clip,&projection,&mv);float rows[16];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=clip.r[r].c[3-c];
    return ClipFrustum(rows);
}
}
ClipFrustum ViewFrustum::model(const C3D_Mtx& transform) const {return objectFrustum(view,projection,transform);}
ClipFrustum Renderer::modelFrustum(const C3D_Mtx& transform) const {return objectFrustum(mainView_,cullProjection_,transform);}
bool Renderer::visible(const Model& model,const C3D_Mtx& transform) const {
    return modelFrustum(transform).intersects({model.minimum,model.maximum});
}
void Renderer::drawAtScreenNow(const Model& model,const Vertex* vertices,const C3D_Mtx& transform,float x,float y,float brightness,float scaleX,float scaleY,MaterialBinder material,void* context){
    flushSprites();
    const C3D_Mtx original=projection_;
    C3D_Mtx tilt;Mtx_PerspTilt(&tilt,C3D_AngleFromDegrees(90.f),1.f,1.f,2.f,false);
    const float vertical=(tilt.r[0].y<0?-1.f:1.f)*(120.f-y)/120.f;
    const float horizontal=(tilt.r[1].x<0?-1.f:1.f)*(x-screenWidth_*.5f)/(screenWidth_*.5f);
    for(unsigned col=0;col<4;++col){
        projection_.r[0].c[col]=scaleY*original.r[0].c[col]+vertical*original.r[3].c[col];
        projection_.r[1].c[col]=scaleX*original.r[1].c[col]+horizontal*original.r[3].c[col];
    }
    const float originalBrightness=modelBrightness_;modelBrightness_=brightness;
    drawNow(model,vertices,transform,false,material,context,nullptr);
    modelBrightness_=originalBrightness;
    projection_=original;
}
void Renderer::drawMenuModelLayerNow(const Model& model,const Vertex* vertices,const C3D_Mtx& transform){
    flushSprites();
    const auto oldProjection=projection_,oldView=view_;
    const bool oldLighting=courseLighting_;
    // Use the port's full-frame menu camera (MenuModels::begin), not the
    // close-range camera used for captured vehicle icons. The original trophy
    // mesh straddles Z=0, so the icon eye at Z=15 would clip through it.
    // Framing remains a target-visual comparison item.
    Mtx_PerspTilt(&projection_,C3D_AngleFromDegrees(45.f),5.f/3.f,10.f,100000.f,false);
    Mtx_LookAt(&view_,FVec3_New(0,0,1000),FVec3_New(0,0,0),FVec3_New(0,1,0),false);
    // Background models must remain behind every captured vehicle pane
    // (0.90 and above), while keeping their own internal depth ordering.
    // Map the layer after interpolation. Baking the interval into clip Z
    // discards fine surface separation in PICA's float24 vertex arithmetic.
    const float oldDepthBase=menuDepthBase_,oldDepthSpan=menuDepthSpan_;
    menuDepthBase_=.8f;menuDepthSpan_=.1f;
    // This is the same full-frame UI image in both eyes, independent of the
    // outdoor camera's convergence. Skin/material commands stay active.
    courseLighting_=false;
    drawNow(model,vertices,transform,false,nullptr,nullptr,nullptr);
    menuDepthBase_=oldDepthBase;menuDepthSpan_=oldDepthSpan;
    projection_=oldProjection;view_=oldView;courseLighting_=oldLighting;
}
void Renderer::drawCoinHudModelNow(const Model& model,const Vertex* vertices,const C3D_Mtx& transform,bool firstColumn){
    flushSprites();
    // Native805a97c0 removes world groups0/1 and draws2 in its own depth
    // range. PICA world draws use the entire depth buffer: reset depth only
    // at the final world->HUD boundary, keeping the colour image and depth
    // comparisons within/between all columns. No world draw follows this
    // layer. A quad avoids a GPU queue split or another render target.
    if(firstColumn)screenFadeNow(0,false,true);
    const auto oldProjection=projection_,oldView=view_;
    const bool oldLighting=courseLighting_;
    const float oldDepthBase=menuDepthBase_,oldDepthSpan=menuDepthSpan_;
    coinHudCamera(projection_,view_);
    // Repeat Wii's single image in both eyes at the adjacent HUD's depth.
    if(!bottomPass_&&execStereo_.active()&&screenParallax_!=0){
        float m[4][4];for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)m[r][c]=projection_.r[r].c[3-c];
        stereo::screenEye(m,1,horizontalSign_,eye_?1.f:-1.f,screenParallax_);
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)projection_.r[r].c[3-c]=m[r][c];
    }
    courseLighting_=false;menuDepthBase_=-1;
    drawNow(model,vertices,transform,false,nullptr,nullptr,nullptr);
    menuDepthBase_=oldDepthBase;menuDepthSpan_=oldDepthSpan;
    projection_=oldProjection;view_=oldView;courseLighting_=oldLighting;
    sceneDepthMap(projection_);
}
void Renderer::drawMenuIconNow(const Model& model,const Vertex* vertices,const C3D_Mtx& transform,const UiQuad& q){
    if(q.alpha<=0||q.w<=0||q.h<=0||q.u1<=q.u0||q.v1<=q.v0)return;
    flushSprites();
    const float width=q.w/(q.u1-q.u0),height=q.h/(q.v1-q.v0);
    const float centerX=q.x+(.5f-q.u0)*width,centerY=q.y+(.5f-q.v0)*height;
    const C3D_Mtx oldProjection=projection_,oldView=view_;
    const bool oldLighting=courseLighting_;
    // Original EGG::Frustum constructor 8022733c: fov45, near10, far100000.
    // RendererMenu uses a 608x480 full frame with eyeZ15, then captures its
    // central192x44 pixels. The icon's crop is not a camera aspect ratio.
    Mtx_PerspTilt(&projection_,C3D_AngleFromDegrees(45.f),608.f/480.f,10.f,100000.f,false);
    Mtx_LookAt(&view_,FVec3_New(0,0,15),FVec3_New(0,0,0),FVec3_New(0,1,0),false);
    const float layer=.9f+.0005f*float(std::min(menuIconDrawCount_++,128u));
    const float oldDepthBase=menuDepthBase_,oldDepthSpan=menuDepthSpan_;
    // Source icon poses sit about 1000 units from the eye. A .0004
    // interval left only a few D24 values across a car, merging front/back.
    // .05 preserves internal separation; actual pose depths remain below the
    // next .0005 image layer, retaining shadow/colour/confirmation order.
    menuDepthBase_=layer;menuDepthSpan_=.05f;
    for(unsigned col=0;col<4;++col){
        projection_.r[0].c[col]*=(480.f/44.f)*(height/240.f);
        projection_.r[1].c[col]*=(608.f/192.f)*(width/screenWidth_);
        // Captured-image panes composite in draw order, including the offset
        // shadow and OK glow of the same vehicle. Give each its own near-depth
        // interval while preserving all relative depth inside this machine.
        // The depth interval is applied by C3D_DepthMap after interpolation.
    }
    // A captured image in its pane: the pane's (screen layer) disparity, not the model's depth.
    if(!bottomPass_&&execStereo_.active()&&screenParallax_!=0){
        float m[4][4];
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)m[r][c]=projection_.r[r].c[3-c];
        stereo::screenEye(m,1,horizontalSign_,eye_?1.f:-1.f,screenParallax_);
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)projection_.r[r].c[3-c]=m[r][c];
    }
    // The pane's crop moves with the eye's image of the pane.
    const float x=q.x+(!bottomPass_&&execStereo_.active()?(eye_?.5f:-.5f)*screenParallax_:0.f);
    const unsigned l=unsigned(std::clamp(std::floor(240.f-q.y-q.h),0.f,240.f));
    const unsigned r=unsigned(std::clamp(std::ceil(240.f-q.y),0.f,240.f));
    const unsigned t=unsigned(std::clamp(std::floor(screenWidth_-x-q.w),0.f,screenWidth_));
    const unsigned b=unsigned(std::clamp(std::ceil(screenWidth_-x),0.f,screenWidth_));
    if(l<r&&t<b){
        C3D_SetScissor(GPU_SCISSOR_NORMAL,l,t,r,b);
        courseLighting_=false;menuIconLayer_=true;menuIconAlpha_=q.alpha;
        menuIconAdditive_=q.additive;menuIconColor0_=q.vehicleColor0;menuIconColor1_=q.vehicleColor1;
        drawAtScreenNow(model,vertices,transform,centerX,centerY,1.f,1.f,1.f);
        menuIconLayer_=false;menuIconAlpha_=1;menuIconAdditive_=false;
        C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
    }
    menuDepthBase_=oldDepthBase;menuDepthSpan_=oldDepthSpan;
    projection_=oldProjection;view_=oldView;courseLighting_=oldLighting;
}
// Every input of drawNow's per-submesh material setup that varies by submesh
// (the rest is fixed for one draw): equal ones set identical GPU state.
static bool sameMaterial(const Submesh& a,const Submesh& b){
    return a.flags==b.flags&&a.lighting==b.lighting&&a.constant==b.constant&&a.add==b.add&&a.ambient==b.ambient&&
           (a.animationFrames>>16)==(b.animationFrames>>16)&&a.texture==b.texture&&a.secondary==b.secondary&&
           a.menuReflection==b.menuReflection&&a.reflectionConstant==b.reflectionConstant&&a.reflectionAdd==b.reflectionAdd&&
           !std::memcmp(a.reflectionUv,b.reflectionUv,sizeof a.reflectionUv);
}
void Renderer::resetMaterialUniforms(){
    if(uvDirty_){
        C3D_FVUnifSet(GPU_VERTEX_SHADER,uvRowLoc_[0],1,0,0,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,uvRowLoc_[1],0,1,0,0);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,uvRowLoc_[2],1,0,0,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,uvRowLoc_[3],0,1,0,0);
        uvDirty_=false;
    }
    if(tintDirty_){C3D_FVUnifSet(GPU_VERTEX_SHADER,tintLoc_,1,1,1,1);tintDirty_=false;}
}
void Renderer::drawNow(const Model& model,const Vertex* vertices,const C3D_Mtx& transform,bool backdrop,MaterialBinder material,void* context,ShapeVisible visible){
    C3D_Mtx mv;Mtx_Multiply(&mv,&view_,&transform);
    drawSampledLighting_=model.dynamicLightTextures;
    ClipFrustum staticFrustum;
    // Reject immutable world models before uploading matrices or lighting.
    // Player meshes never opt into static visibility.
    if(!backdrop&&!model.visibility.empty()){
        C3D_Mtx clip;Mtx_Multiply(&clip,&projection_,&mv);float rows[16];
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=clip.r[r].c[3-c];
        staticFrustum=ClipFrustum(rows);
        if(!staticFrustum.intersects({model.minimum,model.maximum})){stats_.culledTriangles+=model.indexCount/3;return;}
    }
    flushSprites();
    if(model.nativeMiiLighting)C3D_BindProgram(&miiProgram_);
    const uint32_t animationFrame=execFrame_,additiveColor=execAdditive_&0xffffff00u;
    const unsigned inkBlack=execAdditive_&InkBlack;
    uploadProjection(projectionLoc_,projection_);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&mv);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,staticLightingLoc_,model.staticLightingBaked);
    if(!model.fixedLighting&&!model.staticLightingBaked)normalMatrixNow(mv);
    auto* attr=C3D_GetAttrInfo();AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_FLOAT,3);AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);AttrInfo_AddLoader(attr,3,GPU_UNSIGNED_BYTE,4);
    auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,vertices,sizeof(Vertex),4,0x3210);
    if(model.staticEnvironment2){
        // Buffer permutations address loader slots, not shader registers.
        // This is loader slot 4 mapped to v5 (instaticuv); retain baked UVs.
        const int slot=AttrInfo_AddLoader(attr,5,GPU_FLOAT,2);
        BufInfo_Add(buf,model.staticEnvironment2,2*sizeof(float),1,unsigned(slot));
    }
    const bool litSkin=skinPalette_&&model.skinBoneIndices&&!model.fixedLighting&&!model.staticLightingBaked;
    if(litSkin){AttrInfo_AddLoader(attr,4,GPU_FLOAT,1);BufInfo_Add(buf,model.skinBoneIndices,sizeof(float),1,4);}
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedNormalsLoc_,litSkin);
    if(skinPalette_){
        for(unsigned b=0;b<skinBones_;++b){const float* m=skinPalette_+b*skinStride_;
            for(unsigned r=0;r<3;++r)C3D_FVUnifSet(GPU_VERTEX_SHADER,bonesLoc_+int(b*3+r),m[r*4],m[r*4+1],m[r*4+2],m[r*4+3]);}
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedLoc_,true);
    }
    const int region=execSceneRegion_==1?0:execSceneRegion_==2?1:-1;
    const bool regionCull=region>=0&&model.regionIndices[region]&&model.regionVisibility[region].size()==model.submeshes.size();
    const auto* drawIndices=regionCull?model.regionIndices[region]:model.indices;
    // One set of object-space planes per static model, shared by all ranges.
    const bool cull=!backdrop&&!model.visibility.empty()&&model.visibility.size()==model.submeshes.size();
    // Rigid parts (one bone each: a stand of Mii spectators spread over the
    // whole course is one model) are tested one by one against the same planes.
    const bool partCull=!backdrop&&rigid_&&rigid_->parts;
    const bool skinCull=!backdrop&&skinPalette_&&model.skinVisibility.size()==model.submeshes.size();
    ClipFrustum frustum;
    // Reduced quality: course-object parts under minScreenRatio from the eye
    // (model space). Menu models keep every part.
    float partTiny=0;Vec3 eye{};
    if(cull||partCull||skinCull){
        if(!backdrop&&!model.visibility.empty())frustum=staticFrustum;
        else{
            C3D_Mtx clip;Mtx_Multiply(&clip,&projection_,&mv);float rows[16];
            for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=clip.r[r].c[3-c];
            frustum=ClipFrustum(rows);
        }
        if(partCull&&courseLighting_&&(partTiny=renderQuality().minScreenRatio)>0){
            C3D_Mtx inverse=mv;
            if(Mtx_Inverse(&inverse)!=0.f)eye={inverse.r[0].w,inverse.r[1].w,inverse.r[2].w};else partTiny=0;
        }
    }
    // The last drawn submesh whose material state is still bound, and its end
    // of submesh (retailEndSubmesh), deferred while the next one repeats it.
    const Submesh* last=nullptr;bool lastRetail=false,lastEnvironment=false,pendingEnd=false;
    C3D_Tex projectedWaterTexture{};
    for(std::size_t i=0;i<model.submeshes.size();++i){
        if(visible && !visible(context,i))continue;
        const auto& s=model.submeshes[i];
        if(!s.count)continue;
        const auto* parts=rigid_&&rigid_->parts&&i<rigid_->parts->size()?&(*rigid_->parts)[i]:nullptr;
        // A bit per part (the first 64) whose posed bounds reach the view; a
        // submesh with none skips its material setup too.
        uint64_t shownParts=~0ull;
        if(parts&&partCull){
            shownParts=0;
            for(size_t n=0;n<parts->size()&&n<64;++n){
                const auto& part=(*parts)[n];
                const Bounds b=transformBoundsPadded(part.bounds,rigid_->palette+part.matrix*rigid_->stride);
                if(frustum.intersects(b)&&!(partTiny>0&&tinyFrom(b,eye,partTiny)))shownParts|=1ull<<n;
                else stats_.culledTriangles+=part.count/3;
            }
            if(!shownParts&&parts->size()<=64){++stats_.culledSubmeshes;continue;}
        }
        VisibleRanges ranges;
        if(skinCull){
            ranges=selectSkinVisible(model.skinVisibility[i],skinPalette_,skinStride_,frustum);unsigned submitted=0;
            for(unsigned n=0;n<ranges.count;++n)submitted+=ranges.ranges[n].count;
            stats_.culledTriangles+=(s.count-submitted)/3;
            if(!ranges.count){++stats_.culledSubmeshes;continue;}
        }else if(cull){
            ranges=selectVisible(regionCull?model.regionVisibility[region][i]:model.visibility[i],frustum);unsigned submitted=0;
            for(unsigned n=0;n<ranges.count;++n)submitted+=ranges.ranges[n].count;
            stats_.culledTriangles+=(s.count-submitted)/3;
            if(!ranges.count){++stats_.culledSubmeshes;continue;}
        }else ranges.append({s.first,s.count});
        const SubmeshOverride* o=overrides_&&i<overrideCount_&&overrides_[i].flags?&overrides_[i]:nullptr;
        // Reset inherited state before applying this submesh's overrides. The
        // reset clears both uniforms: doing it after UV setup when tint ends
        // also erased the new SRT/mirror matrix (including a Boo at full alpha).
        const bool overrideUv=o&&(o->flags&SubmeshOverride::Uv);
        const auto* track=i<model.uvTracks.size()&&model.uvTracks[i].frames?&model.uvTracks[i]:nullptr;
        const float* uv=overrideUv?o->uv:track?track->samples.data()+12*(track->loop?animationFrame%track->frames:std::min(animationFrame,track->frames-1)):nullptr;
        const bool hasUv=uv!=nullptr;
        const bool hasTint=o&&(o->flags&SubmeshOverride::Tint);
        if((uvDirty_&&!hasUv)||(tintDirty_&&!hasTint))resetMaterialUniforms();
        if(hasUv){
            // An SRT0 track replaces the authored constant-rate scroll.
            for(unsigned row=0;row<4;++row)C3D_FVUnifSet(GPU_VERTEX_SHADER,uvRowLoc_[row],uv[row*3],uv[row*3+1],uv[row*3+2],0);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);uvDirty_=true;
        }else{
            const uint32_t frames=s.animationFrames&0xffff;
            const float frame=frames?float(animationFrame%frames):0.f;
            C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,s.uvVelocity[0]*frame,s.uvVelocity[1]*frame,s.uvVelocity[2]*frame,s.uvVelocity[3]*frame);
        }
        if(hasTint){C3D_FVUnifSet(GPU_VERTEX_SHADER,tintLoc_,o->tint[0],o->tint[1],o->tint[2],o->tint[3]);tintDirty_=true;}
        // The previous drawn submesh's material and textures, without overrides
        // or a binder: its whole state is still bound, only UVs and draws differ.
        const bool sameState=last&&!o&&!material&&sameMaterial(*last,s);
        const auto lastSampler=sameState&&!model.samplers.empty()?std::size_t(last-model.submeshes.data()):0;
        const bool roadLodOnly=sameState&&model.roadSamplerPolicy&&s.texture>=0&&s.menuReflection==0&&
            !(s.lighting&retail::PrimaryEnvironment)&&s.texture!=model.stadiumTexture&&
            roadSampling::primaryLodOnly(model.samplers[lastSampler],model.samplers[i]);
        const bool repeat=sameState&&(roadLodOnly||model.samplers.empty()||
            !std::memcmp(model.samplers[i].data(),model.samplers[lastSampler].data(),sizeof(model.samplers[i])));
        if(roadLodOnly){
            auto* tex=model.textures[s.texture]->sampledGpu(model.samplers[i].data());
            const GPU_TEXTURE_WRAP_PARAM wrap[]={GPU_CLAMP_TO_EDGE,GPU_REPEAT,GPU_MIRRORED_REPEAT,GPU_CLAMP_TO_EDGE};
            C3D_TexSetWrap(tex,wrap[(s.flags>>8)&3],wrap[(s.flags>>10)&3]);C3D_TexBind(0,tex);
            last=&s;
        }
        bool retail=lastRetail,retailEnvironment=lastEnvironment;
        if(!repeat){
        if(pendingEnd){retailEndSubmesh();pendingEnd=false;}
        const uint32_t constant=o&&(o->flags&SubmeshOverride::Constant)?o->constant:s.constant;
        const GPU_TEVSCALE scales[]={GPU_TEVSCALE_1,GPU_TEVSCALE_2,GPU_TEVSCALE_4,GPU_TEVSCALE_1};
        if(!material){
            C3D_TexEnvBufUpdate(C3D_Both,0);
            for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
            auto* e0=C3D_GetTexEnv(0);auto* e1=C3D_GetTexEnv(1);
            C3D_TexEnvSrc(e0,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(e0,C3D_Both,GPU_MODULATE);
            C3D_TexEnvScale(e0,C3D_RGB,scales[(s.flags>>4)&3]);
            const bool waterGrade=(s.flags&(1u<<29))&&!(s.flags&1);
            if(waterGrade){
                // The source water map is a black/white indirect wave mask,
                // not albedo. Use a clean aqua base until indirect-texture
                // distortion is represented; sampling the mask as colour made
                // the sea black and checkerboarded.
                C3D_TexEnvColor(e0,0xff885a40);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
            }
            const unsigned mode=(s.flags>>12)&3;
            const bool bit25=s.flags&(1u<<25);
            const bool environmentMetal=bit25&&mode==3;
            // Course-object materials: lerp(C, texture, raster), optionally
            // followed by + texture*raster (mode 1).
            const bool constantLerp=bit25&&(mode==1||mode==2);
            if(bit25&&mode==0){
                // Original single-texture two-colour interpolation (item ?).
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_TEXTURE0);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
            }
            if(waterGrade){
                // Water uses one authored base texture plus its source alpha.
            }else if(constantLerp){
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
                if(mode==1){
                    C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PREVIOUS);
                    C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);
                }
            }else if(mode==1){
                // GX: lerp(C0, raster, secondary) * base, with authored scales.
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_TEXTURE1);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
                C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE1,GPU_PRIMARY_COLOR);
                C3D_TexEnvScale(e0,C3D_RGB,scales[(s.flags>>14)&3]);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE0);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MODULATE);
                C3D_TexEnvScale(e1,C3D_RGB,scales[(s.flags>>4)&3]);
            }else if(mode==2){
                C3D_TexEnvColor(e1,constant);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT,GPU_PREVIOUS);
                C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);
            }else if(environmentMetal){
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE1);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_ADD_SIGNED);
                auto* raster=C3D_GetTexEnv(2);
                C3D_TexEnvSrc(raster,C3D_RGB,GPU_PREVIOUS,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(raster,C3D_RGB,GPU_MODULATE);
            }else if(mode==3){
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT);
                C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE1,GPU_PRIMARY_COLOR);
                C3D_TexEnvSrc(e1,C3D_Both,GPU_TEXTURE0,GPU_PREVIOUS);
                C3D_TexEnvFunc(e1,C3D_Both,GPU_ADD);
            }
            if(!(s.flags&1)&&((s.animationFrames>>16)&255)==1){
                // Authored half-pipe border: T0.rgb + T1.rgb*T0.alpha.
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0);C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE1,GPU_TEXTURE0,GPU_PREVIOUS);
                C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);
            }
            if(!(s.flags&1)&&((s.animationFrames>>16)&255)==2){
                // GX sky: P + cloud*(1-P), not an additive cloud layer.
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE1,GPU_PREVIOUS,GPU_PREVIOUS);
                C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);
            }
            if(!(s.flags&1)&&((s.animationFrames>>16)&255)==3){
                // GX single-stage raster + texture, including Daisy's sky.
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_ADD);
            }
            if(!(s.flags&1)&&((s.animationFrames>>16)&255)==4){
                // Funky overhead LED panels: P=logo*K+C2.a, RGB=2*(P+P*dots).
                // Keep both original UV/SRT layers; these are not EFB screens.
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_CONSTANT);
                C3D_TexEnvOpRgb(e0,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MULTIPLY_ADD);C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE1,GPU_PREVIOUS);
                C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);C3D_TexEnvScale(e1,C3D_RGB,GPU_TEVSCALE_2);
            }
            if(s.menuReflection==28){
                // Stopwatch: clamp(C2.a*raster + (metal or raster)), then
                // add metal.rgb * metal.a * K2.a. Keep the first clamp.
                C3D_TexEnvBufUpdate(C3D_RGB,1);
                C3D_TexEnvColor(e0,s.reflectionConstant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_PRIMARY_COLOR,GPU_CONSTANT,s.reflectionAdd&1?GPU_PRIMARY_COLOR:GPU_TEXTURE1);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MULTIPLY_ADD);
                C3D_TexEnvScale(e0,C3D_RGB,s.reflectionAdd&2?GPU_TEVSCALE_2:GPU_TEVSCALE_1);
                C3D_TexEnvColor(e1,s.reflectionConstant);
                C3D_TexEnvSrc(e1,C3D_Alpha,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e1,C3D_Alpha,GPU_MODULATE);
                auto* sum=C3D_GetTexEnv(2);
                C3D_TexEnvSrc(sum,C3D_RGB,GPU_TEXTURE1,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER);
                C3D_TexEnvOpRgb(sum,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR);
                C3D_TexEnvFunc(sum,C3D_RGB,GPU_MULTIPLY_ADD);
            }else if(s.menuReflection==29){
                // Balloon: clamp(C0*raster + metal*C1)*raster + C2.
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
                C3D_TexEnvColor(e1,s.reflectionConstant);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT,GPU_PREVIOUS);C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);
                auto* lit=C3D_GetTexEnv(2);C3D_TexEnvColor(lit,s.reflectionAdd);
                C3D_TexEnvSrc(lit,C3D_RGB,GPU_PREVIOUS,GPU_PRIMARY_COLOR,GPU_CONSTANT);C3D_TexEnvFunc(lit,C3D_RGB,GPU_MULTIPLY_ADD);
            }else if(s.menuReflection==30){
                // Balloon: clamp(base*K + metal*(1-K)), then
                // clamp(2*(previous*raster + C2 - .5)).
                C3D_TexEnvBufUpdate(C3D_RGB,1);
                C3D_TexEnvColor(e0,constant);C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
                C3D_TexEnvColor(e1,s.reflectionConstant);C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e1,C3D_RGB,GPU_MODULATE);
                auto* sum=C3D_GetTexEnv(2);C3D_TexEnvSrc(sum,C3D_RGB,GPU_PREVIOUS_BUFFER,GPU_PREVIOUS);C3D_TexEnvFunc(sum,C3D_RGB,GPU_ADD);
                auto* lit=C3D_GetTexEnv(3);C3D_TexEnvSrc(lit,C3D_RGB,GPU_PREVIOUS,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(lit,C3D_RGB,GPU_MODULATE);
                auto* lift=C3D_GetTexEnv(4);C3D_TexEnvColor(lift,s.reflectionAdd);
                C3D_TexEnvSrc(lift,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(lift,C3D_RGB,GPU_ADD_SIGNED);C3D_TexEnvScale(lift,C3D_RGB,GPU_TEVSCALE_2);
            }else if(s.menuReflection==31){
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_PRIMARY_COLOR,GPU_TEXTURE1);
                C3D_TexEnvOpRgb(e0,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
            }else if(s.menuReflection==27){
                // Award Tico's original signed yellow/metal environment layer.
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                C3D_TexEnvBufUpdate(C3D_RGB,1);
                C3D_TexEnvColor(e1,s.reflectionConstant);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_CONSTANT,GPU_CONSTANT,GPU_TEXTURE1);
                C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_INTERPOLATE);
                auto* sum=C3D_GetTexEnv(2);
                C3D_TexEnvSrc(sum,C3D_RGB,GPU_PREVIOUS_BUFFER,GPU_PREVIOUS);
                C3D_TexEnvFunc(sum,C3D_RGB,GPU_ADD_SIGNED);C3D_TexEnvScale(sum,C3D_RGB,GPU_TEVSCALE_2);
                C3D_TexEnvColor(sum,0xffffffff);C3D_TexEnvSrc(sum,C3D_Alpha,GPU_CONSTANT);C3D_TexEnvFunc(sum,C3D_Alpha,GPU_REPLACE);
            }else if(s.menuReflection==26){
                // thunder_m's positive environment ramp, then lit albedo.
                C3D_TexEnvColor(e0,s.reflectionConstant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
                C3D_TexEnvColor(e1,s.reflectionAdd);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE1);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_INTERPOLATE);
                auto* sum=C3D_GetTexEnv(2);
                C3D_TexEnvSrc(sum,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PREVIOUS);
                C3D_TexEnvFunc(sum,C3D_RGB,GPU_MULTIPLY_ADD);
            }else if(s.menuReflection==24){
                C3D_TexEnvColor(e0,s.reflectionConstant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_TEXTURE0);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE0);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_ADD);
            }else if(s.menuReflection==25){
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
            }else if(s.menuReflection==20){
                // Retail treehouse: clamp(2*T0*raster), then + T1*K1 RGB.
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_2);
                C3D_TexEnvColor(e1,constant);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT,GPU_PREVIOUS);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);
            }else if(s.menuReflection==19){
                // Earth: clamp(2 * lerp(clamp(surface*raster),raster,cloud)).
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
                C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_PRIMARY_COLOR,GPU_PREVIOUS,GPU_TEXTURE1);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_INTERPOLATE);
                C3D_TexEnvScale(e1,C3D_RGB,GPU_TEVSCALE_2);
            }else if(s.menuReflection==18){
                // Fire Snake's two independently sampled ramps and raster.
                // The cooker stores C0-C1, raster+C1, and the final C1.
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MULTIPLY_ADD);C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                C3D_TexEnvColor(e1,constant);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT,GPU_PREVIOUS);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);
                auto* cold=C3D_GetTexEnv(2);C3D_TexEnvColor(cold,s.reflectionAdd);
                C3D_TexEnvSrc(cold,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(cold,C3D_RGB,GPU_ADD);
                auto* opacity=C3D_GetTexEnv(3);
                C3D_TexEnvSrc(opacity,C3D_Alpha,GPU_TEXTURE0,GPU_TEXTURE1);C3D_TexEnvFunc(opacity,C3D_Alpha,GPU_ADD);
            }else if(s.menuReflection==17){
                // StarRing: clamp(2*(environment*K0 + surface*lit raster)).
                C3D_TexEnvColor(e0,s.reflectionConstant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PREVIOUS);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);C3D_TexEnvScale(e1,C3D_RGB,GPU_TEVSCALE_2);
            }else if(s.menuReflection==16){
                // StarPiece: 2*(clamp(4*T0*raster) + raster*konst).
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_4);
                C3D_TexEnvColor(e1,s.reflectionConstant);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_PREVIOUS);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);C3D_TexEnvScale(e1,C3D_RGB,GPU_TEVSCALE_2);
            }else if(s.menuReflection==15){
                // GX toll display: clamp(2*(T0.rgb + T1.alpha)).
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
                C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_TEXTURE1);
                C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_ADD);
                C3D_TexEnvScale(e1,C3D_RGB,GPU_TEVSCALE_2);
            }else if(s.menuReflection==14){
                // Original pillar/splash: lerp(Cn,endpoint,T0)+C2*T1.
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_TEXTURE0);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
                C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                C3D_TexEnvColor(e1,o&&(o->flags&SubmeshOverride::Add)?o->add:s.reflectionAdd);
                const bool rasterAlpha=s.reflectionConstant&1;
                C3D_TexEnvSrc(e1,C3D_RGB,rasterAlpha?GPU_PRIMARY_COLOR:GPU_TEXTURE1,GPU_CONSTANT,GPU_PREVIOUS);
                if(rasterAlpha)C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);
            }else if(s.menuReflection==13){
                // FlamePole: preserve saturation of 2*T0*raster before
                // blending the gradient texture toward white.
                C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_2);
                C3D_TexEnvColor(e1,0xffffffffu);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_CONSTANT,GPU_TEXTURE1,GPU_PREVIOUS);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_INTERPOLATE);
            }else if(s.menuReflection==12){
                // Authored lava ramp, with the waterfall's signed bias before
                // scaling/clamping, followed by the alpha-weighted mesh layer.
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_TEXTURE0);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
                C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                const bool waterfall=s.reflectionConstant&1;
                if(waterfall){
                    C3D_TexEnvColor(e1,0);
                    C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);
                    C3D_TexEnvFunc(e1,C3D_RGB,GPU_ADD_SIGNED);
                    C3D_TexEnvScale(e1,C3D_RGB,GPU_TEVSCALE_2);
                }
                auto* layer=C3D_GetTexEnv(waterfall?2:1);
                C3D_TexEnvSrc(layer,C3D_RGB,GPU_TEXTURE1,GPU_TEXTURE0,GPU_PREVIOUS);
                C3D_TexEnvOpRgb(layer,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR);
                C3D_TexEnvFunc(layer,C3D_RGB,GPU_MULTIPLY_ADD);
            }else if(s.menuReflection==11){
                // GX water: P=C0+(1-C0)*glow, then lerp(C0,B,wave)+P.
                // B is P (factory), C2 (DS garden), or ONE (N64 jungle).
                // Save P in the delayed buffer so the last addition does not
                // replace it with the second stage's interpolation result.
                const bool halfGlow=(s.reflectionAdd&2)!=0;
                C3D_TexEnvBufUpdate(C3D_RGB,halfGlow?2:1);
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT,GPU_TEXTURE0,GPU_CONSTANT);
                C3D_TexEnvOpRgb(e0,GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MULTIPLY_ADD);
                C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                if(s.reflectionAdd&4){
                    // Sherbet uses HALF, rather than ONE, as the glow endpoint.
                    C3D_TexEnvColor(e0,(constant&0xffffffu)|0x80000000u);
                    C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT,GPU_CONSTANT,GPU_TEXTURE0);
                    C3D_TexEnvOpRgb(e0,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
                    C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
                }
                if(halfGlow){
                    // N64 jungle's first GX stage has DIVIDE_2 scale.
                    C3D_TexEnvColor(e1,0x80808080u);
                    C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);
                    C3D_TexEnvFunc(e1,C3D_RGB,GPU_MODULATE);
                }
                auto* wave=C3D_GetTexEnv(halfGlow?2:1);
                C3D_TexEnvColor(wave,constant);
                C3D_TexEnvSrc(wave,C3D_RGB,(s.reflectionAdd&1)?GPU_PRIMARY_COLOR:GPU_PREVIOUS,GPU_CONSTANT,GPU_TEXTURE1);
                C3D_TexEnvFunc(wave,C3D_RGB,GPU_INTERPOLATE);
                auto* sum=C3D_GetTexEnv(halfGlow?3:2);
                C3D_TexEnvSrc(sum,C3D_RGB,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER);
                C3D_TexEnvFunc(sum,C3D_RGB,GPU_ADD);
            }else if(s.menuReflection==10){
                // Keep albedo in the delayed TEV buffer while weighting the
                // environment. Adding before subtracting the bias must not
                // clamp the unbiassed sum (which may be greater than one).
                C3D_TexEnvBufUpdate(C3D_RGB,1);
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);
                C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                const uint32_t weight=s.reflectionConstant&255;
                C3D_TexEnvColor(e1,weight*0x010101u);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MODULATE);
                auto* sum=C3D_GetTexEnv(2);
                C3D_TexEnvSrc(sum,C3D_RGB,GPU_PREVIOUS_BUFFER,GPU_PREVIOUS);
                C3D_TexEnvFunc(sum,C3D_RGB,s.reflectionConstant&256?GPU_ADD_SIGNED:GPU_ADD);
                auto* lit=C3D_GetTexEnv(3);
                C3D_TexEnvSrc(lit,C3D_RGB,GPU_PREVIOUS,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(lit,C3D_RGB,GPU_MODULATE);
                auto* lift=C3D_GetTexEnv(4);
                C3D_TexEnvColor(lift,s.reflectionAdd);
                C3D_TexEnvSrc(lift,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);
                C3D_TexEnvFunc(lift,C3D_RGB,GPU_ADD);
                C3D_TexEnvScale(lift,C3D_RGB,scales[(s.flags>>4)&3]);
            }else if(s.menuReflection==22){
                // Original union_D00a: atlas albedo (unit1) plus a camera-
                // projected highlight (unit0). Clamp the doubled first stage.
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE1,GPU_CONSTANT);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_2);
                C3D_TexEnvColor(e1,s.reflectionAdd);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_PREVIOUS);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);
            }else if(s.menuReflection==23){
                // quickSand_m: APREV=T0.a*rasA, then
                // clamp(2*rasC*lerp(T0,T1,APREV)). Both UVs retain SRT0.
                C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(e0,C3D_Alpha,GPU_MODULATE);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE1,GPU_TEXTURE0,GPU_PREVIOUS);
                C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_INTERPOLATE);
                auto* finish=C3D_GetTexEnv(2);
                C3D_TexEnvSrc(finish,C3D_RGB,GPU_PREVIOUS,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(finish,C3D_RGB,GPU_MODULATE);
                C3D_TexEnvScale(finish,C3D_RGB,GPU_TEVSCALE_2);
            }else if(s.menuReflection==21){
                // Sun halo: clamp(scale*(lerp(C1,C0,T)-bias)).
                // In particular ef_sunFlare subtracts 1/2 BEFORE doubling;
                // the grayscale albedo fallback discarded this authored colour ramp.
                C3D_TexEnvColor(e0,s.reflectionConstant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
                C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                C3D_TexEnvColor(e1,s.reflectionAdd);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE0);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_INTERPOLATE);
                auto* finish=C3D_GetTexEnv(2);C3D_TexEnvColor(finish,0);
                C3D_TexEnvSrc(finish,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);
                C3D_TexEnvFunc(finish,C3D_RGB,s.reflectionUv[0]>0?GPU_ADD_SIGNED:GPU_ADD);
                C3D_TexEnvScale(finish,C3D_RGB,scales[(s.flags>>4)&3]);
            }else if(s.menuReflection==7){
                // GX pylon ramp: C0*(1-T0)+C1*T0, then lit raster colour.
                // CLR0 C1 is an absolute Add override, not a vertex tint.
                C3D_TexEnvColor(e0,constant);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_RGB,GPU_REPLACE);
                C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                C3D_TexEnvColor(e1,o&&(o->flags&SubmeshOverride::Add)?o->add:s.reflectionAdd);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE0);
                C3D_TexEnvFunc(e1,C3D_RGB,GPU_INTERPOLATE);
                C3D_TexEnvScale(e1,C3D_RGB,scales[(s.flags>>14)&3]);
                auto* lit=C3D_GetTexEnv(2);
                C3D_TexEnvSrc(lit,C3D_RGB,GPU_PREVIOUS,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(lit,C3D_RGB,GPU_MODULATE);
                C3D_TexEnvScale(lit,C3D_RGB,scales[(s.flags>>4)&3]);
            }else if(s.menuReflection==8){
                // GX statue: P0=T0*ras/2; P1=lerp(P0,T1,C0)/2;
                // result=2*(P1+C1*T2). PICA's half is an 8-bit constant.
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MODULATE);C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                C3D_TexEnvColor(e1,0x80808080u);
                C3D_TexEnvSrc(e1,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e1,C3D_RGB,GPU_MODULATE);
                auto* marble=C3D_GetTexEnv(2);C3D_TexEnvColor(marble,constant);
                C3D_TexEnvSrc(marble,C3D_RGB,GPU_TEXTURE1,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(marble,C3D_RGB,GPU_INTERPOLATE);
                auto* half=C3D_GetTexEnv(3);C3D_TexEnvColor(half,0x80808080u);
                C3D_TexEnvSrc(half,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(half,C3D_RGB,GPU_MODULATE);
                auto* reflection=C3D_GetTexEnv(4);C3D_TexEnvColor(reflection,o&&(o->flags&SubmeshOverride::Add)?o->add:s.reflectionAdd);
                C3D_TexEnvSrc(reflection,C3D_RGB,GPU_TEXTURE2,GPU_CONSTANT,GPU_PREVIOUS);C3D_TexEnvFunc(reflection,C3D_RGB,GPU_MULTIPLY_ADD);
                C3D_TexEnvScale(reflection,C3D_RGB,GPU_TEVSCALE_2);
            }else if(s.menuReflection){
                // GX: clamp(albedo*rasC + C1), then + reflection*APREV
                // (APREV = albedo alpha * authored konst) or * konst RGB.
                // Keep the intermediate alpha until stage 3 applies the
                // source's final alpha; it is a reflection mask, not opacity.
                C3D_TexEnvColor(e0,s.reflectionAdd);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_CONSTANT);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MULTIPLY_ADD);
                if(s.menuReflection==9)C3D_TexEnvScale(e0,C3D_RGB,GPU_TEVSCALE_1);
                C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE0,GPU_CONSTANT);
                C3D_TexEnvFunc(e0,C3D_Alpha,GPU_MODULATE);
                if(s.menuReflection==4){
                    // Banana: lerp(C0, albedo, raster), then its masked highlight.
                    C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_PRIMARY_COLOR);
                    C3D_TexEnvFunc(e0,C3D_RGB,GPU_INTERPOLATE);
                }else if(s.menuReflection==6){
                    // Shell: APREV = albedo alpha * lit raster alpha.
                    C3D_TexEnvSrc(e0,C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR);
                }
                if(s.menuReflection!=3){
                    const bool mask=s.menuReflection==1||s.menuReflection==4||s.menuReflection==6;
                    C3D_TexEnvColor(e1,s.reflectionConstant);
                    C3D_TexEnvSrc(e1,C3D_RGB,GPU_TEXTURE1,mask?GPU_PREVIOUS:GPU_CONSTANT,GPU_PREVIOUS);
                    C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,mask?GPU_TEVOP_RGB_SRC_ALPHA:GPU_TEVOP_RGB_SRC_COLOR);
                    C3D_TexEnvFunc(e1,C3D_RGB,GPU_MULTIPLY_ADD);
                    if(s.menuReflection==9)C3D_TexEnvScale(e1,C3D_RGB,scales[(s.flags>>4)&3]);
                }
            }
            // Alpha is a separate GX equation: lighting's secondary texture
            // must not replace the road decal's actual alpha mask.
            const unsigned alphaMode=(s.flags>>20)&7;
            auto* alpha=C3D_GetTexEnv(3);
            if(s.menuReflection==25){
                auto* intermediate=C3D_GetTexEnv(2);C3D_TexEnvColor(intermediate,constant);
                C3D_TexEnvSrc(intermediate,C3D_Alpha,GPU_TEXTURE1,GPU_PRIMARY_COLOR,GPU_CONSTANT);
                C3D_TexEnvFunc(intermediate,C3D_Alpha,GPU_MULTIPLY_ADD);
                C3D_TexEnvSrc(alpha,C3D_Alpha,GPU_TEXTURE0,GPU_PREVIOUS);
                C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_MODULATE);
            }else if(alphaMode==1||alphaMode==2){
                C3D_TexEnvSrc(alpha,C3D_Alpha,alphaMode==1&&s.menuReflection!=22?GPU_TEXTURE0:GPU_TEXTURE1,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_MODULATE);
            }else if(alphaMode==3||alphaMode==6){
                // Mode 6 doubles the texture product (GX alpha scale 2).
                C3D_TexEnvSrc(alpha,C3D_Alpha,GPU_TEXTURE0,GPU_TEXTURE1);C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_MODULATE);
                if(alphaMode==6)C3D_TexEnvScale(alpha,C3D_Alpha,GPU_TEVSCALE_2);
                if(!(s.menuReflection==14&&(s.reflectionConstant&1))){
                    auto* vertexAlpha=C3D_GetTexEnv(4);C3D_TexEnvSrc(vertexAlpha,C3D_Alpha,GPU_PREVIOUS,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(vertexAlpha,C3D_Alpha,GPU_MODULATE);
                }
            }else if(alphaMode==7){
                C3D_TexEnvColor(alpha,constant);
                C3D_TexEnvSrc(alpha,C3D_Alpha,s.menuReflection==11?GPU_TEXTURE0:GPU_TEXTURE1,GPU_PRIMARY_COLOR,GPU_CONSTANT);
                C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_MULTIPLY_ADD);
            }else if(alphaMode==4&&o&&(o->flags&SubmeshOverride::Alpha)){
                // A CLR0-animated register is the whole alpha (the cooked vertex alpha is its static 0).
                C3D_TexEnvColor(alpha,(s.menuReflection==8?0x00808080u:0u)|(uint32_t(std::lround(std::clamp(o->tint[3],0.f,1.f)*255.f))<<24));
                C3D_TexEnvSrc(alpha,C3D_Alpha,GPU_CONSTANT);C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_REPLACE);
            }else if(alphaMode==4){
                const bool menu=s.lighting&mkw::retail::Menu;
                if(menu)C3D_TexEnvColor(alpha,constant);
                C3D_TexEnvSrc(alpha,C3D_Alpha,menu?GPU_CONSTANT:GPU_PRIMARY_COLOR);C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_REPLACE);
            }else if(alphaMode==5){
                C3D_TexEnvColor(alpha,s.menuReflection==8?(constant&0xff000000u)|0x00808080u:constant);
                C3D_TexEnvSrc(alpha,C3D_Alpha,GPU_TEXTURE0,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_INTERPOLATE);
                if(s.menuReflection==10||s.menuReflection==30){
                    auto* vertexAlpha=C3D_GetTexEnv(4);
                    C3D_TexEnvSrc(vertexAlpha,C3D_Alpha,GPU_PREVIOUS,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(vertexAlpha,C3D_Alpha,GPU_MODULATE);
                }
            }
        }
        if(additiveColor||((s.menuReflection!=7&&s.menuReflection!=8&&s.menuReflection!=14)&&o&&(o->flags&SubmeshOverride::Add))){
            // The shared retail Star CLR0 animates GX C1, added to the textured
            // driver/vehicle colour. Keep the source alpha equation untouched.
            // Course-object CLR0 glows use the same stage (already 0xAABBGGRR).
            const uint32_t c=additiveColor;
            auto* star=C3D_GetTexEnv(5);C3D_TexEnvInit(star);
            C3D_TexEnvColor(star,c?(c>>24)|((c>>8)&0xff00)|((c<<8)&0xff0000):o->add&0xffffff);
            C3D_TexEnvSrc(star,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(star,C3D_RGB,GPU_ADD);
        }
        if(menuIconLayer_){
            // ButtonMachine's native capture remap has nonnegative C1-C0;
            // keep the material alpha equation, then apply the pane opacity.
            auto channel=[&](unsigned shift){return uint32_t(std::max(0,int((menuIconColor1_>>shift)&255)-int((menuIconColor0_>>shift)&255)));};
            const uint32_t difference=channel(24)|(channel(16)<<8)|(channel(8)<<16)|0xff000000u;
            auto* scale=C3D_GetTexEnv(4);
            C3D_TexEnvColor(scale,difference);
            C3D_TexEnvSrc(scale,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(scale,C3D_RGB,GPU_MODULATE);
            const uint32_t base=(menuIconColor0_>>24)|((menuIconColor0_>>8)&0xff00)|((menuIconColor0_<<8)&0xff0000);
            auto* remap=C3D_GetTexEnv(5);C3D_TexEnvInit(remap);
            C3D_TexEnvColor(remap,base|(uint32_t(std::round(std::clamp(menuIconAlpha_,0.f,1.f)*255.f))<<24));
            C3D_TexEnvSrc(remap,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(remap,C3D_RGB,GPU_ADD);
            C3D_TexEnvSrc(remap,C3D_Alpha,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(remap,C3D_Alpha,GPU_MODULATE);
        }
        retail=(courseLighting_||(s.lighting&mkw::retail::Menu))&&retailLight(s);
        if(!retail){
            // Legacy grade: vertex colour * max(ambient floor, 0.35) * grade.
            const float ambient=std::max((s.flags&8)||backdrop?1.f:(courseLighting_?0.86f:0.f),0.35f)*modelBrightness_;
            C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,ambient*(courseLighting_?1.50f:1.f),ambient*(courseLighting_?1.40f:1.f),
                          ambient*(courseLighting_?1.25f:1.f),1.f);
        }
        // Imported GX triangles wind clockwise relative to their outward normals.
        C3D_CullFace(cameraCull((s.flags&4)||backdrop?GPU_CULL_NONE:GPU_CULL_FRONT_CCW,mv));
        const bool sourceState=s.flags&(1u<<31),blended=s.flags&2;
        const GPU_TESTFUNC alphaComparisons[]={GPU_NEVER,GPU_LESS,GPU_EQUAL,GPU_LEQUAL,GPU_GREATER,GPU_NOTEQUAL,GPU_GEQUAL,GPU_ALWAYS};
        const uint8_t alphaReferences[]={0,128,165,255};
        // animationFrames bits 16..31 (tools/gx_material.py): an exact alpha
        // reference and PICA blend factors (+1) the flags cannot express.
        const uint32_t extended=sourceState?s.animationFrames>>16:0;
        C3D_AlphaTest((s.flags&1)!=0,sourceState?alphaComparisons[(s.flags>>26)&7]:GPU_GREATER,
                     sourceState?((extended&255)?uint8_t(extended&255):alphaReferences[(s.flags>>29)&3]):127);
        auto sourceFactor=blended&&!(sourceState&&(s.flags&128))?GPU_SRC_ALPHA:GPU_ONE;
        auto destinationFactor=blended?(sourceState&&(s.flags&64)?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA):GPU_ZERO;
        if(blended&&((extended>>8)&15))sourceFactor=GPU_BLENDFACTOR(((extended>>8)&15)-1);
        if(blended&&((extended>>12)&15))destinationFactor=GPU_BLENDFACTOR(((extended>>12)&15)-1);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,menuIconLayer_?GPU_SRC_ALPHA:sourceFactor,
                      menuIconLayer_?(menuIconAdditive_?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA):destinationFactor,
                      GPU_ONE,menuIconLayer_?(menuIconAdditive_?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA):destinationFactor);
        const bool depthTest=!backdrop&&(!sourceState||(s.flags&(1u<<23)));
        const bool depthWrite=!backdrop&&(sourceState?bool(s.flags&(1u<<24)):!blended);
        C3D_DepthTest(depthTest,GPU_GEQUAL,depthWrite?GPU_WRITE_ALL:GPU_WRITE_COLOR);
        // Retain authored layer order without reverting distant geometry to
        // nonlinear depth. The same mapping is used by actors and shadows.
        if(menuDepthBase_>=0)C3D_DepthMap(true,-menuDepthSpan_,menuDepthBase_);
        else sceneDepthMap(projection_,backdrop?0:s.lighting);
        const bool swap=o&&(o->flags&SubmeshOverride::Texture);
        const int32_t primaryIndex=swap&&o->texture[0]>=0&&size_t(o->texture[0])<model.textures.size()?o->texture[0]:s.texture;
        const int32_t secondaryIndex=swap&&o->texture[1]>=0&&size_t(o->texture[1])<model.textures.size()?o->texture[1]:s.secondary;
        const bool stadium=stadiumTex_[0].data&&model.stadiumTexture>=0&&primaryIndex==model.stadiumTexture;
        const auto* samplers=i<model.samplers.size()?model.samplers[i].data():nullptr;
        auto* texture=stadium?&stadiumTex_[stadiumRead_]:primaryIndex>=0?model.textures[primaryIndex]->sampledGpu(samplers):&white_;
        const GPU_TEXTURE_WRAP_PARAM wraps[]={GPU_CLAMP_TO_EDGE,GPU_REPEAT,GPU_MIRRORED_REPEAT,GPU_CLAMP_TO_EDGE};
        C3D_TexSetWrap(texture,wraps[(s.flags>>8)&3],wraps[(s.flags>>10)&3]);
        C3D_TexBind(0,texture);
        if(stadium){
            // Landscape authored UVs to the cropped portrait texture region.
            constexpr auto origin=stadium::displayUv(0,0),dx=stadium::displayUv(1,0),dy=stadium::displayUv(0,1);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,uvRowLoc_[0],dx.u-origin.u,dy.u-origin.u,origin.u,0);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,uvRowLoc_[1],dx.v-origin.v,dy.v-origin.v,origin.v,0);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);uvDirty_=true;
        }
        auto* secondary=secondaryIndex>=0?model.textures[secondaryIndex]->sampledGpu(samplers?samplers+1:nullptr,1):&white_;
        C3D_TexSetWrap(secondary,wraps[(s.flags>>16)&3],wraps[(s.flags>>18)&3]);
        C3D_TexBind(1,secondary);
        if(s.menuReflection==11||s.menuReflection==22){
            projectedWaterTexture=*secondary;
            projectedWaterTexture.param=(projectedWaterTexture.param&~GPU_TEXTURE_MODE(7))|GPU_TEXTURE_MODE(GPU_TEX_PROJECTION);
            C3D_TexBind(0,&projectedWaterTexture);
            C3D_TexBind(1,texture);
        }
        if(s.menuReflection==8){
            auto* third=model.textures[(s.reflectionConstant&0xffff)-1]->sampledGpu(samplers?samplers+2:nullptr,2);
            C3D_TexSetWrap(third,wraps[(s.reflectionConstant>>16)&3],wraps[(s.reflectionConstant>>18)&3]);
            C3D_TexBind(2,third);
        }
        // Retail course lighting: fog stage, light textures (after the generic setup).
        retailEnvironment=retail&&retailCombiners(s,mv,backdrop,additiveColor||((s.menuReflection!=7&&s.menuReflection!=8&&s.menuReflection!=14)&&o&&(o->flags&SubmeshOverride::Add)),
            additiveColor?(additiveColor>>24)|((additiveColor>>8)&0xff00)|((additiveColor<<8)&0xff0000):o&&(o->flags&SubmeshOverride::Add)?o->add&0xffffff:0,constant);
        // A material binder owns the combiners and may replace any state above.
        if(material)material(context,i);
        // Native CFL owns stages 0..2. Apply the same Star C1 contribution
        // after its material binder, retaining coverage for glasses/masks.
        if(model.nativeMiiLighting&&additiveColor){
            const uint32_t rgb=(additiveColor>>24)|((additiveColor>>8)&0xff00)|((additiveColor<<8)&0xff0000);
            auto* star=C3D_GetTexEnv(4);C3D_TexEnvInit(star);C3D_TexEnvColor(star,rgb);
            C3D_TexEnvSrc(star,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(star,C3D_RGB,GPU_ADD);
            C3D_TexEnvSrc(star,C3D_Alpha,GPU_PREVIOUS);C3D_TexEnvFunc(star,C3D_Alpha,GPU_REPLACE);
        }
        if(inkBlack){auto* ink=C3D_GetTexEnv(5);C3D_TexEnvColor(ink,(ink->color&0xff000000u)|((255u-inkBlack)*0x010101u));
            C3D_TexEnvOpRgb(ink,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
            C3D_TexEnvSrc(ink,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(ink,C3D_RGB,GPU_MODULATE);C3D_TexEnvScale(ink,C3D_RGB,GPU_TEVSCALE_1);}
        last=o?nullptr:&s;lastRetail=retail;lastEnvironment=retailEnvironment;
        } // !repeat
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,projectedWaterLoc_,s.menuReflection==11||s.menuReflection==22);
        if(s.menuReflection==11||s.menuReflection==22){
            // Undo display tilt, map clip [-w,w] to GX texture [0,w],
            // apply the authored SRT, then flip texture T into PICA V.
            // envRow uniforms are exclusive with normal-based EnvCamera.
            float rows[3][4];const float* uv=s.reflectionUv;
            for(unsigned c=0;c<4;++c){
                const float q=projection_.r[3].c[3-c];
                const float u=.5f*(horizontalSign_*projection_.r[1].c[3-c]+q);
                const float t=.5f*(-projection_.r[0].c[3-c]+q);
                rows[0][c]=uv[0]*u+uv[1]*t+uv[2]*q;
                rows[1][c]=q-(uv[3]*u+uv[4]*t+uv[5]*q);
                rows[2][c]=q;
            }
            for(unsigned r=0;r<3;++r)C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[r],rows[r][0],rows[r][1],rows[r][2],rows[r][3]);
            // 3DS water uses the authored flat mesh and scrolling textures.
            // No CPU tessellation or indirect displacement: source UV1 holds
            // texture coordinates, not sampled texel offsets.
            C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,0,0,0,0);
        }
        const bool primaryEnvironment=s.lighting&retail::PrimaryEnvironment;
        const bool menuEnvironment=!model.staticLightingBaked&&!model.fixedLighting&&!(s.lighting&retail::FixedEnvironment)&&(primaryEnvironment||(s.menuReflection&&s.menuReflection!=3&&s.menuReflection!=7&&s.menuReflection!=11&&s.menuReflection!=12&&s.menuReflection!=13&&s.menuReflection!=14&&s.menuReflection!=15&&s.menuReflection!=19&&s.menuReflection!=21&&s.menuReflection!=22&&s.menuReflection!=23&&s.menuReflection!=24&&s.menuReflection!=25&&
            !((s.menuReflection==10||s.menuReflection==20)&&(s.reflectionConstant&512))));
        if((s.lighting&retail::FixedEnvironment)&&s.menuReflection!=8&&!primaryEnvironment){
            // Dynamic EnvCamera replaced the input UV1 before its SRT. Keep
            // that order with baked UV1: do not apply the ordinary UV delta
            // first, or animate an environment transform twice.
            if(model.staticLightingBaked){
                C3D_FVUnifSet(GPU_VERTEX_SHADER,uvRowLoc_[2],1,0,0,0);
                C3D_FVUnifSet(GPU_VERTEX_SHADER,uvRowLoc_[3],0,1,0,0);uvDirty_=true;
            }
            if(s.menuReflection&&normalEnvironment(s.lighting,s.menuReflection,s.reflectionConstant)){
                if(s.menuReflection==17&&uv){float animatedUv[6];animatedEnvironment(s.reflectionUv,uv+6,animatedUv);uv1TransformNow(animatedUv);}
                else uv1TransformNow(s.reflectionUv);
            }
        }
        if(model.staticLightingBaked&&s.menuReflection==8)
            C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentTexture2Loc_,true);
        if(menuEnvironment){
            float animatedUv[6];
            if((s.menuReflection==17||primaryEnvironment)&&uv){
                animatedEnvironment(s.reflectionUv,uv+(primaryEnvironment?0:6),animatedUv);
                environmentUvNow(transform,animatedUv,false);
            }else environmentUvNow(transform,s.reflectionUv,s.menuReflection==8);
            C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentPrimaryLoc_,primaryEnvironment);
        }
        if(parts){
            const bool environment=!model.staticLightingBaked&&!(s.lighting&retail::FixedEnvironment)&&(s.flags&(1u<<25))&&((s.flags>>12)&3)==3&&rigid_->stride>=21;
            for(size_t n=0;n<parts->size();++n){
                if(n<64&&!((shownParts>>n)&1))continue;
                const auto& part=(*parts)[n];
                const float* m=rigid_->palette+part.matrix*rigid_->stride;
                C3D_Mtx bone;Mtx_Identity(&bone);
                for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)bone.r[row].c[3-col]=m[row*4+col];
                C3D_Mtx partView;Mtx_Multiply(&partView,&mv,&bone); // view*object already computed once above
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&partView);
                if(retailEnvironment||menuEnvironment)this->retailEnvironment(partView);
                else if(retail&&!model.fixedLighting&&!model.staticLightingBaked)normalMatrixNow(partView);
                if(environment){
                    for(unsigned row=0;row<3;++row)C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[row],m[12+row*3],m[13+row*3],m[14+row*3],0);
                    C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,std::cos(rigid_->environmentYaw),std::sin(rigid_->environmentYaw),1,0);
                    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,true);
                }
                C3D_DrawElements(GPU_TRIANGLES,part.count,C3D_UNSIGNED_SHORT,model.indices+part.first);
                stats_.triangles+=part.count/3;++stats_.drawCalls;
            }
            if(environment){
                C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,1,0,0,0);
                C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,false);
            }
            if(menuEnvironment||(s.lighting&retail::FixedEnvironment))clearEnvironmentUvNow();
            pendingEnd=retail;
            continue;
        }
        for(unsigned n=0;n<ranges.count;++n){
            const auto range=ranges.ranges[n];
            C3D_DrawElements(GPU_TRIANGLES,range.count,C3D_UNSIGNED_SHORT,drawIndices+range.first);
            stats_.triangles+=range.count/3;++stats_.drawCalls;
        }
        if(menuEnvironment||(s.lighting&retail::FixedEnvironment))clearEnvironmentUvNow();
        pendingEnd=retail;
    }
    if(pendingEnd)retailEndSubmesh();
    sceneDepthMap(projection_);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,projectedWaterLoc_,false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentTexture2Loc_,false);
    // Later draws (sprites, sparks, other models) expect identity UV rows/tint.
    resetMaterialUniforms();
    if(skinPalette_)C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedLoc_,false);
    if(litSkin){
        AttrInfo_Init(attr);AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_FLOAT,3);AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);AttrInfo_AddLoader(attr,3,GPU_UNSIGNED_BYTE,4);
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedNormalsLoc_,false);
    }
    if(model.nativeMiiLighting){C3D_LightEnvBind(nullptr);C3D_BindProgram(&program_);}
}
void Renderer::setMaterialCullNow(GPU_CULLMODE mode){
    C3D_CullFace(cameraCull(mode,view_));
}
void Renderer::shadowVolumesNow(const ShadowVolume* volumes,unsigned count,float darkness,uint8_t excludedStencil){
    flushSprites();
    sceneDepthMap(projection_);
    uploadProjection(projectionLoc_,projection_);
    resetMaterialUniforms();
    for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    const uint32_t shade=uint32_t(std::round(std::clamp(darkness,0.f,1.f)*255.f));
    auto* env=C3D_GetTexEnv(0);C3D_TexEnvColor(env,0xff000000u|shade|(shade<<8)|(shade<<16));
    C3D_TexEnvSrc(env,C3D_Both,GPU_CONSTANT);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    auto drawAll=[&](GPU_CULLMODE cull){
        C3D_CullFace(cameraCull(cull,view_));
        for(unsigned i=0;i<count;++i){
            const auto& v=volumes[i];if(!v.model||!v.model->indexCount)continue;
            C3D_Mtx mv;Mtx_Multiply(&mv,&view_,&v.transform);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&mv);
            auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,v.model->vertices,sizeof(Vertex),4,0x3210);
            C3D_DrawElements(GPU_TRIANGLES,v.model->indexCount,C3D_UNSIGNED_SHORT,v.model->indices);
            stats_.triangles+=v.model->indexCount/3;++stats_.drawCalls;
        }
    };
    // GX winds outward faces clockwise: culling CCW fronts keeps them.
    C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITEMASK(0));
    const uint8_t countMask=uint8_t(~excludedStencil);
    C3D_StencilTest(true,excludedStencil?GPU_EQUAL:GPU_ALWAYS,0,excludedStencil,countMask);
    // Count occluded faces (z-fail). Original closed shadow meshes can
    // intersect the near plane, so z-pass leaves an unbalanced silhouette.
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_DECR_WRAP,GPU_STENCIL_KEEP);
    drawAll(GPU_CULL_FRONT_CCW);
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_INCR_WRAP,GPU_STENCIL_KEEP);
    drawAll(GPU_CULL_BACK_CCW);
    // Shade each marked pixel once and clear its count for later volumes.
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_StencilTest(true,GPU_NOTEQUAL,0,countMask,countMask);
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_ZERO);
    // A screen quad also shades/clears the stencil when the camera is
    // inside a volume. Drawing a volume face here depends on near clipping
    // again, and can leave stray counts for later objects/particles.
    screenFadeNow(1.f-float(shade)/255.f,false,false,shadowUnion_?4:0);
    // The fullscreen shade temporarily binds an orthographic projection.
    // Consecutive volume groups must return to this eye's perspective.
    uploadProjection(projectionLoc_,projection_);
    sceneDepthMap(projection_);
    C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0);
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
    C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
}
void Renderer::environmentUvNow(const C3D_Mtx& transform,const float (&uv)[6],bool texture2,float blend){
    // GX EnvCamera: normalize(view-space normal) -> (0.5+0.5x, 0.5-0.5y) in
    // GX texture space, then the material SRT. The shader yields that point in
    // this renderer's V-up convention (v=1-t); convert the SRT rows to match.
    C3D_Mtx mv;Mtx_Multiply(&mv,&view_,&transform);
    normalMatrixNow(mv,true);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,1,0,std::clamp(blend,0.f,1.f),0);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,true);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentTexture2Loc_,texture2);
    uv1TransformNow(uv);
}
void Renderer::uv1TransformNow(const float (&uv)[6]){
    // GX rows s'=a*s+b*t+tx, t'=c*s+d*t+ty with t=1-v, written back as v'=1-t'.
    const float a=uv[0],b=uv[1],tx=uv[2],c=uv[3],d=uv[4],ty=uv[5];
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[0],a,-b,0,b+tx);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[1],-c,d,0,1-d-ty);
}
void Renderer::setBakedEnvironmentUv(){
    // Material binder only: immutable UV2 was prepared at model load.
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentTexture2Loc_,true);
}
void Renderer::clearEnvironmentUvNow(){
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,playerShadowProjectLoc_,false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,projectedWaterLoc_,false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentTexture2Loc_,false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentPrimaryLoc_,false);
    for(unsigned row=0;row<3;++row)C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[row],row==0,row==1,row==2,0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,1,0,0,0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[0],1,0,0,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[1],0,1,0,0);
}
void Renderer::drawSparksNow(const SparkSpec* specs,unsigned specCount,const Spark* sparks,unsigned sparkCount,const Texture* textures){
    flushSprites();
    uploadProjection(projectionLoc_,projection_);
    sceneDepthMap(projection_);
    const Vec3 right{view_.r[0].x,view_.r[0].y,view_.r[0].z},up{view_.r[1].x,view_.r[1].y,view_.r[1].z};
    const Vec3 viewForward{view_.r[2].x,view_.r[2].y,view_.r[2].z};
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&view_);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);
    for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto* env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_TEXTURE0);C3D_TexEnvFunc(env,C3D_RGB,GPU_INTERPOLATE);
    C3D_TexEnvSrc(env,C3D_Alpha,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Alpha,GPU_MODULATE);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_COLOR);
    // Every quad is written first and flushed once; binds and draws follow in
    // the same order as when each particle flushed its own quad.
    unsigned offset=0;
    for(unsigned pass=(eye_||bottomWorldPass_)?1:0;pass<2;++pass){offset=0;
    for(unsigned id=0;id<specCount;++id){
        const auto& s=specs[id];if(!textures[s.texture].ready)continue;
        if(pass){
        C3D_TexBind(0,const_cast<C3D_Tex*>(&textures[s.texture].gpu));
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,s.texture==2?GPU_ONE_MINUS_SRC_ALPHA:GPU_ONE,GPU_ZERO,GPU_ONE);
        }
        for(unsigned n=0;n<sparkCount;++n)if(const auto& p=sparks[n];p.spec==id&&p.age<p.life){
            if(pass){
                const unsigned count=sparkCounts_[offset/6];
                // Each particle retains its authored random primary AND secondary
                // colours, instead of forcing one constant blue/orange per layer.
                C3D_TexEnvColor(env,uint32_t(p.secondary[0])|(uint32_t(p.secondary[1])<<8)|(uint32_t(p.secondary[2])<<16));
                auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,particles_+offset,sizeof(Vertex),4,0x3210);
                C3D_DrawArrays(GPU_TRIANGLES,0,count);stats_.triangles+=count/3;++stats_.drawCalls;offset+=count;
                continue;
            }
            const unsigned age=std::min(p.age,31u);
            // NW4R draws corners at +/-size: these are half-extents already.
            // The source also links size and scale axes independently.
            const float extentX=p.size[0]*s.scale[age][0];
            const float extentY=p.size[(s.drawFlags&0x2000)?0:1]*s.scale[age][(s.drawFlags&0x4000)?0:1];
            Vec3 axis=up,width=right;
            if(s.shapeType==4){
                axis=normalized(p.direction,p.up);
                if(s.shapePivot&1){
                    axis=normalized(tangent(axis,viewForward),up);
                    width=normalized(cross(axis,viewForward),right);
                }else{
                    // Core flares are two intersecting directional planes. Their
                    // radial emitter axis changes as the wheel moves past them.
                    width=normalized(cross(p.up,axis),right);
                }
            }else if(p.rotation){
                const float angle=p.rotation*float(p.age+1),c=std::cos(angle),sn=std::sin(angle);
                width=right*c+up*sn;axis=up*c-right*sn;
            }
            const Vec3 vertical=axis*extentY;
            // Preserve the BREFF pivots, including -65 and -27 on flash children.
            const Vec3 center=p.position-width*(extentX*s.pivot[0])-axis*(extentY*s.pivot[1]);
            const uint8_t alpha=uint8_t(255.f*std::clamp(s.alpha[age]*p.alphaVariation,0.f,1.f));
            unsigned count=0;
            auto quad=[&](Vec3 horizontal){
                const Vec3 points[]={center-horizontal-vertical,center+horizontal-vertical,center+horizontal+vertical,
                                     center-horizontal-vertical,center+horizontal+vertical,center-horizontal+vertical};
                const float uv[][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
                for(unsigned i=0;i<6;++i)particles_[offset+count++]={points[i],{0,0,1},{uv[i][0],uv[i][1]},{0,0},{p.primary[0],p.primary[1],p.primary[2],alpha}};
            };
            quad(width*extentX);
            if(s.shapeAssist==1)quad(normalized(cross(axis,width),viewForward)*extentX);
            sparkCounts_[offset/6]=uint8_t(count);offset+=count;
        }
    }
    if(!pass)flushGpuData(particles_,offset*sizeof(Vertex));
    }
}
bool Renderer::projectTop(Vec3 p,float& x,float& y,float& viewZ) const{
    const C3D_FVec eye=Mtx_MultiplyFVec4(&mainView_,FVec4_New(p.x,p.y,p.z,1));
    viewZ=eye.z;
    const C3D_FVec clip=Mtx_MultiplyFVec4(&mainProjection_,eye);
    if(clip.w<=0.0001f)return false;
    const float nx=clip.x/clip.w,ny=clip.y/clip.w;
    // Invert the overlay's Mtx_OrthoTilt pixel mapping (affine in x and y).
    C3D_Mtx o;Mtx_OrthoTilt(&o,0,mainScreenWidth_,240,0,-1,1,false);
    const float a=o.r[0].x,b=o.r[0].y,c=o.r[1].x,d=o.r[1].y,det=a*d-b*c;
    if(std::fabs(det)<1e-9f)return false;
    const float rx=nx-o.r[0].w,ry=ny-o.r[1].w;
    x=(rx*d-b*ry)/det;y=(a*ry-c*rx)/det;
    return true;
}
bool Renderer::projectChaseTop(Vec3 p,float& x,float& y,float& viewZ,bool lookBack) const{
    const C3D_FVec eye=Mtx_MultiplyFVec4(lookBack?&mainView_:&chaseView_,FVec4_New(p.x,p.y,p.z,1));
    viewZ=eye.z;
    const C3D_FVec clip=Mtx_MultiplyFVec4(&mainProjection_,eye);
    if(clip.w<=0.0001f)return false;
    const float nx=clip.x/clip.w,ny=clip.y/clip.w;
    // Invert the overlay's Mtx_OrthoTilt pixel mapping (affine in x and y).
    C3D_Mtx o;Mtx_OrthoTilt(&o,0,mainScreenWidth_,240,0,-1,1,false);
    const float a=o.r[0].x,b=o.r[0].y,c=o.r[1].x,d=o.r[1].y,det=a*d-b*c;
    if(std::fabs(det)<1e-9f)return false;
    const float rx=nx-o.r[0].w,ry=ny-o.r[1].w;
    x=(rx*d-b*ry)/det;y=(a*ry-c*rx)/det;
    return true;
}
namespace {
GPU_TEVSRC tevSource(ef::Src s){
    switch(s){
    case ef::Src::Vertex:return GPU_PRIMARY_COLOR;
    case ef::Src::Tex0:return GPU_TEXTURE0;
    case ef::Src::Tex1:return GPU_TEXTURE1;
    case ef::Src::Lookup:return GPU_TEXTURE2;
    case ef::Src::Const:return GPU_CONSTANT;
    case ef::Src::Buffer:return GPU_PREVIOUS_BUFFER;
    default:return GPU_PREVIOUS;
    }
}
GPU_TEVOP_RGB rgbOperand(ef::Operand o){
    switch(o){
    case ef::Operand::OneMinusColor:return GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR;
    case ef::Operand::Alpha:return GPU_TEVOP_RGB_SRC_ALPHA;
    case ef::Operand::OneMinusAlpha:return GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA;
    default:return GPU_TEVOP_RGB_SRC_COLOR;
    }
}
GPU_TEVOP_A alphaOperand(ef::Operand o){return o==ef::Operand::OneMinusAlpha?GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA:GPU_TEVOP_A_SRC_ALPHA;}
GPU_COMBINEFUNC combineFunc(ef::Func f){
    switch(f){
    case ef::Func::Modulate:return GPU_MODULATE;
    case ef::Func::Add:return GPU_ADD;
    case ef::Func::AddSigned:return GPU_ADD_SIGNED;
    case ef::Func::Interpolate:return GPU_INTERPOLATE;
    case ef::Func::Subtract:return GPU_SUBTRACT;
    case ef::Func::MultiplyAdd:return GPU_MULTIPLY_ADD;
    default:return GPU_REPLACE;
    }
}
GPU_TEVSCALE tevScale(u8 s){return s==4?GPU_TEVSCALE_4:s==2?GPU_TEVSCALE_2:GPU_TEVSCALE_1;}
GPU_TEXTURE_WRAP_PARAM wrapParam(unsigned gx){return gx==2?GPU_MIRRORED_REPEAT:gx==1?GPU_REPEAT:GPU_CLAMP_TO_EDGE;}
GPU_BLENDFACTOR blendFactor(u8 gx){
    switch(gx){
    case 0:return GPU_ZERO;
    case 1:return GPU_ONE;
    case 4:return GPU_SRC_ALPHA;
    case 5:return GPU_ONE_MINUS_SRC_ALPHA;
    case 6:return GPU_DST_ALPHA;
    case 7:return GPU_ONE_MINUS_DST_ALPHA;
    default:return GPU_ONE;
    }
}
// GX compare (NEVER LESS EQUAL LEQUAL GREATER NEQUAL GEQUAL ALWAYS) -> PICA test
// function; depth is reversed on this renderer (greater is nearer).
GPU_TESTFUNC alphaTest(u8 gx){
    static constexpr GPU_TESTFUNC map[8]={GPU_NEVER,GPU_LESS,GPU_EQUAL,GPU_LEQUAL,GPU_GREATER,GPU_NOTEQUAL,GPU_GEQUAL,GPU_ALWAYS};
    return map[gx&7];
}
GPU_TESTFUNC depthTest(u8 gx){
    static constexpr GPU_TESTFUNC map[8]={GPU_NEVER,GPU_GREATER,GPU_EQUAL,GPU_GEQUAL,GPU_LESS,GPU_NOTEQUAL,GPU_LEQUAL,GPU_ALWAYS};
    return map[gx&7];
}
}
void Renderer::updateEffectState(const RaceEffects& effects){
    EffectDrawState& state=*effectDraw_;
    auto& system=effects.system();
    if(state.owner==&effects&&state.resources==system.resources.size()&&state.sizes.size()==effects.textures.size())return;
    // A recorded frame still being drawn reads these materials.
    if(recording())drain();
    state.owner=&effects;state.resources=system.resources.size();
    state.materials.assign(system.resources.size(),{});
    for(size_t i=0;i<system.resources.size();++i)ef::translateMaterial(system.resources[i].desc.draw,state.materials[i]);
    state.sizes.clear();
    for(const auto& t:effects.textures)state.sizes.push_back({t.width,t.height,t.paddedWidth,t.paddedHeight});
}
void Renderer::buildEffectList(RaceEffects& effects,const C3D_Mtx& viewMatrix,ef::DrawList& list){
    EffectDrawState& state=*effectDraw_;
    ef::Mtx34 view;
    for(int row=0;row<3;++row)for(int col=0;col<4;++col)view.m[row][col]=viewMatrix.r[row].c[3-col];
    list.clear();
    // Keep the accepted dual-LCD ceremony path intact. Ordinary race effects
    // use the centre camera's conservative stereo frustum in eye coordinates.
    float rows[16];for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=cullProjection_.r[r].c[3-c];
    const ClipFrustum frustum(rows);
    ef::buildDrawList(effects.system(),state.materials,state.sizes.data(),uint32_t(state.sizes.size()),view,list,kEffectVertices,
        frameDualScreenIntro_?nullptr:&frustum);
}
// The immediate path; recorded frames build the list in drawEffects (render_thread.cpp).
void Renderer::drawEffectsImmediate(RaceEffects& effects,const Texture* textures){
    updateEffectState(effects);
    buildEffectList(effects,view_,effectDraw_->list);
    effectListNow(effects,effectDraw_->list,textures);
}
EffectLayerBuffer::~EffectLayerBuffer(){
    if(vertices)linearFree(vertices);
    if(lookup.data)C3D_TexDelete(&lookup);
}
bool EffectLayerBuffer::init(){
    if(!vertices)vertices=linearAlloc(VertexCapacity*sizeof(ef::DrawVertex));
    if(!vertices)return false;
    if(!lookup.data&&!C3D_TexInit(&lookup,ef::DrawList::LookupSize,ef::DrawList::LookupSize,GPU_RGBA8))return false;
    C3D_TexSetFilter(&lookup,GPU_NEAREST,GPU_NEAREST);
    C3D_TexSetWrap(&lookup,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    return true;
}
void Renderer::drawSceneEffectsImmediate(RaceEffects& effects,const Texture* textures,const EffectLayerBuffer& buffer){
    updateEffectState(effects);buildEffectList(effects,view_,effectDraw_->list);
    sceneEffectListNow(effects,effectDraw_->list,textures,buffer);
}
void Renderer::sceneEffectListNow(const RaceEffects& effects,const ef::DrawList& list,const Texture* textures,const EffectLayerBuffer& buffer){
    flushSprites();
    const auto oldLookup=effectLookup_;auto* oldVertices=effectVertices_;
    effectLookup_=buffer.lookup;effectVertices_=buffer.vertices;
    effectListNow(effects,list,textures);
    effectLookup_=oldLookup;effectVertices_=oldVertices;
}
void Renderer::drawMenuEffects(RaceEffects& effects,const Texture* textures,EffectLayerBuffer& buffer){
    if(recording()||!effects.ready||!effects.liveParticles()||!buffer.vertices||!buffer.lookup.data)return;
    static_assert(kEffectVertices==EffectLayerBuffer::VertexCapacity);
    flushSprites();
    const auto oldProjection=projection_,oldView=view_;
    const auto oldLookup=effectLookup_;auto* oldVertices=effectVertices_;
    effectLookup_=buffer.lookup;effectVertices_=buffer.vertices;
    Mtx_PerspTilt(&projection_,C3D_AngleFromDegrees(45.f),5.f/3.f,10.f,100000.f,false);
    Mtx_LookAt(&view_,FVec3_New(0,0,1000),FVec3_New(0,0,0),FVec3_New(0,1,0),false);
    for(unsigned col=0;col<4;++col)
        projection_.r[2].c[col]=.01f*projection_.r[2].c[col]-.95f*projection_.r[3].c[col];
    drawEffectsImmediate(effects,textures);
    projection_=oldProjection;view_=oldView;effectLookup_=oldLookup;effectVertices_=oldVertices;
}
void Renderer::effectListNow(const RaceEffects& effects,const ef::DrawList& list,const Texture* textures){
    // Award world and driving banks can coexist in one recorded frame.
    updateEffectState(effects);
    EffectDrawState& state=*effectDraw_;
    if(state.generation!=effects.generation()){state.generation=effects.generation();state.diagnosticMask=0;}
    // Submit only complete triangles within the existing 8192-vertex allocation.
    const size_t vertexCount=ef::triangleVertexCount(list.vertices.size(),kEffectVertices);
    if(!vertexCount)return;
    flushSprites();
    // Both eyes and the intro's lower view reuse the centre camera list;
    // only its first top pass writes buffers that the GPU will read.
    if(!eye_&&!bottomWorldPass_){
    std::memcpy(effectVertices_,list.vertices.data(),vertexCount*sizeof(ef::DrawVertex));
    flushGpuData(effectVertices_,vertexCount*sizeof(ef::DrawVertex));
    // Lookup texels in index order are already in tiled order (8x8 Morton tiles).
    uint8_t* texels=static_cast<uint8_t*>(effectLookup_.data);
    for(size_t i=0;i<list.lookup.size();++i){
        const uint32_t c=list.lookup[i];
        texels[i*4+0]=uint8_t(c>>24);texels[i*4+1]=uint8_t(c>>16);texels[i*4+2]=uint8_t(c>>8);texels[i*4+3]=uint8_t(c);
    }
    C3D_TexFlush(&effectLookup_);
    }
    C3D_BindProgram(&effectProgram_);
    uploadProjection(effectProjectionLoc_,projection_);
    sceneDepthMap(projection_);
    C3D_AttrInfo* attr=C3D_GetAttrInfo();AttrInfo_Init(attr);
    static_assert(sizeof(ef::DrawVertex)==44,"position 3f, unit 0 3f, units 1 and 2 4f, colour 4ub");
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_FLOAT,3);AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);AttrInfo_AddLoader(attr,3,GPU_UNSIGNED_BYTE,4);
    auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,effectVertices_,sizeof(ef::DrawVertex),4,0x3210);
    C3D_TexBind(2,&effectLookup_);
    C3D_TexEnvBufColor(0);
    // Unit 0 in projection mode for projected layers; bound by pointer until the draw.
    C3D_Tex projected{};
    for(const ef::DrawBatch& b:list.batches){
        if(b.first>=vertexCount)break;
        const uint32_t count=ef::drawBatchVertexCount(b,vertexCount);
        if(!count)continue;
        const ef::Material& m=state.materials[b.res->index];
        const ef::DrawSetting& d=b.res->desc.draw;
        bool missing=false;
        for(int u=0;u<2;++u){
            const uint32_t t=b.texture[u];
            if(t==0xffffffffu||t>=effects.textures.size()){C3D_TexBind(u,nullptr);continue;}
            const Texture& tex=textures[t];
            if(!tex.ready){missing=true;continue;}
            C3D_Tex* g=const_cast<C3D_Tex*>(&tex.gpu);
            C3D_TexSetFilter(g,GPU_LINEAR,GPU_LINEAR);
            C3D_TexSetWrap(g,wrapParam(b.wrap[u]&3u),wrapParam((b.wrap[u]>>2)&3u));
            if(u==0&&m.projected){
                projected=*g;
                projected.param=(projected.param&~GPU_TEXTURE_MODE(7))|GPU_TEXTURE_MODE(GPU_TEX_PROJECTION);
                g=&projected;
            }
            C3D_TexBind(u,g);
        }
#ifndef MKW_RELEASE
        constexpr const char* diagnosed[]={"rk_itemBox","rk_bananaSmk","rk_miniTurbo","rk_spinHalfL","rk_spinHalfR","rk_spinL","rk_spinR","rk_driftSpark1LB_Spark00","rk_driftSpark1LB_Spark01","rk_driftSpark1RB_Spark00","rk_driftSpark1RB_Spark01","rk_driftSpark1CB_Spark00","rk_driftSpark1CB_Spark01"};
        for(unsigned i=0;i<std::size(diagnosed);++i)if(!std::strcmp(b.res->name,diagnosed[i])&&!(state.diagnosticMask&(1u<<i))){
            state.diagnosticMask|=1u<<i;
            if(FILE* log=std::fopen("sdmc:/3ds/mkw-3ds/effects-state.txt","a")){
                std::fprintf(log,"draw generation=%u root=%s vertices=%lu missingTexture=%u linear=%lu\n",effects.generation(),b.res->name,(unsigned long)count,missing,(unsigned long)linearSpaceFree());
                std::fclose(log);
            }
        }
#endif
        if(missing)continue;
        int rgbBuffer=0,alphaBuffer=0;
        for(int i=0;i<6;++i){
            C3D_TexEnv* env=C3D_GetTexEnv(i);C3D_TexEnvInit(env);
            if(i>=m.stages)continue;
            const ef::CombinerStage& st=m.stage[i];
            C3D_TexEnvSrc(env,C3D_RGB,tevSource(st.rgbSrc[0]),tevSource(st.rgbSrc[1]),tevSource(st.rgbSrc[2]));
            C3D_TexEnvSrc(env,C3D_Alpha,tevSource(st.alphaSrc[0]),tevSource(st.alphaSrc[1]),tevSource(st.alphaSrc[2]));
            C3D_TexEnvOpRgb(env,rgbOperand(st.rgbOp[0]),rgbOperand(st.rgbOp[1]),rgbOperand(st.rgbOp[2]));
            C3D_TexEnvOpAlpha(env,alphaOperand(st.alphaOp[0]),alphaOperand(st.alphaOp[1]),alphaOperand(st.alphaOp[2]));
            C3D_TexEnvFunc(env,C3D_RGB,combineFunc(st.rgbFunc));C3D_TexEnvFunc(env,C3D_Alpha,combineFunc(st.alphaFunc));
            C3D_TexEnvScale(env,C3D_RGB,tevScale(st.rgbScale));C3D_TexEnvScale(env,C3D_Alpha,tevScale(st.alphaScale));
            const uint8_t* k=b.constant[i];
            C3D_TexEnvColor(env,uint32_t(k[0])|uint32_t(k[1])<<8|uint32_t(k[2])<<16|uint32_t(k[3])<<24);
            if(st.bufferRgb)rgbBuffer|=1<<i;
            if(st.bufferAlpha)alphaBuffer|=1<<i;
        }
        C3D_TexEnvBufUpdate(C3D_RGB,rgbBuffer);C3D_TexEnvBufUpdate(C3D_Alpha,alphaBuffer);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,blendFactor(d.blendSrc),blendFactor(d.blendDst),blendFactor(d.blendSrc),blendFactor(d.blendDst));
        const bool zTest=d.flags&ef::DrawSetting::ZTest;
        C3D_DepthTest(zTest,zTest?depthTest(d.zCompare):GPU_ALWAYS,(d.flags&ef::DrawSetting::ZUpdate)?GPU_WRITE_ALL:GPU_WRITE_COLOR);
        // GX alpha compare (comp0 ref0 AND comp1 ref1): a side that always passes (ALWAYS,
        // LEQUAL 255, GEQUAL 0) drops out, leaving one PICA test. The cooker refuses others.
        auto passes=[](uint8_t comp,uint8_t ref){return comp==7||(comp==3&&ref==255)||(comp==6&&ref==0);};
        const bool pass0=passes(d.acmpComp0,b.alphaRef0),pass1=passes(d.acmpComp1,b.alphaRef1);
        if(pass0&&pass1)C3D_AlphaTest(false,GPU_ALWAYS,0);
        else if(pass1)C3D_AlphaTest(true,alphaTest(d.acmpComp0),b.alphaRef0);
        else C3D_AlphaTest(true,alphaTest(d.acmpComp1),b.alphaRef1);
        // GX front faces wind clockwise here, as for models (GX_CULL_BACK keeps them under
        // GPU_CULL_FRONT_CCW): GX_CULL_FRONT (1) is GPU_CULL_BACK_CCW, GX_CULL_BACK (2) the other.
        C3D_CullFace(cameraCull(b.cull==1?GPU_CULL_BACK_CCW:b.cull==2?GPU_CULL_FRONT_CCW:GPU_CULL_NONE,view_));
        C3D_DrawArrays(GPU_TRIANGLES,int(b.first),int(count));
        stats_.triangles+=count/3;++stats_.drawCalls;
    }
    // Back to the scene program and its state.
    C3D_TexEnvBufUpdate(C3D_Both,0);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_TexBind(0,nullptr);C3D_TexBind(1,nullptr);C3D_TexBind(2,nullptr);
    C3D_BindProgram(&program_);
    attr=C3D_GetAttrInfo();AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_FLOAT,3);AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);AttrInfo_AddLoader(attr,3,GPU_UNSIGNED_BYTE,4);
    for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
}
Vertex* Renderer::reserveOverlay(int count){
    if(count<=0 || count>kOverlayCapacity-overlayCount_)std::abort();
    Vertex* vertices=overlay_+(bottomPass_?kOverlayCapacity:0)+overlayCount_;
    overlayCount_+=count;
    return vertices;
}
void Renderer::cinemaBarsNow(float topHeight,float bottomHeight,float opacity){
    flushSprites();
    Vertex* vertices=reserveOverlay(12);
    const auto alpha=static_cast<uint8_t>(std::round(std::clamp(opacity,0.f,1.f)*255.f));
    auto rectangle=[&](int first,float top,float bottom){
        const Vec3 positions[6]={{0,top,0},{screenWidth_,top,0},{screenWidth_,bottom,0},
                                 {0,top,0},{screenWidth_,bottom,0},{0,bottom,0}};
        for(int i=0;i<6;++i)vertices[first+i]={positions[i],{0,0,1},{0,0},{0,0},{0,0,0,alpha}};
    };
    if(writesVertices()){
        rectangle(0,0,240.f*std::clamp(topHeight,0.f,1.f));
        rectangle(6,240.f*(1.f-std::clamp(bottomHeight,0.f,1.f)),240.f);
        flushGpuData(vertices,12*sizeof(Vertex));
    }
    C3D_Mtx projection,identity;Mtx_OrthoTilt(&projection,0,screenWidth_,240,0,-1,1,false);Mtx_Identity(&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&identity);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);
    for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
    auto* env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
    auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,vertices,sizeof(Vertex),4,0x3210);
    C3D_DrawArrays(GPU_TRIANGLES,0,12);stats_.triangles+=4;++stats_.drawCalls;
}
void Renderer::shadowUnionNow(bool active){
    flushSprites();resetMaterialUniforms();clearEnvironmentUvNow();
    C3D_StencilTest(true,GPU_ALWAYS,0,0xff,0xff);
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_ZERO);
    auto* attr=C3D_GetAttrInfo();AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_FLOAT,3);
    AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);AttrInfo_AddLoader(attr,3,GPU_UNSIGNED_BYTE,4);
    screenFadeNow(active?0.f:1.f,false,false,active?1:2);
    shadowUnion_=active;
    C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0);
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
    uploadProjection(projectionLoc_,projection_);sceneDepthMap(projection_);
    C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
    spriteStateReady_=false;
}
void Renderer::screenFadeNow(float opacity,bool white,bool depthOnly,int shadowMode){
    flushSprites();
    const auto alpha=static_cast<uint8_t>(std::round(std::clamp(opacity,0.f,1.f)*255.f));
    const uint8_t value=white?255:0;
    const Vec3 positions[6]={{0,0,0},{screenWidth_,0,0},{screenWidth_,240,0},
                             {0,0,0},{screenWidth_,240,0},{0,240,0}};
    Vertex* fade=reserveOverlay(6);
    if(writesVertices()){
        for(int i=0;i<6;++i)fade[i]={positions[i],{0,0,1},{0,0},{0,0},{value,value,value,alpha}};
        flushGpuData(fade,6*sizeof(Vertex));
    }
    C3D_Mtx projection,identity;Mtx_OrthoTilt(&projection,0,screenWidth_,240,0,-1,1,false);Mtx_Identity(&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&identity);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);
    for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
    auto* env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_DepthTest(depthOnly,GPU_ALWAYS,depthOnly?GPU_WRITE_DEPTH:GPU_WRITE_COLOR);
    if(depthOnly)C3D_DepthMap(true,0,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
    // Shadow modes: clear alpha, resolve once, preserve alpha for stencil,
    // or accumulate fallback volume coverage. RGB/depth stay intact until resolve.
    if(shadowMode){
        C3D_DepthTest(false,GPU_ALWAYS,shadowMode==2?GPU_WRITE_COLOR:shadowMode==3?GPU_WRITEMASK(0):GPU_WRITE_ALPHA);
        if(shadowMode==2)C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ZERO,GPU_ONE_MINUS_DST_ALPHA,GPU_ONE,GPU_ZERO);
        else C3D_AlphaBlend(GPU_BLEND_ADD,shadowMode==4?GPU_BLEND_MAX:GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    }
    auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,fade,sizeof(Vertex),4,0x3210);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);stats_.triangles+=2;++stats_.drawCalls;
}
void Renderer::spriteState(){
    if(spriteStateReady_)return;
    C3D_Mtx projection=screenProjection(screenParallax_),identity;Mtx_Identity(&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&identity);
    // Normal (0,0,1) with light (0,0,1,1) keeps vertex colour at full intensity.
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);
    for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
    auto* env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,spriteBase_,sizeof(Vertex),4,0x3210);
    spriteStateReady_=true;spriteRunTexture_=nullptr;spriteRunFirst_=spriteCount_;
}
void Renderer::flushSprites(){
    if(spriteRunTexture_&&spriteCount_>spriteRunFirst_){
        const int count=spriteCount_-spriteRunFirst_;
        C3D_DrawArrays(GPU_TRIANGLES,6*spriteRunFirst_,6*count);stats_.triangles+=2*unsigned(count);++stats_.drawCalls;
    }
    spriteRunFirst_=spriteCount_;spriteRunTexture_=nullptr;spriteStateReady_=false;
}
void Renderer::spriteNow(const Texture& texture,float x,float y,float width,float height,float u0,float v0,float u1,float v1,float alpha,uint32_t rgba,const uint32_t* corners,bool additive,float angle){
    if(spriteCount_>=spriteCapacity_)return;
    spriteState();
    if(spriteRunTexture_!=&texture.gpu||spriteRunAdditive_!=additive){
        // A texture or blend change closes the current run of quads.
        if(spriteRunTexture_&&spriteCount_>spriteRunFirst_){
            const int count=spriteCount_-spriteRunFirst_;
            C3D_DrawArrays(GPU_TRIANGLES,6*spriteRunFirst_,6*count);stats_.triangles+=2*unsigned(count);++stats_.drawCalls;
        }
        spriteRunFirst_=spriteCount_;spriteRunTexture_=&texture.gpu;spriteRunAdditive_=additive;
        C3D_TexBind(0,const_cast<C3D_Tex*>(&texture.gpu));
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,additive?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
    }
    Vertex* v=spriteBase_+6*spriteCount_++;
    if(!writesVertices())return; // the left eye wrote this quad
    // tex3ds images are stored bottom-up: v=1 is the top image row.
    Vec3 p[6]={{x,y,0},{x+width,y,0},{x+width,y+height,0},{x,y,0},{x+width,y+height,0},{x,y+height,0}};
    if(angle!=0){
        const float cx=x+width*.5f,cy=y+height*.5f,c=std::cos(angle),s=std::sin(angle);
        for(auto& q:p){const float dx=q.x-cx,dy=q.y-cy;q.x=cx+dx*c-dy*s;q.y=cy+dx*s+dy*c;}
    }
    const float uv[6][2]={{u0,1-v0},{u1,1-v0},{u1,1-v1},{u0,1-v0},{u1,1-v1},{u0,1-v1}};
    constexpr unsigned cornerIndex[6]={0,1,3,0,3,2};
    for(int i=0;i<6;++i){
        const uint32_t corner=corners?corners[cornerIndex[i]]:0xffffffff;
        auto channel=[&](unsigned shift){return uint8_t((((rgba>>shift)&255)*((corner>>shift)&255)+127)/255);};
        const auto a=uint8_t(std::round(std::clamp(alpha,0.f,1.f)*channel(0)));
        v[i]={p[i],{0,0,1},{uv[i][0],uv[i][1]},{0,0},{channel(24),channel(16),channel(8),a}};
    }
}
void Renderer::fontSpriteNow(const Texture& texture,float x,float y,float width,float height,float u1,float v1,float alpha,uint32_t gradientTop,uint32_t gradientBottom,uint32_t color0,uint32_t color1){
    if(spriteCount_>=spriteCapacity_)return;
    flushSprites();
    Vertex* vertices=spriteBase_+6*spriteCount_++;spriteRunFirst_=spriteCount_;
    const Vec3 p[6]={{x,y,0},{x+width,y,0},{x+width,y+height,0},{x,y,0},{x+width,y+height,0},{x,y+height,0}};
    const float uv[6][2]={{0,1},{u1,1},{u1,1-v1},{0,1},{u1,1-v1},{0,1-v1}};
    if(writesVertices())for(unsigned i=0;i<6;++i){
        const uint32_t rgba=(i==2||i>=4)?gradientBottom:gradientTop;
        const auto a=uint8_t(std::round(std::clamp(alpha,0.f,1.f)*float(rgba&0xff)));
        vertices[i]={p[i],{0,0,1},{uv[i][0],uv[i][1]},{0,0},{uint8_t(rgba>>24),uint8_t(rgba>>16),uint8_t(rgba>>8),a}};
    }
    // Sprite-buffer quads flush with the rest of it in end() (or beginBottom);
    // a mid-frame command-list split flushes all of LINEAR first.
    C3D_Mtx projection=screenProjection(screenParallax_),identity;Mtx_Identity(&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&identity);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);
    C3D_TexBind(0,const_cast<C3D_Tex*>(&texture.gpu));
    for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
    const auto gpuColor=[](uint32_t rgba){return (rgba>>24)|((rgba>>8)&0xff00)|((rgba<<8)&0xff0000)|(rgba<<24);};
    auto* e0=C3D_GetTexEnv(0);C3D_TexEnvColor(e0,gpuColor(color0));
    C3D_TexEnvSrc(e0,C3D_Both,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_Both,GPU_REPLACE);
    auto* e1=C3D_GetTexEnv(1);C3D_TexEnvColor(e1,gpuColor(color1));
    C3D_TexEnvSrc(e1,C3D_Both,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(e1,C3D_Both,GPU_INTERPOLATE);
    auto* e2=C3D_GetTexEnv(2);C3D_TexEnvSrc(e2,C3D_Both,GPU_PREVIOUS,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e2,C3D_Both,GPU_MODULATE);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
    auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,vertices,sizeof(Vertex),4,0x3210);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);stats_.triangles+=2;++stats_.drawCalls;
}
void Renderer::materialQuadsNow(const Texture* texture,const MaterialQuad* quads,unsigned count,uint32_t color0,uint32_t color1,int wrapS,int wrapT,bool additive){
    if(!count||spriteCount_>=spriteCapacity_)return;
    count=std::min(count,unsigned(spriteCapacity_-spriteCount_));
    flushSprites();
    Vertex* vertices=spriteBase_+6*spriteCount_;const int first=spriteCount_;
    spriteCount_+=int(count);spriteRunFirst_=spriteCount_;
    constexpr unsigned order[6]={0,1,3,0,3,2};
    if(writesVertices())for(unsigned q=0;q<count;++q){
        const auto& quad=quads[q];
        for(unsigned i=0;i<6;++i){
            const unsigned k=order[i];const uint32_t rgba=quad.color[k];
            // tex3ds images are stored bottom-up.
            vertices[q*6+i]={{quad.x[k],quad.y[k],0},{0,0,1},{quad.u[k],1-quad.v[k]},{0,0},{uint8_t(rgba>>24),uint8_t(rgba>>16),uint8_t(rgba>>8),uint8_t(rgba)}};
        }
    }
    // Flushed with the sprite buffer in end() (see drawScreenFontSprite).
    C3D_Mtx projection=screenProjection(screenParallax_),identity;Mtx_Identity(&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&identity);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);
    for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
    const auto gpuColor=[](uint32_t rgba){return (rgba>>24)|((rgba>>8)&0xff00)|((rgba<<8)&0xff0000)|(rgba<<24);};
    auto* e0=C3D_GetTexEnv(0);
    if(texture){
        // NW4R default material: lerp(C0, C1, texture) x vertex colour.
        if(wrapS>=0)C3D_TexSetWrap(const_cast<C3D_Tex*>(&texture->gpu),GPU_TEXTURE_WRAP_PARAM(wrapS),GPU_TEXTURE_WRAP_PARAM(wrapT));
        C3D_TexBind(0,const_cast<C3D_Tex*>(&texture->gpu));
        C3D_TexEnvColor(e0,gpuColor(color0));C3D_TexEnvSrc(e0,C3D_Both,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_Both,GPU_REPLACE);
        auto* e1=C3D_GetTexEnv(1);C3D_TexEnvColor(e1,gpuColor(color1));
        C3D_TexEnvSrc(e1,C3D_Both,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(e1,C3D_Both,GPU_INTERPOLATE);
        auto* e2=C3D_GetTexEnv(2);C3D_TexEnvSrc(e2,C3D_Both,GPU_PREVIOUS,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e2,C3D_Both,GPU_MODULATE);
    }else{
        C3D_TexEnvColor(e0,gpuColor(color1));C3D_TexEnvSrc(e0,C3D_Both,GPU_CONSTANT,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(e0,C3D_Both,GPU_MODULATE);
    }
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,additive?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
    auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,spriteBase_,sizeof(Vertex),4,0x3210);
    C3D_DrawArrays(GPU_TRIANGLES,6*first,6*int(count));stats_.triangles+=2*count;++stats_.drawCalls;
}
void Renderer::textNow(const char* text,float x,float y,float size,uint32_t rgba){
    flushSprites();
    // Five columns, seven rows. No platform font or additional texture dependency.
    static const uint8_t alphabet[][5]={
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
        {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0,0x41,0x7f,0x41,0},
        {0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
        {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},
        {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t digits[][5]={{0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e}};
    static const uint8_t slash[]={0x60,0x18,0x04,0x03,0},colon[]={0,0x36,0x36,0,0},bang[]={0,0,0x5f,0,0},dash[]={8,8,8,8,8},dot[]={0,0x60,0x60,0,0};
    unsigned first=textCount_;const float left=x;
    for(;*text;++text,x+=6*size){
        unsigned char c=*text;if(c>='a'&&c<='z')c-=32;
        if(c=='\n'){x=left-6*size;y+=9*size;continue;}
        const uint8_t* mask=c>='A'&&c<='Z'?alphabet[c-'A']:c>='0'&&c<='9'?digits[c-'0']:c=='/'?slash:c==':'?colon:c=='!'?bang:c=='-'?dash:c=='.'?dot:nullptr;
        if(!mask)continue;
        for(unsigned col=0;col<5;++col)for(unsigned row=0;row<7;++row)if(mask[col]&(1u<<row)){
            if(textCount_+6>kTextVertexLimit)break;
            if(!writesVertices()){textCount_+=6;continue;}
            float px=x+col*size,py=y+row*size;
            const Vec3 positions[]={{px,py,0},{px+size,py,0},{px+size,py+size,0},{px,py,0},{px+size,py+size,0},{px,py+size,0}};
            for(auto p:positions)text_[textCount_++]={p,{0,0,1},{0,0},{0,0},{uint8_t(rgba>>24),uint8_t(rgba>>16),uint8_t(rgba>>8),uint8_t(rgba)}};
        }
    }
    if(first==textCount_)return;
    Vertex* vertices=text_+first;unsigned count=textCount_-first;if(writesVertices())flushGpuData(vertices,count*sizeof(Vertex));
    C3D_Mtx projection=screenProjection(screenParallax_),identity;Mtx_Identity(&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&identity);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);
    for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
    auto* env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
    auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,vertices,sizeof(Vertex),4,0x3210);C3D_DrawArrays(GPU_TRIANGLES,0,count);stats_.triangles+=count/3;++stats_.drawCalls;
}
// Every modified vertex/texture range is explicitly flushed by its writer.
// Citro3D may flush the command list without redundantly flushing the LINEAR heap.
bool Renderer::configureStadiumScreen(bool enabled){
    if(enabled&&stadiumTex_[0].data)return true;
    waitForGpu();
    if(!enabled){freeStadiumScreen();return true;}
    // Allocate capture/composite scratch at scene load, never at first visibility.
    if(!initBloom())return false;
    for(unsigned i=0;i<2;++i){
        auto& t=stadiumTex_[i];
        if(!C3D_TexInitVRAM(&t,stadium::TextureWidth,stadium::TextureHeight,GPU_RGBA8)||
           !(stadiumTarget_[i]=C3D_RenderTargetCreateFromTex(&t,GPU_TEXFACE_2D,0,-1))){freeStadiumScreen();return false;}
        C3D_TexSetFilter(&t,GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(&t,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        C3D_RenderTargetClear(stadiumTarget_[i],C3D_CLEAR_COLOR,0x000000FF,0);
    }
    return true;
}
void Renderer::freeStadiumScreen(){
    for(auto*& target:stadiumTarget_){if(target)C3D_RenderTargetDelete(target);target=nullptr;}
    for(auto& t:stadiumTex_){if(t.data)C3D_TexDelete(&t);t={};}
    stadiumRead_=0;
    stadiumRequested_=stadiumFrame_=stadiumPending_=false;stadiumVisible_=true;stadiumScreens_.clear();
    stadiumRenderFrames_=0;stadiumCaptureFrame_=false;
}
void Renderer::captureStadiumScreenNow(){
    stadiumFrame_=false;
    if(eye_||!sceneTarget_)return;
    // Retail WLscreenGC copies the scene EFB before HUD. Keep that ordering
    // and the existing half-render-frame/visible-screen policy. Rendering a
    // crop directly into the next native target avoids LCD cache invalidation,
    // CPU pixel conversion, a synchronous readback and all captured UI.
    flushSprites();
    auto replace=[](){
        C3D_TexEnvBufUpdate(C3D_Both,0);
        for(unsigned i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
        auto* e=C3D_GetTexEnv(0);C3D_TexEnvSrc(e,C3D_Both,GPU_TEXTURE0);
        C3D_TexEnvFunc(e,C3D_Both,GPU_REPLACE);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    };
    bloomPass(stadiumTarget_[stadiumRead_^1],stadium::CaptureWidth,stadium::CaptureHeight);
    replace();
    constexpr float cropY=184.f*10.f/19.f;
    bloomQuad(sceneTex_,stadium::CaptureWidth,stadium::CaptureHeight,
              stadium::CaptureWidth,stadium::CaptureHeight,
              float(stadium::CaptureWidth)-400.f+stadium::CaptureX,
              float(stadium::CaptureHeight)-240.f+cropY);
    C3D_TexBind(0,nullptr);
    stadiumPending_=true; // both eyes still sample the previous complete image
    if(!bloomFrame_){
        bloomPass(topTarget(),400,240);replace();
        bloomQuad(sceneTex_,400,240,400,240,0,0);
        C3D_TexBind(0,nullptr);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
        uploadProjection(projectionLoc_,projection_);
        spriteStateReady_=false;
    }
}

void Renderer::requestBloomNow(const CourseBloom& settings){
    bloomRequested_=settings.enabled();bloomSettings_=&settings;
}
bool Renderer::initBloomTargets(){
    if(sceneTarget_)return true;
    if(bloomFailed_)return false;
    // Portrait targets: full 64x128 near and 32x64 far bloom textures.
    // The scene still occupies its native 240x400 region in 256x512.
    bool ok=C3D_TexInitVRAM(&sceneTex_,256,512,GPU_RGBA8)&&
        (sceneTarget_=C3D_RenderTargetCreateFromTex(&sceneTex_,GPU_TEXFACE_2D,0,GPU_RB_DEPTH24_STENCIL8))!=nullptr;
    for(unsigned i=0;ok&&i<std::size(bloomTex_);++i){
        const unsigned width=i>=2?BloomFarWidth:BloomNearWidth,height=i>=2?BloomFarHeight:BloomNearHeight;
        ok=C3D_TexInitVRAM(&bloomTex_[i],height,width,GPU_RGBA8)&&(bloomTarget_[i]=C3D_RenderTargetCreateFromTex(&bloomTex_[i],GPU_TEXFACE_2D,0,-1))!=nullptr;
    }
    if(!ok){freeBloom();bloomFailed_=true;return false;}
    for(auto* t:{&sceneTex_,&bloomTex_[0],&bloomTex_[1],&bloomTex_[2],&bloomTex_[3]}){C3D_TexSetFilter(t,GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(t,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);}
    return true;
}
bool Renderer::initBloom(){
    static_assert(kBloomVertices>=6*(2*BloomFullQuads+2));
    if(!initBloomTargets())return false;
    if(bloomVertices_)return true;
    // The quad vertices are LINEAR: allocated with the first bloom request,
    // never by reserveBloom().
    if(!(bloomVertices_=static_cast<Vertex*>(linearAlloc(kBloomVertices*sizeof(Vertex))))){freeBloom();bloomFailed_=true;return false;}
    // Known, flushed contents: bloomQuad only rewrites slots that differ.
    std::memset(static_cast<void*>(bloomVertices_),0,kBloomVertices*sizeof(Vertex));flushGpuData(bloomVertices_,kBloomVertices*sizeof(Vertex));
    return true;
}
void Renderer::freeBloom(){
    for(auto*& t:bloomTarget_){if(t)C3D_RenderTargetDelete(t);t=nullptr;}
    if(sceneTarget_)C3D_RenderTargetDelete(sceneTarget_);
    sceneTarget_=nullptr;
    for(auto& t:bloomTex_){if(t.data)C3D_TexDelete(&t);t={};}
    if(sceneTex_.data)C3D_TexDelete(&sceneTex_);
    sceneTex_={};linearFree(bloomVertices_);bloomVertices_=nullptr;
}
void Renderer::bloomPass(C3D_RenderTarget* target,unsigned width,unsigned height){
    // Landscape width x height region of a portrait render target.
    C3D_FrameDrawOn(target);C3D_SetViewport(0,0,height,width);
    C3D_Mtx projection,identity;Mtx_OrthoTilt(&projection,0,float(width),float(height),0,-1,1,false);Mtx_Identity(&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&identity);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
}
void Renderer::bloomQuad(const C3D_Tex& source,unsigned sourceWidth,unsigned sourceHeight,float width,float height,float du,float dv,unsigned secondWidth,unsigned secondHeight,const C3D_Tex* secondSource,float secondDu,float secondDv){
    // A landscape point (X,Y) of a WxH region lies at window (H-Y, W-X) of its
    // portrait target; texture-backed targets store window (x,y) at texel (x,y).
    if(bloomCount_+6>kBloomVertices)return;
    Vertex* v=bloomVertices_+bloomCount_;
    const float sw=float(sourceWidth),sh=float(sourceHeight),tw=float(source.width),th=float(source.height);
    auto uv=[&](float x,float y,float* out){out[0]=(sh-y*sh/height-dv)/tw;out[1]=(sw-x*sw/width-du)/th;};
    const float corners[6][2]={{0,0},{width,0},{width,height},{0,0},{width,height},{0,height}};
    Vertex quad[6];
    for(int i=0;i<6;++i){
        quad[i]={{corners[i][0],corners[i][1],0},{0,0,1},{0,0},{0,0},{255,255,255,255}};
        uv(corners[i][0],corners[i][1],quad[i].uv);
        if(secondWidth){
            quad[i].uv1[0]=(float(secondHeight)-corners[i][1]*float(secondHeight)/height-secondDv)/float(secondSource?secondSource->width:source.width);
            quad[i].uv1[1]=(float(secondWidth)-corners[i][0]*float(secondWidth)/width-secondDu)/float(secondSource?secondSource->height:source.height);
        }
    }
    // Slots append across both eyes and remain unchanged until FrameEnd.
    // Reuse matching data from an earlier, completed frame without a flush.
    if(std::memcmp(v,quad,sizeof quad)){std::memcpy(v,quad,sizeof quad);flushGpuData(v,sizeof quad);}
    C3D_TexBind(0,const_cast<C3D_Tex*>(&source));
    auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,v,sizeof(Vertex),4,0x3210);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);bloomCount_+=6;stats_.triangles+=2;++stats_.drawCalls;
}
void Renderer::applyBloomNow(){
    flushSprites();bloomFrame_=false;
    const auto& settings=*bloomSettings_;
    auto pica=[](uint32_t rgba){return ((rgba>>24)&255)|((rgba>>8)&0xff00)|((rgba<<8)&0xff0000)|((rgba<<24)&0xff000000);};
    auto env=[](){C3D_TexEnvBufUpdate(C3D_Both,0);for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));return C3D_GetTexEnv(0);};
    auto blend=[](bool add){C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,add?GPU_ONE:GPU_ZERO,GPU_ONE,add?GPU_ONE:GPU_ZERO);};
    // Retail DrawPathBloom captures GX IA8 before thresholding (USA
    // 8021cdd0), then colours that intensity mask with the full scene.
    // GX copy intensity is .257 R + .504 G + .098 B + 16/255.
    // Reuse the near target before its blur pass; no extra texture allocation.
    bloomPass(bloomTarget_[1],BloomNearWidth,BloomNearHeight);
    auto* e0=env();
    const GPU_TEVOP_RGB channels[]={GPU_TEVOP_RGB_SRC_R,GPU_TEVOP_RGB_SRC_G,GPU_TEVOP_RGB_SRC_B};
    const unsigned coefficients[]={66,128,25}; // 219/255 luma range, plus 16/255 pedestal.
    for(unsigned i=0;i<3;++i){
        auto* e=C3D_GetTexEnv(i);C3D_TexEnvColor(e,coefficients[i]*0x01010101u);
        C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,GPU_PREVIOUS);
        C3D_TexEnvOpRgb(e,channels[i],GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvFunc(e,C3D_RGB,i?GPU_MULTIPLY_ADD:GPU_MODULATE);
        C3D_TexEnvSrc(e,C3D_Alpha,i?GPU_PREVIOUS:GPU_TEXTURE0);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
    }
    auto* e3=C3D_GetTexEnv(3);C3D_TexEnvColor(e3,0x10101010u);
    C3D_TexEnvSrc(e3,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e3,C3D_RGB,GPU_ADD);
    auto* e4=C3D_GetTexEnv(4);C3D_TexEnvColor(e4,0x40404040u);
    C3D_TexEnvSrc(e4,C3D_Both,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e4,C3D_Both,GPU_MODULATE);
    constexpr float taps[4][2]={{-1,-1},{1,-1},{-1,1},{1,1}};
    for(unsigned i=0;i<4;++i){blend(i>0);bloomQuad(sceneTex_,400,240,BloomNearWidth,BloomNearHeight,taps[i][0],taps[i][1]);}
    bloomPass(bloomTarget_[0],BloomNearWidth,BloomNearHeight);
    e0=env();C3D_TexEnvColor(e0,pica(bloomThresholdColor(settings)));
    C3D_TexEnvSrc(e0,C3D_Both,GPU_TEXTURE0,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_Both,GPU_SUBTRACT);
    auto* e1=C3D_GetTexEnv(1);
    C3D_TexEnvSrc(e1,C3D_Both,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(e1,C3D_Both,GPU_MODULATE);
    C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);C3D_TexEnvScale(e1,C3D_Both,GPU_TEVSCALE_2);
    auto* e2=C3D_GetTexEnv(2);C3D_TexEnvSrc(e2,C3D_Both,GPU_PREVIOUS,GPU_TEXTURE1);C3D_TexEnvFunc(e2,C3D_Both,GPU_MODULATE);
    C3D_TexBind(1,&sceneTex_);blend(false);
    bloomQuad(bloomTex_[1],BloomNearWidth,BloomNearHeight,BloomNearWidth,BloomNearHeight,0,0,400,240,&sceneTex_);
    C3D_TexBind(1,nullptr);
    // EGG keeps a near band (8 taps) and a far band (8 + 16 taps). Reduced
    // quality halves spatial samples, doubling their weights to keep energy.
    // Radii are original low-resolution texels, converted to the 3DS canvas.
    const unsigned reduction=renderQuality().bloomBlurRounds<2?2:1;
    auto ring=[&](unsigned source,unsigned sourceWidth,unsigned sourceHeight,unsigned dest,unsigned width,unsigned height,
                  unsigned band,unsigned stage,bool preserve){
        bloomPass(bloomTarget_[dest],width,height);
        auto* e=env();const unsigned coefficient=unsigned(bloomTapIntensity(settings,band,stage)*255.f+.5f);
        C3D_TexEnvColor(e,coefficient*0x01010101u);
        C3D_TexEnvSrc(e,C3D_Both,GPU_TEXTURE0,GPU_CONSTANT);C3D_TexEnvFunc(e,C3D_Both,GPU_MODULATE);
        if(reduction==2)C3D_TexEnvScale(e,C3D_Both,GPU_TEVSCALE_2);
        // Two independently offset samples per fragment use PICA's second
        // texture unit. Retain every ring tap/radius/weight; halve only draw
        // submissions and framebuffer blend traffic, not the blur kernel.
        if(reduction==1){
            auto* pair=C3D_GetTexEnv(1);C3D_TexEnvColor(pair,coefficient*0x01010101u);
            C3D_TexEnvSrc(pair,C3D_Both,GPU_TEXTURE1,GPU_CONSTANT,GPU_PREVIOUS);
            C3D_TexEnvFunc(pair,C3D_Both,GPU_MULTIPLY_ADD);
        }
        const unsigned nativeTaps=stage?16:8,count=nativeTaps/reduction;
        // Convert the authored radius to this source's texel dimensions.
        const float radiusX=bloomTapRadius(settings.strength[band],band,sourceWidth,608);
        const float radiusY=bloomTapRadius(settings.strength[band],band,sourceHeight,456);
        if(reduction==1)C3D_TexBind(1,&bloomTex_[source]);
        for(unsigned i=0;i<count;i+=reduction==1?2:1){
            const auto& tap=BloomRing[i*(16/count)];blend(preserve||i>0);
            if(reduction==1){
                const auto& next=BloomRing[(i+1)*(16/count)];
                bloomQuad(bloomTex_[source],sourceWidth,sourceHeight,float(width),float(height),
                    tap[0]*radiusX,tap[1]*radiusY,sourceWidth,sourceHeight,&bloomTex_[source],next[0]*radiusX,next[1]*radiusY);
            }else bloomQuad(bloomTex_[source],sourceWidth,sourceHeight,float(width),float(height),tap[0]*radiusX,tap[1]*radiusY);
        }
        C3D_TexBind(1,nullptr);
    };
    ring(0,BloomNearWidth,BloomNearHeight,1,BloomNearWidth,BloomNearHeight,0,0,false);
    // DrawPathBloom (USA 8021d3c4..8021d41c) captures a half-size copy
    // before the far rings. Sampling the near texture directly at displaced
    // coordinates omits that low-pass filter and retains sharp bright specks.
    // Keep the entire 64x32 source valid for hardware edge clamping.
    bloomPass(bloomTarget_[3],BloomFarWidth,BloomFarHeight);
    e0=env();C3D_TexEnvSrc(e0,C3D_Both,GPU_TEXTURE0);C3D_TexEnvFunc(e0,C3D_Both,GPU_REPLACE);
    blend(false);bloomQuad(bloomTex_[1],BloomNearWidth,BloomNearHeight,BloomFarWidth,BloomFarHeight,0,0);
    ring(3,BloomFarWidth,BloomFarHeight,2,BloomFarWidth,BloomFarHeight,1,0,false);
    ring(3,BloomFarWidth,BloomFarHeight,2,BloomFarWidth,BloomFarHeight,1,1,true);
    bloomPass(topTarget(),400,240);
    e0=env();C3D_TexEnvSrc(e0,C3D_Both,GPU_TEXTURE0);C3D_TexEnvFunc(e0,C3D_Both,GPU_REPLACE);
    blend(false);bloomQuad(sceneTex_,400,240,400,240,0,0);
    // Preserve the two authored composite weights and their alpha. Retail
    // output mode 1 uses screen blending: bloom * (1 - scene) + scene.
    // GX source factor INVSRCCLR aliases INVDSTCLR in this position; PICA
    // has distinct enums, so using ONE_MINUS_SRC_COLOR here is incorrect.
    e0=env();unsigned w0=unsigned(std::clamp(settings.weight[0],0.f,1.f)*255.f),w1=unsigned(std::clamp(settings.weight[1],0.f,1.f)*255.f);
    C3D_TexEnvColor(e0,w0*0x01010101u);C3D_TexEnvSrc(e0,C3D_Both,GPU_TEXTURE0,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_Both,GPU_MODULATE);
    C3D_TexEnvBufUpdate(C3D_Both,1);
    e1=C3D_GetTexEnv(1);C3D_TexEnvColor(e1,w1*0x01010101u);
    C3D_TexEnvSrc(e1,C3D_Both,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e1,C3D_Both,GPU_MODULATE);
    e2=C3D_GetTexEnv(2);C3D_TexEnvSrc(e2,C3D_Both,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER);C3D_TexEnvFunc(e2,C3D_Both,GPU_ADD);
    e3=C3D_GetTexEnv(3);C3D_TexEnvColor(e3,pica(bloomDisplayTint(settings)));
    C3D_TexEnvSrc(e3,C3D_Both,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e3,C3D_Both,GPU_MODULATE);
    C3D_TexBind(1,&bloomTex_[2]);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE_MINUS_DST_COLOR,GPU_ONE,GPU_ONE_MINUS_DST_ALPHA,GPU_ONE);
    bloomQuad(bloomTex_[1],BloomNearWidth,BloomNearHeight,400,240,0,0,BloomFarWidth,BloomFarHeight,&bloomTex_[2]);
    for(int unit=0;unit<3;++unit)C3D_TexBind(unit,nullptr);
    auto* env0=env();C3D_TexEnvSrc(env0,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env0,C3D_Both,GPU_MODULATE);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
    uploadProjection(projectionLoc_,projection_);
    spriteStateReady_=false;
}
void Renderer::endEyeNow(){
    if(stadiumFrame_)captureStadiumScreenNow();
    if(bloomFrame_)applyBloomNow();
    flushSprites();
    if(bottomPass_){
        // The bottom screen's quads were written in this pass (it runs once).
        if(spriteCount_)flushGpuData(spriteBase_,unsigned(spriteCount_)*6*sizeof(Vertex));
        if(topSpriteCount_&&!eye_)flushGpuData(sprites_,unsigned(topSpriteCount_)*6*sizeof(Vertex));
    }else if(spriteCount_&&!eye_)flushGpuData(sprites_,unsigned(spriteCount_)*6*sizeof(Vertex));
}
void Renderer::beginBottomNow(){
    if(!bottomTarget_||bottomPass_)return;
    // The top screen is complete: its bloom composite and last sprite run first.
    if(bloomFrame_)applyBloomNow();
    flushSprites();
    topSpriteCount_=spriteCount_;
    spriteBase_=text_+kTextVertexLimit;spriteCapacity_=kBottomSpriteCapacity;
    spriteCount_=0;spriteRunFirst_=0;spriteRunTexture_=nullptr;spriteStateReady_=false;
    bottomPass_=true;screenWidth_=320.f;screenParallax_=0;
    overlayCount_=0;menuIconDrawCount_=0;
    for(int unit=0;unit<3;++unit)C3D_TexBind(unit,nullptr);
    // No clear: the first bottom draw covers every pixel opaquely (a memory
    // fill here would split the command list without flushing it).
    C3D_FrameDrawOn(bottomTarget_);
}
void Renderer::endNow(){
    // The GPU reads batched sprite vertices only after FrameEnd submits them.
    endEyeNow();
    if(execLoadingFrame_){
        // Flush the real scene/capture work before clearing the LCD outputs.
        // Both eyes and the lower intro replay have finished at this point.
        C3D_FrameSplit(GX_CMDLIST_FLUSH);
        C3D_RenderTargetClear(target_,C3D_CLEAR_COLOR,0x000000ff,0);
        if(execStereo_.active())C3D_RenderTargetClear(targetRight_,C3D_CLEAR_COLOR,0x000000ff,0);
        if(bottomTarget_)C3D_RenderTargetClear(bottomTarget_,C3D_CLEAR_COLOR,0x000000ff,0);
    }
    // FrameEnd transfers each eye to these (the right half only in 3D mode).
    if(execStereo_.active())for(unsigned eye=0;eye<2;++eye)presented_[eye]=gfxGetFramebuffer(GFX_TOP,eye?GFX_RIGHT:GFX_LEFT,nullptr,nullptr);
    if(bottomPass_||bottomWorldPass_)presentedBottom_=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,nullptr,nullptr);
    stats_.commandUsage=C3D_GetCmdBufUsage();C3D_FrameEnd(GX_CMDLIST_FLUSH);
    if(stadiumPending_){stadiumRead_^=1;stadiumPending_=false;}
}
bool Renderer::captureTop(uint8_t* rgb,unsigned eye){
    if(!ready_||!rgb||(eye&&!stereoShown_))return false;
    drain();
    C3D_RenderTarget* target=eye?targetRight_:target_;
    u32* linear=static_cast<u32*>(linearAlloc(240*400*4));
    if(!linear)return false;
    // Same orientation as the screen transfer, untiled RGBA8 output. Outside a
    // frame this waits for the queued frame before the copy.
    C3D_SyncDisplayTransfer(static_cast<u32*>(target->frameBuf.colorBuf),GX_BUFFER_DIM(240,400),linear,GX_BUFFER_DIM(240,400),
        GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(linear,240*400*4);
    // Framebuffer column x holds 240 pixels from the bottom of the screen up.
    const uint8_t* bytes=reinterpret_cast<const uint8_t*>(linear);
    for(int y=0;y<240;++y)for(int x=0;x<400;++x){
        const uint8_t* p=bytes+4*(x*240+(239-y));
        uint8_t* o=rgb+3*(y*400+x);
        o[0]=p[3];o[1]=p[2];o[2]=p[1];
    }
    linearFree(linear);
    return true;
}
bool Renderer::copyPresentedBottom(uint8_t* bgr){
    if(!ready_||!bgr||!presentedBottom_||!bottomTarget_)return false;
    waitForGpu();
    constexpr unsigned bytes=320*240*3;
    GSPGPU_InvalidateDataCache(const_cast<uint8_t*>(presentedBottom_),bytes);
    std::memcpy(bgr,presentedBottom_,bytes);return true;
}
bool Renderer::captureBottom(uint8_t* rgb){
    if(!ready_||!rgb||!bottomTarget_)return false;
    drain();
    u32* linear=static_cast<u32*>(linearAlloc(240*320*4));
    if(!linear)return false;
    C3D_SyncDisplayTransfer(static_cast<u32*>(bottomTarget_->frameBuf.colorBuf),GX_BUFFER_DIM(240,320),linear,GX_BUFFER_DIM(240,320),
        GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(linear,240*320*4);
    const uint8_t* bytes=reinterpret_cast<const uint8_t*>(linear);
    for(int y=0;y<240;++y)for(int x=0;x<320;++x){
        const uint8_t* p=bytes+4*(x*240+(239-y));
        uint8_t* o=rgb+3*(y*320+x);
        o[0]=p[3];o[1]=p[2];o[2]=p[1];
    }
    linearFree(linear);
    return true;
}
bool Renderer::presentedMatches(const uint8_t* rgb,unsigned eye){
    if(!stereoShown_||eye>1||!presented_[eye]||!rgb)return false;
    drain();
    // BGR8 columns from the bottom of the screen up, as the transfer writes them.
    GSPGPU_InvalidateDataCache(const_cast<uint8_t*>(presented_[eye]),240*400*3);
    for(int y=0;y<240;++y)for(int x=0;x<400;++x){
        const uint8_t* p=presented_[eye]+3*(x*240+(239-y));const uint8_t* o=rgb+3*(y*400+x);
        if(o[0]!=p[2]||o[1]!=p[1]||o[2]!=p[0])return false;
    }
    return true;
}
}
