#include "mkw/renderer.hpp"
#include "mkw/render_quality.hpp"
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
// Translated combiner programs and texture sizes for the loaded RaceEffects table.
struct EffectDrawState {
    const RaceEffects* owner=nullptr;
    size_t resources=0;
    std::vector<ef::Material> materials;
    std::vector<ef::TextureSize> sizes;
    ef::DrawList list;
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
    gpu={};ready=false;
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
    if(gpu.maxLevel)C3D_TexSetFilterMipmap(&gpu,GPU_LINEAR);
    C3D_TexSetWrap(&gpu,GPU_REPEAT,GPU_REPEAT);
    ready=true; return true;
}
uint32_t Texture::bytes() const { return ready?C3D_TexCalcTotalSize(gpu.size,gpu.maxLevel):0; }
bool Texture::initMovieEtc1(std::uint16_t width, std::uint16_t height) {
    if (ready || width == 0 || height == 0 || !C3D_TexInit(&gpu, width, height, GPU_ETC1))
        return false;
    C3D_TexSetFilter(&gpu, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&gpu, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    ready = true;
    return true;
}
void Model::enableStaticCulling(){
    visibility.clear();visibility.reserve(submeshes.size());
    for(const auto& s:submeshes)visibility.push_back(buildVisibility(vertices,indices,s.first,s.count));
}
Model::~Model() { if(vertices)linearFree(vertices);if(indices)linearFree(indices);if(skinBoneIndices)linearFree(skinBoneIndices); }
bool Model::load(const char* path) {
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
        bool atEnd(){return f?std::fgetc(f)==EOF:offset==packed.size();}
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
        if(s.menuReflection>6||s.menuReflection==5||(s.menuReflection&&!(s.lighting&retail::Menu)))return false;
        if(s.menuReflection&&s.menuReflection!=3&&s.secondary<0)return false;
        for(float x:s.reflectionUv)if(!std::isfinite(x))return false;
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
    if(!f.atEnd())return false;
    file.reset();
    for(uint32_t i=0;i<indexCount;++i)if(indices[i]>=vertexCount)return false;
    minimum=maximum=vertices[0].position;
    for(uint32_t i=0;i<vertexCount;++i){auto p=vertices[i].position;if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return false;
        minimum={std::min(minimum.x,p.x),std::min(minimum.y,p.y),std::min(minimum.z,p.z)};
        maximum={std::max(maximum.x,p.x),std::max(maximum.y,p.y),std::max(maximum.z,p.z)};}
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
    {C3D_Mtx tilt;Mtx_PerspTilt(&tilt,C3D_AngleFromDegrees(90.f),1.f,1.f,2.f,false);horizontalSign_=tilt.r[1].x<0?-1.f:1.f;}
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
    overlay_=static_cast<Vertex*>(linearAlloc(kOverlayCapacity*sizeof(Vertex)));if(!overlay_)return fail();
    sprites_=static_cast<Vertex*>(linearAlloc(kSpriteCapacity*6*sizeof(Vertex)));if(!sprites_)return fail();
    text_=static_cast<Vertex*>(linearAlloc(kTextVertices*sizeof(Vertex)));if(!text_)return fail();
    particles_=static_cast<Vertex*>(linearAlloc(DriftEffects::Capacity*12*sizeof(Vertex)));if(!particles_)return fail();
    effectVertices_=linearAlloc(kEffectVertices*sizeof(ef::DrawVertex));if(!effectVertices_)return fail();
    if(shader_->numDVLE<2)return fail();
    shaderProgramInit(&effectProgram_);effectProgramReady_=true;
    if(R_FAILED(shaderProgramSetVsh(&effectProgram_,&shader_->DVLE[1])))return fail();
    effectProjectionLoc_=shaderInstanceGetUniformLocation(effectProgram_.vertexShader,"projection");
    if(effectProjectionLoc_<0)return fail();
    if(!C3D_TexInit(&effectLookup_,ef::DrawList::LookupSize,ef::DrawList::LookupSize,GPU_RGBA8))return fail();
    C3D_TexSetFilter(&effectLookup_,GPU_NEAREST,GPU_NEAREST);C3D_TexSetWrap(&effectLookup_,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    effectDraw_=new EffectDrawState;
    std::memset(white_.data,255,8*8*4);C3D_TexFlush(&white_);
    Mtx_PerspTilt(&projection_,C3D_AngleFromDegrees(55.f),C3D_AspectRatioTop,10.f,400000.f,false);
    mainProjection_=cullProjection_=projection_;
    ready_=true;return true;
}
bool Renderer::enableBottom(){
    if(bottomTarget_)return true;
    if(!deviceReady_)return false;
    // citro3d's linked-target table is read by FrameEnd on the render thread.
    drain();
    // 2D only: no depth buffer (a depth-less target is never cleared or tested
    // for depth, as the bloom targets). Display transfer as the top screen's.
    bottomTarget_=C3D_RenderTargetCreate(240,320,GPU_RB_RGBA8,C3D_DEPTHTYPE(-1));
    if(!bottomTarget_)return false;
    C3D_RenderTargetSetOutput(bottomTarget_,GFX_BOTTOM,GFX_LEFT,TopTransfer);
    return true;
}
void Renderer::releaseBottom(){
    if(!bottomTarget_)return;
    // No queued frame or transfer still reads the target (or the framebuffers).
    waitForGpu();
    C3D_RenderTargetDelete(bottomTarget_);bottomTarget_=nullptr;
    // The lower LCD shows its last transferred frame until something replaces
    // it: menus never draw there, so blank both of its buffers (BGR8).
    for(unsigned i=0;i<2;++i){
        u16 w=0,h=0;u8* framebuffer=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);
        if(framebuffer){std::memset(framebuffer,0,size_t(w)*h*3);GSPGPU_FlushDataCache(framebuffer,u32(w)*h*3);}
        gfxScreenSwapBuffers(GFX_BOTTOM,false);
    }
}
bool Renderer::reserveBloom(){
    if(!deviceReady_||!renderQuality().bloom)return false;
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
    ok=ok&&!(header[2]&0x40)&&header[4]==0&&width<=1024&&height<=1024&&(format==GPU_ETC1||format==GPU_RGBA8)&&
        bytes==C3D_TexCalcTotalSize(format==GPU_ETC1?width*height/2:width*height*4,0);
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
    if(!texture.ready||!t.data||(at>=OS_VRAM_VADDR&&at<OS_VRAM_VADDR+OS_VRAM_SIZE)||t.maxLevel)return false;
    const std::size_t bytes=C3D_TexCalcTotalSize(t.size,0);
    if(vramSpaceFree()<bytes+reserve)return false;
    C3D_Tex moved{};
    if(!C3D_TexInitVRAM(&moved,t.width,t.height,t.fmt))return false;
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
    if(bottomTarget_)C3D_RenderTargetDelete(bottomTarget_);
    bottomTarget_=nullptr;
    freeBloom();
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
    view_=view;projection_=eyeProjection(projection);
    beginFrame();
}
// The right eye replays the frame's commands after the left eye's.
void Renderer::beginEyeNow(const C3D_Mtx& view,const C3D_Mtx& projection){
    eye_=1;
    view_=view;projection_=eyeProjection(projection);
    beginFrame();
}
C3D_Mtx Renderer::eyeProjection(const C3D_Mtx& center) const{
    if(!execStereo_.active())return center;
    float m[4][4];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)m[r][c]=center.r[r].c[3-c];
    stereo::perspectiveEye(m,1,horizontalSign_,eye_?1.f:-1.f,execStereo_);
    C3D_Mtx out;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)out.r[r].c[3-c]=m[r][c];
    return out;
}
// Top-screen pixels (origin top-left). A stereo layer moves each eye by half its disparity.
C3D_Mtx Renderer::screenProjection(float parallax) const{
    C3D_Mtx out;Mtx_OrthoTilt(&out,0,screenWidth_,240,0,-1,1,false);
    if(!execStereo_.active()||parallax==0)return out;
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
    // The right eye repeats the left eye's buffer offsets: its sprites, text,
    // overlays, sparks and effect vertices reuse the data the left eye wrote.
    screenWidth_=400.f;screenParallax_=0;
    spriteBase_=sprites_;spriteCapacity_=kSpriteCapacity;topSpriteCount_=0;bottomPass_=false;
    spriteCount_=0;spriteRunFirst_=0;spriteRunTexture_=nullptr;spriteStateReady_=false;
    menuIconDrawCount_=0;
    textCount_=0;
    overlayCount_=0;
    // The local SDK permits unbinding all units, including inherited samplers.
    for(int unit=0;unit<3;++unit)C3D_TexBind(unit,nullptr);
    if(!eye_){
        C3D_RenderTargetClear(target_,C3D_CLEAR_ALL,0x071326FF,0);
        if(execStereo_.active())C3D_RenderTargetClear(targetRight_,C3D_CLEAR_ALL,0x071326FF,0);
    }
    bloomFrame_=bloomRequested_&&initBloom();bloomRequested_=false;bloomCount_=0;
    if(bloomFrame_){
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
    }else C3D_FrameDrawOn(topTarget());
    C3D_BindProgram(&program_);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection_);
    // Environment rows stay finite; mix zero keeps every vertex's own UV1.
    C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[0],1,0,0,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[1],0,1,0,0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[2],0,0,1,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,1,0,0,0);
    uvDirty_=tintDirty_=true;resetMaterialUniforms();
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[0],1,0,0,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[1],0,1,0,0);
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
    if(!eye_)stats_.triangles=stats_.drawCalls=stats_.culledTriangles=stats_.culledSubmeshes=0;
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
void Renderer::drawAtScreenNow(const Model& model,const Vertex* vertices,const C3D_Mtx& transform,float x,float y,float brightness,float scaleX,float scaleY){
    flushSprites();
    const C3D_Mtx original=projection_;
    C3D_Mtx tilt;Mtx_PerspTilt(&tilt,C3D_AngleFromDegrees(90.f),1.f,1.f,2.f,false);
    const float vertical=(tilt.r[0].y<0?-1.f:1.f)*(120.f-y)/120.f;
    const float horizontal=(tilt.r[1].x<0?-1.f:1.f)*(x-200.f)/200.f;
    for(unsigned col=0;col<4;++col){
        projection_.r[0].c[col]=scaleY*original.r[0].c[col]+vertical*original.r[3].c[col];
        projection_.r[1].c[col]=scaleX*original.r[1].c[col]+horizontal*original.r[3].c[col];
    }
    const float originalBrightness=modelBrightness_;modelBrightness_=brightness;
    drawNow(model,vertices,transform,false,nullptr,nullptr,nullptr);
    modelBrightness_=originalBrightness;
    projection_=original;
}
void Renderer::drawMenuIconNow(const Model& model,const Vertex* vertices,const C3D_Mtx& transform,const UiQuad& q){
    if(q.bottom||q.alpha<=0||q.w<=0||q.h<=0||q.u1<=q.u0||q.v1<=q.v0)return;
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
    for(unsigned col=0;col<4;++col){
        projection_.r[0].c[col]*=(480.f/44.f)*(height/240.f);
        projection_.r[1].c[col]*=(608.f/192.f)*(width/400.f);
        // Captured-image panes composite in draw order, including the offset
        // shadow and OK glow of the same vehicle. Give each its own near-depth
        // interval while preserving all relative depth inside this machine.
        projection_.r[2].c[col]=.01f*projection_.r[2].c[col]-layer*projection_.r[3].c[col];
    }
    // A captured image in its pane: the pane's (screen layer) disparity, not the model's depth.
    if(execStereo_.active()&&screenParallax_!=0){
        float m[4][4];
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)m[r][c]=projection_.r[r].c[3-c];
        stereo::screenEye(m,1,horizontalSign_,eye_?1.f:-1.f,screenParallax_);
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)projection_.r[r].c[3-c]=m[r][c];
    }
    // The pane's crop moves with the eye's image of the pane.
    const float x=q.x+(execStereo_.active()?(eye_?.5f:-.5f)*screenParallax_:0.f);
    const unsigned l=unsigned(std::clamp(std::floor(240.f-q.y-q.h),0.f,240.f));
    const unsigned r=unsigned(std::clamp(std::ceil(240.f-q.y),0.f,240.f));
    const unsigned t=unsigned(std::clamp(std::floor(400.f-x-q.w),0.f,400.f));
    const unsigned b=unsigned(std::clamp(std::ceil(400.f-x),0.f,400.f));
    if(l<r&&t<b){
        C3D_SetScissor(GPU_SCISSOR_NORMAL,l,t,r,b);
        courseLighting_=false;menuIconLayer_=true;menuIconAlpha_=q.alpha;
        menuIconAdditive_=q.additive;menuIconColor0_=q.vehicleColor0;menuIconColor1_=q.vehicleColor1;
        drawAtScreenNow(model,vertices,transform,centerX,centerY,1.f,1.f,1.f);
        menuIconLayer_=false;menuIconAlpha_=1;menuIconAdditive_=false;
        C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
    }
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
    flushSprites();
    const uint32_t animationFrame=execFrame_,additiveColor=execAdditive_;
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection_);
    C3D_Mtx mv;Mtx_Multiply(&mv,&view_,&transform);C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&mv);
    auto* attr=C3D_GetAttrInfo();AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_FLOAT,3);AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);AttrInfo_AddLoader(attr,3,GPU_UNSIGNED_BYTE,4);
    auto* buf=C3D_GetBufInfo();BufInfo_Init(buf);BufInfo_Add(buf,vertices,sizeof(Vertex),4,0x3210);
    const bool litSkin=skinPalette_&&model.skinBoneIndices;
    if(litSkin){AttrInfo_AddLoader(attr,4,GPU_FLOAT,1);BufInfo_Add(buf,model.skinBoneIndices,sizeof(float),1,4);}
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedNormalsLoc_,litSkin);
    if(skinPalette_){
        for(unsigned b=0;b<skinBones_;++b){const float* m=skinPalette_+b*skinStride_;
            for(unsigned r=0;r<3;++r)C3D_FVUnifSet(GPU_VERTEX_SHADER,bonesLoc_+int(b*3+r),m[r*4],m[r*4+1],m[r*4+2],m[r*4+3]);}
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedLoc_,true);
    }
    // One set of object-space planes per static model, shared by all ranges.
    const bool cull=!backdrop&&!model.visibility.empty()&&model.visibility.size()==model.submeshes.size();
    // Rigid parts (one bone each: a stand of Mii spectators spread over the
    // whole course is one model) are tested one by one against the same planes.
    const bool partCull=!backdrop&&rigid_&&rigid_->parts;
    ClipFrustum frustum;
    // Reduced quality: course-object parts under minScreenRatio from the eye
    // (model space). Menu models keep every part.
    float partTiny=0;Vec3 eye{};
    if(cull||partCull){
        C3D_Mtx clip;Mtx_Multiply(&clip,&projection_,&mv);float rows[16];
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=clip.r[r].c[3-c];
        frustum=ClipFrustum(rows);
        if(partCull&&courseLighting_&&(partTiny=renderQuality().minScreenRatio)>0){
            C3D_Mtx inverse=mv;
            if(Mtx_Inverse(&inverse)!=0.f)eye={inverse.r[0].w,inverse.r[1].w,inverse.r[2].w};else partTiny=0;
        }
    }
    // The last drawn submesh whose material state is still bound, and its end
    // of submesh (retailEndSubmesh), deferred while the next one repeats it.
    const Submesh* last=nullptr;bool lastRetail=false,lastEnvironment=false,pendingEnd=false;
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
        if(cull){
            ranges=selectVisible(model.visibility[i],frustum);unsigned submitted=0;
            for(unsigned n=0;n<ranges.count;++n)submitted+=ranges.ranges[n].count;
            stats_.culledTriangles+=(s.count-submitted)/3;
            if(!ranges.count){++stats_.culledSubmeshes;continue;}
        }else ranges.append({s.first,s.count});
        const SubmeshOverride* o=overrides_&&i<overrideCount_&&overrides_[i].flags?&overrides_[i]:nullptr;
        if(o&&(o->flags&SubmeshOverride::Uv)){
            // An SRT0 track replaces the authored constant-rate scroll.
            for(unsigned row=0;row<4;++row)C3D_FVUnifSet(GPU_VERTEX_SHADER,uvRowLoc_[row],o->uv[row*3],o->uv[row*3+1],o->uv[row*3+2],0);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);uvDirty_=true;
        }else{
            if(uvDirty_)resetMaterialUniforms();
            const uint32_t frames=s.animationFrames&0xffff;
            const float frame=frames?float(animationFrame%frames):0.f;
            C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,s.uvVelocity[0]*frame,s.uvVelocity[1]*frame,s.uvVelocity[2]*frame,s.uvVelocity[3]*frame);
        }
        if(o&&(o->flags&SubmeshOverride::Tint)){C3D_FVUnifSet(GPU_VERTEX_SHADER,tintLoc_,o->tint[0],o->tint[1],o->tint[2],o->tint[3]);tintDirty_=true;}
        else if(tintDirty_)resetMaterialUniforms();
        // The previous drawn submesh's material and textures, without overrides
        // or a binder: its whole state is still bound, only UVs and draws differ.
        const bool repeat=last&&!o&&!material&&sameMaterial(*last,s);
        bool retail=lastRetail,retailEnvironment=lastEnvironment;
        if(!repeat){
        if(pendingEnd){retailEndSubmesh();pendingEnd=false;}
        const uint32_t constant=o&&(o->flags&SubmeshOverride::Constant)?o->constant:s.constant;
        const GPU_TEVSCALE scales[]={GPU_TEVSCALE_1,GPU_TEVSCALE_2,GPU_TEVSCALE_4,GPU_TEVSCALE_1};
        if(!material){
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
            if(s.menuReflection){
                // GX: clamp(albedo*rasC + C1), then + reflection*APREV
                // (APREV = albedo alpha * authored konst) or * konst RGB.
                // Keep the intermediate alpha until stage 3 applies the
                // source's final alpha; it is a reflection mask, not opacity.
                C3D_TexEnvColor(e0,s.reflectionAdd);
                C3D_TexEnvSrc(e0,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_CONSTANT);
                C3D_TexEnvFunc(e0,C3D_RGB,GPU_MULTIPLY_ADD);
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
                }
            }
            // Alpha is a separate GX equation: lighting's secondary texture
            // must not replace the road decal's actual alpha mask.
            const unsigned alphaMode=(s.flags>>20)&7;
            auto* alpha=C3D_GetTexEnv(3);
            if(alphaMode==1||alphaMode==2){
                C3D_TexEnvSrc(alpha,C3D_Alpha,alphaMode==1?GPU_TEXTURE0:GPU_TEXTURE1,GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_MODULATE);
            }else if(alphaMode==3||alphaMode==6){
                // Mode 6 doubles the texture product (GX alpha scale 2).
                C3D_TexEnvSrc(alpha,C3D_Alpha,GPU_TEXTURE0,GPU_TEXTURE1);C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_MODULATE);
                if(alphaMode==6)C3D_TexEnvScale(alpha,C3D_Alpha,GPU_TEVSCALE_2);
                auto* vertexAlpha=C3D_GetTexEnv(4);C3D_TexEnvSrc(vertexAlpha,C3D_Alpha,GPU_PREVIOUS,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(vertexAlpha,C3D_Alpha,GPU_MODULATE);
            }else if(alphaMode==4&&o&&(o->flags&SubmeshOverride::Alpha)){
                // A CLR0-animated register is the whole alpha (the cooked vertex alpha is its static 0).
                C3D_TexEnvColor(alpha,uint32_t(std::lround(std::clamp(o->tint[3],0.f,1.f)*255.f))<<24);
                C3D_TexEnvSrc(alpha,C3D_Alpha,GPU_CONSTANT);C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_REPLACE);
            }else if(alphaMode==4){
                const bool menu=s.lighting&mkw::retail::Menu;
                if(menu)C3D_TexEnvColor(alpha,constant);
                C3D_TexEnvSrc(alpha,C3D_Alpha,menu?GPU_CONSTANT:GPU_PRIMARY_COLOR);C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_REPLACE);
            }else if(alphaMode==5){
                C3D_TexEnvColor(alpha,constant);
                C3D_TexEnvSrc(alpha,C3D_Alpha,GPU_TEXTURE0,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(alpha,C3D_Alpha,GPU_INTERPOLATE);
            }
        }
        if(additiveColor||(o&&(o->flags&SubmeshOverride::Add))){
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
        C3D_CullFace((s.flags&4)||backdrop?GPU_CULL_NONE:GPU_CULL_FRONT_CCW);
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
        const bool swap=o&&(o->flags&SubmeshOverride::Texture);
        const int32_t primaryIndex=swap&&o->texture[0]>=0&&size_t(o->texture[0])<model.textures.size()?o->texture[0]:s.texture;
        const int32_t secondaryIndex=swap&&o->texture[1]>=0&&size_t(o->texture[1])<model.textures.size()?o->texture[1]:s.secondary;
        auto* texture=primaryIndex>=0?&model.textures[primaryIndex]->gpu:&white_;
        const GPU_TEXTURE_WRAP_PARAM wraps[]={GPU_CLAMP_TO_EDGE,GPU_REPEAT,GPU_MIRRORED_REPEAT,GPU_CLAMP_TO_EDGE};
        C3D_TexSetWrap(texture,wraps[(s.flags>>8)&3],wraps[(s.flags>>10)&3]);
        C3D_TexBind(0,texture);
        auto* secondary=secondaryIndex>=0?&model.textures[secondaryIndex]->gpu:&white_;
        C3D_TexSetWrap(secondary,wraps[(s.flags>>16)&3],wraps[(s.flags>>18)&3]);
        C3D_TexBind(1,secondary);
        // Retail course lighting: fog stage, light textures (after the generic setup).
        retailEnvironment=retail&&retailCombiners(s,mv,backdrop,additiveColor||(o&&(o->flags&SubmeshOverride::Add)),
            additiveColor?(additiveColor>>24)|((additiveColor>>8)&0xff00)|((additiveColor<<8)&0xff0000):o&&(o->flags&SubmeshOverride::Add)?o->add&0xffffff:0);
        // A material binder owns the combiners and may replace any state above.
        if(material)material(context,i);
        last=o?nullptr:&s;lastRetail=retail;lastEnvironment=retailEnvironment;
        } // !repeat
        const bool menuEnvironment=s.menuReflection&&s.menuReflection!=3;
        if(menuEnvironment)environmentUvNow(transform,s.reflectionUv);
        if(parts){
            const bool environment=(s.flags&(1u<<25))&&((s.flags>>12)&3)==3&&rigid_->stride>=21;
            for(size_t n=0;n<parts->size();++n){
                if(n<64&&!((shownParts>>n)&1))continue;
                const auto& part=(*parts)[n];
                const float* m=rigid_->palette+part.matrix*rigid_->stride;
                C3D_Mtx bone;Mtx_Identity(&bone);
                for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)bone.r[row].c[3-col]=m[row*4+col];
                C3D_Mtx local,partView;Mtx_Multiply(&local,&transform,&bone);Mtx_Multiply(&partView,&view_,&local);
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,modelViewLoc_,&partView);
                if(retailEnvironment)this->retailEnvironment(partView);
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
            pendingEnd=retail;
            continue;
        }
        for(unsigned n=0;n<ranges.count;++n){
            const auto range=ranges.ranges[n];
            C3D_DrawElements(GPU_TRIANGLES,range.count,C3D_UNSIGNED_SHORT,model.indices+range.first);
            stats_.triangles+=range.count/3;++stats_.drawCalls;
        }
        if(menuEnvironment)clearEnvironmentUvNow();
        pendingEnd=retail;
    }
    if(pendingEnd)retailEndSubmesh();
    // Later draws (sprites, sparks, other models) expect identity UV rows/tint.
    resetMaterialUniforms();
    if(skinPalette_)C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedLoc_,false);
    if(litSkin){
        AttrInfo_Init(attr);AttrInfo_AddLoader(attr,0,GPU_FLOAT,3);AttrInfo_AddLoader(attr,1,GPU_FLOAT,3);AttrInfo_AddLoader(attr,2,GPU_FLOAT,4);AttrInfo_AddLoader(attr,3,GPU_UNSIGNED_BYTE,4);
        C3D_BoolUnifSet(GPU_VERTEX_SHADER,skinnedNormalsLoc_,false);
    }
}
void Renderer::shadowVolumesNow(const ShadowVolume* volumes,unsigned count,float darkness){
    flushSprites();
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection_);
    resetMaterialUniforms();
    for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    const uint32_t shade=uint32_t(std::round(std::clamp(darkness,0.f,1.f)*255.f));
    auto* env=C3D_GetTexEnv(0);C3D_TexEnvColor(env,0xff000000u|shade|(shade<<8)|(shade<<16));
    C3D_TexEnvSrc(env,C3D_Both,GPU_CONSTANT);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc_,1,1,1,1);C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,0,0,0,0);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    auto drawAll=[&](GPU_CULLMODE cull){
        C3D_CullFace(cull);
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
    C3D_StencilTest(true,GPU_ALWAYS,0,0xff,0xff);
    // Count occluded faces (z-fail). Original closed shadow meshes can
    // intersect the near plane, so z-pass leaves an unbalanced silhouette.
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_DECR_WRAP,GPU_STENCIL_KEEP);
    drawAll(GPU_CULL_FRONT_CCW);
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_INCR_WRAP,GPU_STENCIL_KEEP);
    drawAll(GPU_CULL_BACK_CCW);
    // Shade each marked pixel once and clear its count for later volumes.
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_StencilTest(true,GPU_NOTEQUAL,0,0xff,0xff);
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_ZERO);
    // A screen quad also shades/clears the stencil when the camera is
    // inside a volume. Drawing a volume face here depends on near clipping
    // again, and can leave stray counts for later objects/particles.
    screenFadeNow(1.f-float(shade)/255.f,false);
    // The fullscreen shade temporarily binds an orthographic projection.
    // Consecutive volume groups must return to this eye's perspective.
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection_);
    C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0);
    C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
    C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
}
void Renderer::environmentUvNow(const C3D_Mtx& transform,const float (&uv)[6]){
    // GX EnvCamera: normalize(view-space normal) -> (0.5+0.5x, 0.5-0.5y) in
    // GX texture space, then the material SRT. The shader yields that point in
    // this renderer's V-up convention (v=1-t); convert the SRT rows to match.
    C3D_Mtx mv;Mtx_Multiply(&mv,&view_,&transform);
    for(unsigned row=0;row<3;++row)C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[row],mv.r[row].x,mv.r[row].y,mv.r[row].z,0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,1,0,1,0);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,true);
    uv1TransformNow(uv);
}
void Renderer::uv1TransformNow(const float (&uv)[6]){
    // GX rows s'=a*s+b*t+tx, t'=c*s+d*t+ty with t=1-v, written back as v'=1-t'.
    const float a=uv[0],b=uv[1],tx=uv[2],c=uv[3],d=uv[4],ty=uv[5];
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[0],a,-b,0,b+tx);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[1],-c,d,0,1-d-ty);
}
void Renderer::clearEnvironmentUvNow(){
    C3D_BoolUnifSet(GPU_VERTEX_SHADER,environmentMappedLoc_,false);
    for(unsigned row=0;row<3;++row)C3D_FVUnifSet(GPU_VERTEX_SHADER,envRowLoc_[row],row==0,row==1,row==2,0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,envParamLoc_,1,0,0,0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[0],1,0,0,0);C3D_FVUnifSet(GPU_VERTEX_SHADER,uv1RowLoc_[1],0,1,0,0);
}
void Renderer::drawSparksNow(const SparkSpec* specs,unsigned specCount,const Spark* sparks,unsigned sparkCount,const Texture* textures){
    flushSprites();
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
    for(unsigned pass=eye_?1:0;pass<2;++pass){offset=0;
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
void Renderer::updateEffectState(RaceEffects& effects){
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
    ef::buildDrawList(effects.system(),state.materials,state.sizes.data(),uint32_t(state.sizes.size()),view,list,kEffectVertices);
}
// The immediate path; recorded frames build the list in drawEffects (render_thread.cpp).
void Renderer::drawEffectsImmediate(RaceEffects& effects,const Texture* textures){
    updateEffectState(effects);
    buildEffectList(effects,view_,effectDraw_->list);
    effectListNow(effects,effectDraw_->list,textures);
}
void Renderer::effectListNow(const RaceEffects& effects,const ef::DrawList& list,const Texture* textures){
    EffectDrawState& state=*effectDraw_;
    // Submit only complete triangles within the existing 8192-vertex allocation.
    const size_t vertexCount=ef::triangleVertexCount(list.vertices.size(),kEffectVertices);
    if(!vertexCount)return;
    flushSprites();
    // Both eyes draw the list built from the centre camera; the left eye uploads it.
    if(!eye_){
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
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,effectProjectionLoc_,&projection_);
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
        C3D_CullFace(b.cull==1?GPU_CULL_BACK_CCW:b.cull==2?GPU_CULL_FRONT_CCW:GPU_CULL_NONE);
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
    Vertex* vertices=overlay_+overlayCount_;
    overlayCount_+=count;
    return vertices;
}
void Renderer::cinemaBarsNow(float topHeight,float bottomHeight,float opacity){
    flushSprites();
    Vertex* vertices=reserveOverlay(12);
    const auto alpha=static_cast<uint8_t>(std::round(std::clamp(opacity,0.f,1.f)*255.f));
    auto rectangle=[&](int first,float top,float bottom){
        const Vec3 positions[6]={{0,top,0},{400,top,0},{400,bottom,0},
                                 {0,top,0},{400,bottom,0},{0,bottom,0}};
        for(int i=0;i<6;++i)vertices[first+i]={positions[i],{0,0,1},{0,0},{0,0},{0,0,0,alpha}};
    };
    if(!eye_){
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
void Renderer::screenFadeNow(float opacity,bool white){
    flushSprites();
    const auto alpha=static_cast<uint8_t>(std::round(std::clamp(opacity,0.f,1.f)*255.f));
    const uint8_t value=white?255:0;
    const Vec3 positions[6]={{0,0,0},{400,0,0},{400,240,0},
                             {0,0,0},{400,240,0},{0,240,0}};
    Vertex* fade=reserveOverlay(6);
    if(!eye_){
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
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
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
void Renderer::materialQuadsNow(const Texture* texture,const MaterialQuad* quads,unsigned count,uint32_t color0,uint32_t color1,int wrapS,int wrapT){
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
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
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
            if(eye_){textCount_+=6;continue;}
            float px=x+col*size,py=y+row*size;
            const Vec3 positions[]={{px,py,0},{px+size,py,0},{px+size,py+size,0},{px,py,0},{px+size,py+size,0},{px,py+size,0}};
            for(auto p:positions)text_[textCount_++]={p,{0,0,1},{0,0},{0,0},{uint8_t(rgba>>24),uint8_t(rgba>>16),uint8_t(rgba>>8),uint8_t(rgba)}};
        }
    }
    if(first==textCount_)return;
    Vertex* vertices=text_+first;unsigned count=textCount_-first;if(!eye_)flushGpuData(vertices,count*sizeof(Vertex));
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
void Renderer::requestBloomNow(const CourseBloom& settings){
    bloomRequested_=renderQuality().bloom&&settings.enabled();bloomSettings_=&settings;
}
bool Renderer::initBloomTargets(){
    if(sceneTarget_)return true;
    if(bloomFailed_)return false;
    // 240x400 scene in a 256x512 VRAM texture; 60x100 bright/blur buffers.
    bool ok=C3D_TexInitVRAM(&sceneTex_,256,512,GPU_RGBA8)&&
        (sceneTarget_=C3D_RenderTargetCreateFromTex(&sceneTex_,GPU_TEXFACE_2D,0,GPU_RB_DEPTH24_STENCIL8))!=nullptr;
    for(unsigned i=0;ok&&i<3;++i)
        ok=C3D_TexInitVRAM(&bloomTex_[i],64,128,GPU_RGBA8)&&(bloomTarget_[i]=C3D_RenderTargetCreateFromTex(&bloomTex_[i],GPU_TEXFACE_2D,0,-1))!=nullptr;
    if(!ok){freeBloom();bloomFailed_=true;return false;}
    for(auto* t:{&sceneTex_,&bloomTex_[0],&bloomTex_[1],&bloomTex_[2]}){C3D_TexSetFilter(t,GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(t,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);}
    return true;
}
bool Renderer::initBloom(){
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
void Renderer::bloomQuad(const C3D_Tex& source,unsigned sourceWidth,unsigned sourceHeight,float width,float height,float du,float dv,unsigned secondWidth,unsigned secondHeight){
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
            quad[i].uv1[0]=(float(secondHeight)-corners[i][1]*float(secondHeight)/height)/tw;
            quad[i].uv1[1]=(float(secondWidth)-corners[i][0]*float(secondWidth)/width)/th;
        }
    }
    // Each frame repeats the same quads in the same slots: rewrite (and flush)
    // only a slot whose vertices differ. The GPU finished the last frame.
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
    // USA EGG threshold filter: subtract the threshold constant, double with
    // scene alpha, then modulate by the original scene. Tint belongs to the
    // final composite, so coloured light can spread before being tinted.
    bloomPass(bloomTarget_[0],100,60);
    auto* e0=env();C3D_TexEnvColor(e0,pica(bloomThresholdColor(settings)));
    C3D_TexEnvSrc(e0,C3D_Both,GPU_TEXTURE0,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_Both,GPU_SUBTRACT);
    auto* e1=C3D_GetTexEnv(1);
    C3D_TexEnvSrc(e1,C3D_Both,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(e1,C3D_Both,GPU_MODULATE);
    C3D_TexEnvOpRgb(e1,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);C3D_TexEnvScale(e1,C3D_Both,GPU_TEVSCALE_2);
    auto* e2=C3D_GetTexEnv(2);C3D_TexEnvSrc(e2,C3D_Both,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(e2,C3D_Both,GPU_MODULATE);
    auto* e3=C3D_GetTexEnv(3);C3D_TexEnvColor(e3,0x40404040u);
    C3D_TexEnvSrc(e3,C3D_Both,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e3,C3D_Both,GPU_MODULATE);
    constexpr float taps[4][2]={{-1,-1},{1,-1},{-1,1},{1,1}};
    for(unsigned i=0;i<4;++i){blend(i>0);bloomQuad(sceneTex_,400,240,100,60,taps[i][0],taps[i][1]);}
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
        const unsigned nativeTaps=stage?16:8,count=nativeTaps/reduction;
        // The far source is downsampled by two; sampling its parent texture
        // directly with bilinear filtering saves an extra copy and target.
        const float radius=settings.strength[band]*(band?2.f:1.f);
        for(unsigned i=0;i<count;++i){
            const auto& tap=BloomRing[i*(16/count)];blend(preserve||i>0);
            bloomQuad(bloomTex_[source],sourceWidth,sourceHeight,float(width),float(height),
                tap[0]*radius*(400.f/608.f),tap[1]*radius*(240.f/456.f));
        }
    };
    ring(0,100,60,1,100,60,0,0,false);
    ring(1,100,60,2,50,30,1,0,false);
    ring(1,100,60,2,50,30,1,1,true);
    bloomPass(topTarget(),400,240);
    e0=env();C3D_TexEnvSrc(e0,C3D_Both,GPU_TEXTURE0);C3D_TexEnvFunc(e0,C3D_Both,GPU_REPLACE);
    blend(false);bloomQuad(sceneTex_,400,240,400,240,0,0);
    // Preserve the two authored composite weights and their alpha. Retail
    // output mode 1 selects blend preset 5: inverse-source-colour / one.
    e0=env();unsigned w0=unsigned(std::clamp(settings.weight[0],0.f,1.f)*255.f),w1=unsigned(std::clamp(settings.weight[1],0.f,1.f)*255.f);
    C3D_TexEnvColor(e0,w0*0x01010101u);C3D_TexEnvSrc(e0,C3D_Both,GPU_TEXTURE0,GPU_CONSTANT);C3D_TexEnvFunc(e0,C3D_Both,GPU_MODULATE);
    C3D_TexEnvBufUpdate(C3D_Both,1);
    e1=C3D_GetTexEnv(1);C3D_TexEnvColor(e1,w1*0x01010101u);
    C3D_TexEnvSrc(e1,C3D_Both,GPU_TEXTURE1,GPU_CONSTANT);C3D_TexEnvFunc(e1,C3D_Both,GPU_MODULATE);
    e2=C3D_GetTexEnv(2);C3D_TexEnvSrc(e2,C3D_Both,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER);C3D_TexEnvFunc(e2,C3D_Both,GPU_ADD);
    e3=C3D_GetTexEnv(3);C3D_TexEnvColor(e3,pica(bloomDisplayTint(settings)));
    C3D_TexEnvSrc(e3,C3D_Both,GPU_PREVIOUS,GPU_CONSTANT);C3D_TexEnvFunc(e3,C3D_Both,GPU_MODULATE);
    C3D_TexBind(1,&bloomTex_[2]);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE_MINUS_SRC_COLOR,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE);
    bloomQuad(bloomTex_[1],100,60,400,240,0,0,50,30);
    for(int unit=0;unit<3;++unit)C3D_TexBind(unit,nullptr);
    auto* env0=env();C3D_TexEnvSrc(env0,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env0,C3D_Both,GPU_MODULATE);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLoc_,&projection_);
    spriteStateReady_=false;
}
void Renderer::endEyeNow(){
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
    for(int unit=0;unit<3;++unit)C3D_TexBind(unit,nullptr);
    // No clear: the first bottom draw covers every pixel opaquely (a memory
    // fill here would split the command list without flushing it).
    C3D_FrameDrawOn(bottomTarget_);
}
void Renderer::endNow(){
    // The GPU reads batched sprite vertices only after FrameEnd submits them.
    endEyeNow();
    // FrameEnd transfers each eye to these (the right half only in 3D mode).
    if(execStereo_.active())for(unsigned eye=0;eye<2;++eye)presented_[eye]=gfxGetFramebuffer(GFX_TOP,eye?GFX_RIGHT:GFX_LEFT,nullptr,nullptr);
    stats_.commandUsage=C3D_GetCmdBufUsage();C3D_FrameEnd(GX_CMDLIST_FLUSH);
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
