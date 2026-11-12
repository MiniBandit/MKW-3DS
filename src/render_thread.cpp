// Render thread: deferred frames record Renderer calls by value on the
// recording (main) thread and the second core replays them through the same
// citro3d code an immediate frame runs, one frame later. The main thread then
// simulates frame k+1 while frame k's GPU commands are generated.
//
// citro3d is single-context: in deferred mode only the render thread calls
// it, and the main thread uses it again only after drain(). Two command lists
// alternate, so frame k+1 records while frame k draws. Recorded copies cover
// everything a later step may change (transforms, palettes, binder contexts,
// particles, the per-draw animation frame and Star colour, the vertex buffer
// a draw binds); what a recorded frame references by pointer (models,
// textures, specs) must outlive it, which waitForGpu() guarantees for the
// race's loads and releases. CPU writes the GPU reads go through
// deferredWrites() and run on the render thread after FrameBegin.
//
// Stereo frames (docs/STEREO_3D.md) draw the list once per eye: the left eye
// as a 2D frame would, then the right eye with its own projection and target,
// reusing every buffer the left eye wrote. Immediate frames in stereo (menus)
// record the same way and are drawn on their own thread at end().
#include "mkw/renderer.hpp"
#include "mkw/menu_ui.hpp"
#include "mkw/race_effects.hpp"
#include "mkw/nw4r_ef_draw.hpp"
#include "mkw/retail_lighting.hpp"
#include "mkw/skin_positions.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mkw {
namespace {
thread_local bool renderThread=false;
// The thread that records frames (it constructs the Renderer); job threads only queue writes.
thread_local bool recordThread=false;
Renderer* activeRenderer=nullptr;
// Ordinary-heap bump allocator in 64 KiB chunks, kept for reuse: a frame list
// allocates nothing once its chunks cover the largest frame.
struct Arena {
    struct Chunk {std::unique_ptr<uint8_t[]> bytes;std::size_t size=0;};
    std::vector<Chunk> chunks;
    std::size_t current=0,used=0;
    void* allocate(std::size_t n){
        n=(n+7)&~std::size_t(7);
        while(current<chunks.size()&&used+n>chunks[current].size){++current;used=0;}
        if(current==chunks.size()){
            const std::size_t size=std::max<std::size_t>(64*1024,n);
            chunks.push_back({std::unique_ptr<uint8_t[]>(new uint8_t[size]),size});used=0;
        }
        void* p=chunks[current].bytes.get()+used;used+=n;
        return p;
    }
    void reset(){current=0;used=0;}
};
struct Header {uint16_t type,reserved;uint32_t size;};
enum : uint16_t {
    CmdBegin=1,CmdEnd,CmdProjection,CmdCourseLighting,CmdRetailFog,CmdRequestBloom,CmdApplyBloom,CmdBottom,
    CmdDraw,CmdDrawAtScreen,CmdMenuIcon,CmdEnvironmentUv,CmdUv1,CmdUvOffset,CmdClearEnvironment,
    CmdRigid,CmdSkin,CmdOverrides,CmdShadows,CmdSparks,CmdEffects,CmdSceneEffects,CmdCinema,CmdFade,CmdSprite,CmdFontSprite,
    CmdMaterialQuads,CmdText,CmdScreenDepth,CmdMenuModelLayer,CmdIntroWorldEnd,CmdRequestStadium,CmdCoinHudModel,CmdPlayerShadow,CmdSceneRegion,CmdShadowUnion,
};
// begun: C3D_FrameBegin already ran on the recording thread (stereo immediate frames).
struct ShadowUnionCommand {static constexpr uint16_t Type=CmdShadowUnion;bool active;};
struct SceneRegionCommand {static constexpr uint16_t Type=CmdSceneRegion;unsigned mask;};
struct BeginCommand {static constexpr uint16_t Type=CmdBegin;C3D_Mtx view,projection;unsigned lightingTick;bool sync,begun,loading;};
struct EndCommand {static constexpr uint16_t Type=CmdEnd;};
struct ProjectionCommand {static constexpr uint16_t Type=CmdProjection;C3D_Mtx projection;};
struct CourseLightingCommand {static constexpr uint16_t Type=CmdCourseLighting;bool enabled;};
struct RetailFogCommand {static constexpr uint16_t Type=CmdRetailFog;RetailFog fog;};
struct RequestBloomCommand {static constexpr uint16_t Type=CmdRequestBloom;const CourseBloom* settings;};
struct RequestStadiumCommand {static constexpr uint16_t Type=CmdRequestStadium;const Bounds* screens;unsigned count;};
struct ApplyBloomCommand {static constexpr uint16_t Type=CmdApplyBloom;};
struct BottomCommand {static constexpr uint16_t Type=CmdBottom;};
struct IntroWorldEndCommand {static constexpr uint16_t Type=CmdIntroWorldEnd;};
struct DrawCommand {
    static constexpr uint16_t Type=CmdDraw;
    const Model* model;const Vertex* vertices;C3D_Mtx transform;
    Renderer::MaterialBinder material;void* context;Renderer::ShapeVisible visible;
    uint32_t frame,additive;bool backdrop;
};
struct DrawAtScreenCommand {
    static constexpr uint16_t Type=CmdDrawAtScreen;
    const Model* model;const Vertex* vertices;C3D_Mtx transform;float x,y,brightness,scaleX,scaleY;uint32_t frame,additive;
    Renderer::MaterialBinder material;void* context;
};
struct MenuIconCommand {static constexpr uint16_t Type=CmdMenuIcon;const Model* model;const Vertex* vertices;C3D_Mtx transform;const UiQuad* quad;uint32_t frame,additive;};
struct MenuModelLayerCommand {static constexpr uint16_t Type=CmdMenuModelLayer;const Model* model;const Vertex* vertices;C3D_Mtx transform;uint32_t frame,additive;};
struct CoinHudModelCommand {static constexpr uint16_t Type=CmdCoinHudModel;const Model* model;const Vertex* vertices;C3D_Mtx transform;uint32_t frame,additive;bool firstColumn;};
struct EnvironmentUvCommand {static constexpr uint16_t Type=CmdEnvironmentUv;C3D_Mtx transform;float uv[6];bool texture2;float blend;};
struct Uv1Command {static constexpr uint16_t Type=CmdUv1;float uv[6];};
struct UvOffsetCommand {static constexpr uint16_t Type=CmdUvOffset;float u,v;};
struct ClearEnvironmentCommand {static constexpr uint16_t Type=CmdClearEnvironment;};
struct RigidCommand {static constexpr uint16_t Type=CmdRigid;const RigidPose* pose;};
struct SkinCommand {static constexpr uint16_t Type=CmdSkin;const float* palette;unsigned bones,stride;};
struct OverridesCommand {static constexpr uint16_t Type=CmdOverrides;const SubmeshOverride* overrides;unsigned count;};
struct PlayerShadowCommand {static constexpr uint16_t Type=CmdPlayerShadow;const Renderer::ShadowCaster* casters;unsigned count;const Model* const* receivers;unsigned receiverCount;Vec3 origin,lightDirection;float scale;const Renderer::ShadowVolume* fallback;const Renderer::ShadowReceiver* objects;unsigned objectCount;bool actor;};
struct ShadowsCommand {static constexpr uint16_t Type=CmdShadows;const Renderer::ShadowVolume* volumes;unsigned count;float darkness;};
struct SparksCommand {static constexpr uint16_t Type=CmdSparks;const SparkSpec* specs;unsigned specCount;const Spark* sparks;unsigned count;const Texture* textures;};
// The NW4R EF draw list is built while the frame records (FrameList::effects).
struct SceneEffectsCommand {static constexpr uint16_t Type=CmdSceneEffects;const RaceEffects* effects;const ef::DrawList* list;const Texture* textures;const EffectLayerBuffer* buffer;};
struct EffectsCommand {static constexpr uint16_t Type=CmdEffects;const RaceEffects* effects;const ef::DrawList* list;const Texture* textures;};
struct CinemaCommand {static constexpr uint16_t Type=CmdCinema;float top,bottom,opacity;};
struct FadeCommand {static constexpr uint16_t Type=CmdFade;float opacity;bool white;};
struct SpriteCommand {
    static constexpr uint16_t Type=CmdSprite;
    const Texture* texture;float x,y,width,height,u0,v0,u1,v1,alpha,angle;uint32_t rgba,corners[4];bool hasCorners,additive;
};
struct FontSpriteCommand {
    static constexpr uint16_t Type=CmdFontSprite;
    const Texture* texture;float x,y,width,height,u1,v1,alpha;uint32_t gradientTop,gradientBottom,color0,color1;
};
struct MaterialQuadsCommand {
    static constexpr uint16_t Type=CmdMaterialQuads;
    const Texture* texture;const Renderer::MaterialQuad* quads;unsigned count;uint32_t color0,color1;int wrapS,wrapT;bool additive;
};
struct TextCommand {static constexpr uint16_t Type=CmdText;const char* text;float x,y,size;uint32_t rgba;};
struct ScreenDepthCommand {static constexpr uint16_t Type=CmdScreenDepth;float parallax;};
}

struct DeferredWrites::Storage {
    LightLock lock;
    Arena arena;
    std::vector<Write> writes;
    Storage(){LightLock_Init(&lock);writes.reserve(256);}
};
void* DeferredWrites::allocate(std::size_t bytes){
    LightLock_Lock(&storage_->lock);void* p=storage_->arena.allocate(bytes);LightLock_Unlock(&storage_->lock);
    return p;
}
void DeferredWrites::queue(void (*run)(void*),void* payload){
    LightLock_Lock(&storage_->lock);storage_->writes.push_back({run,payload});LightLock_Unlock(&storage_->lock);
}

struct Renderer::FrameList {
    std::vector<uint8_t> commands;
    Arena data;
    DeferredWrites::Storage storage;
    DeferredWrites writes;
    // This frame's NW4R EF draw list; its vectors keep their capacity across frames.
    std::unique_ptr<ef::DrawList> effects=std::make_unique<ef::DrawList>();
    std::vector<std::unique_ptr<ef::DrawList>> sceneEffects;unsigned sceneEffectsUsed=0;
    unsigned frame=0;
    // Recorded at begin(): both eyes are drawn when active.
    stereo::Frame stereo{};bool dualIntro=false;
    FrameList(){commands.reserve(128*1024);writes.storage_=&storage;}
    void reset(){sceneEffectsUsed=0;commands.clear();data.reset();storage.arena.reset();storage.writes.clear();stereo={};dualIntro=false;}
};

DeferredWrites* deferredWrites(){return activeRenderer?activeRenderer->writes():nullptr;}

Renderer::Renderer(){
    lists_[0]=std::make_unique<FrameList>();lists_[1]=std::make_unique<FrameList>();
    LightLock_Init(&publishLock_);
    activeRenderer=this;recordThread=true;
}
Renderer::~Renderer(){if(activeRenderer==this)activeRenderer=nullptr;}

bool Renderer::startRenderThread(){
    if(thread_)return true;
    if(!ready_)return false;
    LightEvent_Init(&wake_,RESET_ONESHOT);LightEvent_Init(&done_,RESET_ONESHOT);
    // Identical VFP mode (flush-to-zero, rounding) to the recording thread:
    // the render thread's matrices and culling match an immediate frame's.
    fpscr_=__builtin_arm_get_fpscr();
    bool n3ds=false;APT_CheckNew3DS(&n3ds);
    // New 3DS reserves core 2 for rendering and short audio work. Animation
    // jobs share main core 0 at lower priority; Old 3DS retains core 1.
    s32 priority=0x30;svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
    if(!n3ds){u32 limit=0;APT_GetAppCpuTimeLimit(&limit);if(limit<80)APT_SetAppCpuTimeLimit(80);}
    thread_=threadCreate(threadEntry,this,64*1024,std::clamp<s32>(priority+1,0x1a,0x3f),n3ds?2:1,false);
    // aptMainLoop runs citro3d's suspend hook (GX queue wait, then a state
    // reset on restore) on this thread. Hooks run newest first: this one
    // finishes every recorded frame before that, and before sleep.
    if(thread_)aptHook(&aptCookie_,aptEvent,this);
    return thread_!=nullptr;
}
void Renderer::aptEvent(APT_HookType type,void* self){
    if(type==APTHOOK_ONSUSPEND||type==APTHOOK_ONSLEEP)static_cast<Renderer*>(self)->drain();
}
void Renderer::threadEntry(void* self){static_cast<Renderer*>(self)->threadLoop();}
void Renderer::threadLoop(){
    __builtin_arm_set_fpscr(fpscr_);
    renderThread=true;positionScratchIndex=2;
    for(;;){
        const unsigned next=completed_.load(std::memory_order_relaxed);
        while(next==submitted_.load(std::memory_order_acquire)){
            if(quit_.load(std::memory_order_acquire))return;
            LightEvent_Wait(&wake_);
        }
        execute(*lists_[next%2]);
        completed_.store(next+1,std::memory_order_release);
        LightEvent_Signal(&done_);
    }
}
// A local replay executes on the recording thread: binders' renderer calls draw.
bool Renderer::recording() const {return !renderThread&&!localReplay_&&(deferred_||localRecording_);}
bool Renderer::canRecordFrame() const{
    if(!deferred_||!thread_)return true;
    const unsigned frame=submitted_.load(std::memory_order_relaxed);
    return frame<2||completed_.load(std::memory_order_acquire)+1>=frame;
}
Renderer::FrameList* Renderer::acquire(){
    const unsigned frame=submitted_.load(std::memory_order_relaxed);
    recordWaitMs=0;
    // List frame%2 last held frame-2: free once that frame has been drawn.
    if(frame>=2&&completed_.load(std::memory_order_acquire)+1<frame){
        const auto start=svcGetSystemTick();
        while(completed_.load(std::memory_order_acquire)+1<frame){
            if(LightEvent_WaitTimeout(&done_,50000000LL))svcSleepThread(1000000LL);
        }
        recordWaitMs+=float((svcGetSystemTick()-start)*1000.0/SYSCLOCK_ARM11);
    }
    auto& list=*lists_[frame%2];
    list.reset();list.frame=frame;
    return &list;
}
void* Renderer::command(uint16_t type,std::size_t bytes){
    if(!recording_)recording_=acquire();
    auto& c=recording_->commands;
    const std::size_t at=c.size(),size=(sizeof(Header)+bytes+7)&~std::size_t(7);
    c.resize(at+size);
    auto* h=reinterpret_cast<Header*>(c.data()+at);h->type=type;h->size=uint32_t(size);
    return h+1;
}
void* Renderer::data(std::size_t bytes){
    if(!recording_)recording_=acquire();
    return recording_->data.allocate(bytes);
}
DeferredWrites* Renderer::writes(){
    if(renderThread)return nullptr;
    // A frame between begin() and end() queues its writes.
    if(auto* list=writeTarget_.load(std::memory_order_acquire))return &list->writes;
    // Outside a frame (loads) they run at once, after every drawn frame.
    if(deferred_&&recordThread&&!gpuIdle_)waitForGpu();
    return nullptr;
}
void Renderer::submit(){
    if(!recording_||recording_==localList_.get())return;
    writeTarget_.store(nullptr,std::memory_order_release);
    recording_=nullptr;gpuIdle_=false;
    submitted_.store(submitted_.load(std::memory_order_relaxed)+1,std::memory_order_release);
    LightEvent_Signal(&wake_);
}
void Renderer::drain(){
    // Each eye replays a whole stereo frame, so one being recorded is not
    // split here: callers mid-frame (effect material rebuilds) only need the
    // frames already submitted to finish.
    if(!(writeTarget_.load(std::memory_order_relaxed)&&recording_&&(recording_->stereo.active()||recording_->dualIntro)))submit();
    if(!thread_)return;
    const unsigned target=submitted_.load(std::memory_order_relaxed);
    while(completed_.load(std::memory_order_acquire)!=target){
        if(LightEvent_WaitTimeout(&done_,50000000LL))svcSleepThread(1000000LL);
    }
}
void Renderer::setDeferred(bool on){
    on=on&&thread_;
    if(on==deferred_)return;
    drain();
    deferred_=on;
}
void Renderer::runWrites(FrameList& list){
    // Every recording thread has finished with the list (submitted).
    for(const auto& w:list.storage.writes)w.run(w.payload);
    list.storage.writes.clear();
}
void Renderer::publish(){
    LightLock_Lock(&publishLock_);
    published_=stats_;published_.metrics=renderMetrics;
    LightLock_Unlock(&publishLock_);
}
void Renderer::execute(FrameList& list){
    const auto start=svcGetSystemTick();
    if(!localReplay_)stats_.beginWaitMs=0;
    bool began=false;
    execStereo_=targetRight_?list.stereo:stereo::Frame{};
    const unsigned eyes=execStereo_.active()?2:1;
    execDualScreenIntro_=list.dualIntro&&bottomTarget_;
    const unsigned passes=eyes+unsigned(execDualScreenIntro_);
    for(unsigned pass=0;pass<passes;++pass){
    bottomWorldPass_=pass==eyes;
    const unsigned eye=bottomWorldPass_?0:pass;
    bool afterWorld=false;
    // Each eye starts from the state the frame started with.
    rigid_=nullptr;skinPalette_=nullptr;overrides_=nullptr;overrideCount_=0;eye_=eye;
    // The bottom screen (after CmdBottom) is drawn once, with the last eye.
    bool bottom=false;
    const uint8_t* p=list.commands.data();const uint8_t* end=p+list.commands.size();
    while(p<end){
        const auto& h=*reinterpret_cast<const Header*>(p);const void* body=&h+1;p+=h.size;
        if(bottomWorldPass_){
            // The world reuses its first pass's GPU data. Later 2D panes only
            // belong on top; full-screen fades span both LCDs.
            if(afterWorld&&h.type!=CmdFade&&h.type!=CmdEnd)continue;
            if(h.type==CmdRequestStadium||h.type==CmdRequestBloom||h.type==CmdApplyBloom||h.type==CmdBottom)continue;
        }else if(bottom&&(execDualScreenIntro_||eye+1<eyes)&&h.type!=CmdEnd)continue;
        switch(h.type){
        case CmdBegin:{
            const auto& c=*static_cast<const BeginCommand*>(body);execFrame_=c.lightingTick;execLoadingFrame_=c.loading;
            if(bottomWorldPass_){beginBottomWorldNow(c.view,c.projection);break;}
            if(eye){beginEyeNow(c.view,c.projection);break;}
            beginNow(c.view,c.projection,c.sync,c.begun);
            // The GPU has finished every earlier frame.
            gpuDone_.store(list.frame,std::memory_order_release);
            runWrites(list);began=true;break;
        }
        case CmdEnd:if(pass+1<passes)endEyeNow();else endNow();break;
        case CmdIntroWorldEnd:afterWorld=true;break;
        case CmdProjection:projection_=eyeProjection(static_cast<const ProjectionCommand*>(body)->projection);break;
        case CmdSceneRegion:execSceneRegion_=static_cast<const SceneRegionCommand*>(body)->mask;break;
        case CmdCourseLighting:courseLighting_=static_cast<const CourseLightingCommand*>(body)->enabled;break;
        case CmdRetailFog:retailFogNow(static_cast<const RetailFogCommand*>(body)->fog);break;
        case CmdRequestStadium:{const auto& c=*static_cast<const RequestStadiumCommand*>(body);requestStadiumScreen(c.screens,c.count);break;}
        case CmdRequestBloom:{const auto& c=*static_cast<const RequestBloomCommand*>(body);requestBloomNow(*c.settings);break;}
        case CmdApplyBloom:applyBloom();break;
        case CmdBottom:bottom=true;if(!execDualScreenIntro_&&eye+1==eyes)beginBottom();break;
        case CmdDraw:{
            const auto& c=*static_cast<const DrawCommand*>(body);
            execFrame_=c.frame;execAdditive_=c.additive;
            drawNow(*c.model,c.vertices,c.transform,c.backdrop,c.material,c.context,c.visible);break;
        }
        case CmdDrawAtScreen:{
            const auto& c=*static_cast<const DrawAtScreenCommand*>(body);
            execFrame_=c.frame;execAdditive_=c.additive;
            drawAtScreenNow(*c.model,c.vertices,c.transform,c.x,c.y,c.brightness,c.scaleX,c.scaleY,c.material,c.context);break;
        }
        case CmdMenuIcon:{
            const auto& c=*static_cast<const MenuIconCommand*>(body);
            execFrame_=c.frame;execAdditive_=c.additive;
            drawMenuIconNow(*c.model,c.vertices,c.transform,*c.quad);break;
        }
        case CmdMenuModelLayer:{
            const auto& c=*static_cast<const MenuModelLayerCommand*>(body);
            execFrame_=c.frame;execAdditive_=c.additive;
            drawMenuModelLayerNow(*c.model,c.vertices,c.transform);break;
        }
        case CmdCoinHudModel:{
            const auto& c=*static_cast<const CoinHudModelCommand*>(body);
            execFrame_=c.frame;execAdditive_=c.additive;
            drawCoinHudModelNow(*c.model,c.vertices,c.transform,c.firstColumn);break;
        }
        case CmdShadowUnion:{shadowUnionNow(static_cast<const ShadowUnionCommand*>(body)->active);break;}
        case CmdEnvironmentUv:{const auto& c=*static_cast<const EnvironmentUvCommand*>(body);setEnvironmentUv(c.transform,c.uv,c.texture2,c.blend);break;}
        case CmdUv1:setUv1Transform(static_cast<const Uv1Command*>(body)->uv);break;
        case CmdUvOffset:{const auto& c=*static_cast<const UvOffsetCommand*>(body);setUvOffset(c.u,c.v);break;}
        case CmdClearEnvironment:clearEnvironmentUv();break;
        case CmdRigid:rigid_=static_cast<const RigidCommand*>(body)->pose;break;
        case CmdSkin:{const auto& c=*static_cast<const SkinCommand*>(body);skinPalette_=c.palette;skinBones_=c.bones;skinStride_=c.stride;break;}
        case CmdOverrides:{const auto& c=*static_cast<const OverridesCommand*>(body);overrides_=c.overrides;overrideCount_=c.overrides?c.count:0;break;}
        case CmdPlayerShadow:{const auto& c=*static_cast<const PlayerShadowCommand*>(body);playerShadowNow(c.casters,c.count,c.receivers,c.receiverCount,c.origin,c.lightDirection,c.scale,c.fallback,c.objects,c.objectCount,c.actor);break;}
        case CmdShadows:{const auto& c=*static_cast<const ShadowsCommand*>(body);drawShadowVolumes(c.volumes,c.count,c.darkness);break;}
        case CmdSparks:{const auto& c=*static_cast<const SparksCommand*>(body);drawSparksNow(c.specs,c.specCount,c.sparks,c.count,c.textures);break;}
        case CmdSceneEffects:{const auto& c=*static_cast<const SceneEffectsCommand*>(body);sceneEffectListNow(*c.effects,*c.list,c.textures,*c.buffer);break;}
        case CmdEffects:{const auto& c=*static_cast<const EffectsCommand*>(body);effectListNow(*c.effects,*c.list,c.textures);break;}
        case CmdCinema:{const auto& c=*static_cast<const CinemaCommand*>(body);drawCinemaBars(c.top,c.bottom,c.opacity);break;}
        case CmdFade:{const auto& c=*static_cast<const FadeCommand*>(body);drawScreenFade(c.opacity,c.white);break;}
        case CmdSprite:{
            const auto& c=*static_cast<const SpriteCommand*>(body);
            drawScreenSpriteRegion(*c.texture,c.x,c.y,c.width,c.height,c.u0,c.v0,c.u1,c.v1,c.alpha,c.rgba,c.hasCorners?c.corners:nullptr,c.additive,c.angle);break;
        }
        case CmdFontSprite:{
            const auto& c=*static_cast<const FontSpriteCommand*>(body);
            drawScreenFontSprite(*c.texture,c.x,c.y,c.width,c.height,c.u1,c.v1,c.alpha,c.gradientTop,c.gradientBottom,c.color0,c.color1);break;
        }
        case CmdMaterialQuads:{
            const auto& c=*static_cast<const MaterialQuadsCommand*>(body);
            materialQuadsNow(c.texture,c.quads,c.count,c.color0,c.color1,c.wrapS,c.wrapT,c.additive);break;
        }
        case CmdText:{const auto& c=*static_cast<const TextCommand*>(body);drawScreenText(c.text,c.x,c.y,c.size,c.rgba);break;}
        case CmdScreenDepth:screenDepthNow(static_cast<const ScreenDepthCommand*>(body)->parallax);break;
        }
    }
    }
    eye_=0;execStereo_={};execDualScreenIntro_=false;bottomWorldPass_=false;
    if(!began&&!list.storage.writes.empty()){
        // Writes recorded outside a frame wait for the GPU like a frame's.
        if(!C3D_FrameBegin(0))std::abort();
        runWrites(list);
        C3D_FrameEnd(GX_CMDLIST_FLUSH);
    }
    // Recorded copies end with the list.
    rigid_=nullptr;skinPalette_=nullptr;overrides_=nullptr;overrideCount_=0;
    stats_.renderMs=float((svcGetSystemTick()-start)*1000.0/SYSCLOCK_ARM11)-stats_.beginWaitMs;
    if(began)publish();
}
void Renderer::retireShared(std::shared_ptr<void> object){
    retired_.push_back({submitted_.load(std::memory_order_relaxed),std::move(object)});
}
void Renderer::releaseRetired(){
    // Entry frame f: frames up to f may sample it; free once the GPU finished f.
    const unsigned done=deferred_?gpuDone_.load(std::memory_order_acquire):~0u;
    std::erase_if(retired_,[&](const Retired& r){return r.frame<done;});
}

// Recording side of the public calls (renderer.cpp holds the drawing side).
void Renderer::setFov(float degrees,float nearPlane,float farPlane){
    fov_=std::clamp(degrees,1.f,170.f);Mtx_PerspTilt(&mainProjection_,C3D_AngleFromDegrees(fov_),mkw::display::Aspect,std::clamp(nearPlane,1.f,100.f),std::max(farPlane,101.f),false);
    cullProjection_=mainProjection_;
    if(recording())command<ProjectionCommand>().projection=mainProjection_;
    else projection_=mainProjection_;
}
float Renderer::slider() const{return sliderOverride_>=0?sliderOverride_:osGet3DSliderState();}
// Recording side of a frame's stereo parameters (mainView_ is this frame's).
// As in Mario Kart 7 the reference plane keeps a fixed view depth for the
// whole scene: the subject's depth, set per frame, times the scene's ratio.
void Renderer::beginStereo(float targetDistance){
    frameStereo_={};frameSlider_=0;cullProjection_=mainProjection_;
    const stereo::Scene scene=stereoSubjectSet_?stereoScene_:stereo::Race;
    const float subject=stereoSubjectSet_?stereoSubject_:targetDistance;
    stereoSubjectSet_=false;
    if(!targetRight_)return;
    const float slider=this->slider();
    if(!(slider>0))return;
    frameSlider_=std::min(slider,1.f);
    frameStereo_=stereo::frame(slider,stereoDisplay_,scene,subject);
    float m[4][4];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)m[r][c]=mainProjection_.r[r].c[3-c];
    stereo::cullingUnion(m,frameStereo_);
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)cullProjection_.r[r].c[3-c]=m[r][c];
}
void Renderer::setScreenDepth(float parallax){
    if(!frameStereo_.active())return;
    if(recording()){command<ScreenDepthCommand>().parallax=parallax;return;}
    screenDepthNow(parallax);
}
void Renderer::setSceneRegion(unsigned mask){
    recordSceneRegion_=mask==1||mask==2?mask:3;
    if(recording())command<SceneRegionCommand>().mask=recordSceneRegion_;
    else execSceneRegion_=recordSceneRegion_;
}
void Renderer::setCourseLighting(bool enabled){
    if(recording())command<CourseLightingCommand>().enabled=enabled;
    else courseLighting_=enabled;
}
void Renderer::setRetailFog(const RetailFog& fog){
    if(recording())command<RetailFogCommand>().fog=fog;
    else retailFogNow(fog);
}
void Renderer::requestStadiumScreen(const Bounds* screens,unsigned count){
    if(recording()){
        Bounds* copy=nullptr;
        if(screens&&count){copy=static_cast<Bounds*>(data(size_t(count)*sizeof(Bounds)));std::copy_n(screens,count,copy);}
        auto& c=command<RequestStadiumCommand>();c.screens=copy;c.count=copy?count:0;
    }else{
        stadiumRequested_=true;stadiumScreens_.clear();
        if(screens&&count)stadiumScreens_.assign(screens,screens+count);
    }
}
void Renderer::requestBloom(const CourseBloom& settings){
    if(!recording()){requestBloomNow(settings);return;}
    // Bloom's VRAM targets (unless reserved at load) and LINEAR vertices:
    // create them here, once, with the render thread idle.
    if(settings.enabled()&&!bloomVertices_&&!bloomFailed_){drain();initBloom();}
    command<RequestBloomCommand>().settings=&settings;
}
void Renderer::begin(Vec3 eye,Vec3 target,Vec3 up,bool mirror){
    auto e=FVec3_New(eye.x,eye.y,eye.z),t=FVec3_New(target.x,target.y,target.z),u=FVec3_New(up.x,up.y,up.z);
    Mtx_LookAt(&mainView_,e,t,u,false);
    // CtrlRaceChase USA807e8810..89fc: reflect the camera eye through
    // its target in X/Z, retaining eye Y and the original up vector.
    // This projects items approaching from behind, outside the race view.
    const auto chaseEye=FVec3_New(2*target.x-eye.x,eye.y,2*target.z-eye.z);
    Mtx_LookAt(&chaseView_,chaseEye,t,u,false);
    if(mirror)for(unsigned c=0;c<4;++c)chaseView_.r[0].c[c]=-chaseView_.r[0].c[c];
    // Reflect camera-space X, including its translation. The whole world,
    // collision and route simulation remain in one shared source coordinate
    // system; human steering/tricks are inverted at the input boundary.
    // Captured in BeginCommand's view for both eyes and the deferred worker.
    if(mirror)for(unsigned c=0;c<4;++c)mainView_.r[0].c[c]=-mainView_.r[0].c[c];
    mainScreenWidth_=400.f;
    frameDualScreenIntro_=dualScreenIntro_&&bottomTarget_;
    beginStereo(length(target-eye));
    if(frameDualScreenIntro_)for(unsigned c=0;c<4;++c)
        cullProjection_.r[0].c[c]=cullProjection_.r[0].c[c]*display::IntroCullScale+
            verticalSign_*display::IntroCullOffset*cullProjection_.r[3].c[c];
    beginRecorded();
}
void Renderer::beginRecorded(){
    if(!recording()&&(frameStereo_.active()||frameDualScreenIntro_)){
        // An immediate frame in stereo: wait for the GPU now, as beginNow
        // would, so CPU writes until end() stay safe; record the draws.
        const auto waitStart=svcGetSystemTick();
        if(!C3D_FrameBegin(frameSync?C3D_FRAME_SYNCDRAW:0))std::abort();
        stats_.beginWaitMs=float((svcGetSystemTick()-waitStart)*1000.0/SYSCLOCK_ARM11);
        stats_.previousGpuMs=C3D_GetDrawingTime();
        renderMetrics={};retired_.clear();
        if(!localList_)localList_=std::make_unique<FrameList>();
        localList_->reset();localList_->stereo=frameStereo_;localList_->dualIntro=frameDualScreenIntro_;
        recording_=localList_.get();localRecording_=true;
        auto& c=command<BeginCommand>();c.view=mainView_;c.projection=mainProjection_;c.lightingTick=animationFrame;c.loading=loadingFrame;c.sync=frameSync;c.begun=true;
        return;
    }
    if(!recording()){execFrame_=animationFrame;execLoadingFrame_=loadingFrame;beginNow(mainView_,mainProjection_,frameSync);retired_.clear();return;}
    renderMetrics={};
    releaseRetired();
    auto& c=command<BeginCommand>();c.view=mainView_;c.projection=mainProjection_;c.lightingTick=animationFrame;c.loading=loadingFrame;c.sync=frameSync;c.begun=false;
    recording_->stereo=frameStereo_;recording_->dualIntro=frameDualScreenIntro_;
    writeTarget_.store(recording_,std::memory_order_release);
}
void Renderer::beginSourceCamera(const float* view,const float* projection){
    Mtx_Identity(&mainView_);
    for(int row=0;row<3;++row)for(int col=0;col<4;++col)
        mainView_.r[row].c[3-col]=view[row*4+col];
    // Ask citro3d for its display rotation, then apply that rotation to the
    // complete source projection (including shake). Both use -1..0 clip depth.
    C3D_Mtx tilt;Mtx_PerspTilt(&tilt,C3D_AngleFromDegrees(90.f),1.f,1.f,2.f,false);
    const float xSign=tilt.r[0].y<0?-1.f:1.f;
    const float ySign=tilt.r[1].x<0?-1.f:1.f;
    for(int col=0;col<4;++col){
        mainProjection_.r[0].c[3-col]=xSign*projection[4+col];
        mainProjection_.r[1].c[3-col]=ySign*projection[col];
        mainProjection_.r[2].c[3-col]=projection[8+col];
        mainProjection_.r[3].c[3-col]=projection[12+col];
    }
    mainScreenWidth_=400.f;
    // No target point: the stereo subject when set, else 1000 units.
    frameDualScreenIntro_=dualScreenIntro_&&bottomTarget_;
    beginStereo(1000.f);
    if(frameDualScreenIntro_)for(unsigned c=0;c<4;++c)
        cullProjection_.r[0].c[c]=cullProjection_.r[0].c[c]*display::IntroCullScale+
            verticalSign_*display::IntroCullOffset*cullProjection_.r[3].c[c];
    beginRecorded();
}
void Renderer::end(){
    if(localRecording_){endLocal();return;}
    if(!recording()){
        endNow();gpuIdle_=false;
        triangles=stats_.triangles;drawCalls=stats_.drawCalls;culledTriangles=stats_.culledTriangles;culledSubmeshes=stats_.culledSubmeshes;
        previousGpuMs=stats_.previousGpuMs;beginWaitMs=stats_.beginWaitMs;commandUsage=stats_.commandUsage;renderThreadMs=0;
        return;
    }
    command<EndCommand>();
    submit();
    // The last frame the render thread finished (one frame behind).
    LightLock_Lock(&publishLock_);const Stats s=published_;LightLock_Unlock(&publishLock_);
    triangles=s.triangles;drawCalls=s.drawCalls;culledTriangles=s.culledTriangles;culledSubmeshes=s.culledSubmeshes;
    previousGpuMs=s.previousGpuMs;beginWaitMs=s.beginWaitMs;commandUsage=s.commandUsage;renderThreadMs=s.renderMs;
    renderMetrics.flushCalls+=s.metrics.flushCalls;renderMetrics.flushBytes+=s.metrics.flushBytes;
    renderMetrics.skinnedVertices+=s.metrics.skinnedVertices;
    renderMetrics.contactDraws=s.metrics.contactDraws;renderMetrics.contactTriangles=s.metrics.contactTriangles;
    renderMetrics.contactMs=s.metrics.contactMs;renderMetrics.shadowSupportMs=s.metrics.shadowSupportMs;
}
// Draws the recorded immediate stereo frame: both eyes, then FrameEnd.
void Renderer::endLocal(){
    command<EndCommand>();
    localRecording_=false;recording_=nullptr;localReplay_=true;
    execute(*localList_);
    localReplay_=false;gpuIdle_=false;
    triangles=stats_.triangles;drawCalls=stats_.drawCalls;culledTriangles=stats_.culledTriangles;culledSubmeshes=stats_.culledSubmeshes;
    previousGpuMs=stats_.previousGpuMs;beginWaitMs=stats_.beginWaitMs;commandUsage=stats_.commandUsage;renderThreadMs=0;
}
void Renderer::beginBottom(){
    // The target exists only between a race's load and its release (main thread).
    if(!bottomTarget_)return;
    if(recording()){mainScreenWidth_=320.f;command<BottomCommand>();return;}
    if(!renderThread)mainScreenWidth_=320.f;
    beginBottomNow();
}
void Renderer::markIntroWorldEnd(){
    if(frameDualScreenIntro_&&recording())command<IntroWorldEndCommand>();
}
void Renderer::applyBloom(){
    if(recording()){command<ApplyBloomCommand>();return;}
    if(stadiumFrame_)captureStadiumScreenNow();
    if(!bloomFrame_)return;
    applyBloomNow();
}
void Renderer::drawWith(const Model& model,const C3D_Mtx& transform,bool backdrop,MaterialBinder material,const void* context,std::size_t contextSize,ShapeVisible visible){
    if(!recording()){
        if(!renderThread){execFrame_=animationFrame;execAdditive_=additiveColor;}
        drawNow(model,model.vertices,transform,backdrop,material,const_cast<void*>(context),visible);
        return;
    }
    void* copied=nullptr;
    if(context&&contextSize){copied=data(contextSize);std::memcpy(copied,context,contextSize);}
    auto& c=command<DrawCommand>();
    c.model=&model;c.vertices=model.vertices;c.transform=transform;c.material=material;c.context=copied;c.visible=visible;
    c.frame=animationFrame;c.additive=additiveColor;c.backdrop=backdrop;
}
void Renderer::drawAtScreen(const Model& model,const C3D_Mtx& transform,float x,float y,float brightness,float scaleX,float scaleY){
    drawAtScreenWith(model,transform,x,y,brightness,scaleX,scaleY,nullptr,nullptr,0);
}
void Renderer::drawAtScreenWith(const Model& model,const C3D_Mtx& transform,float x,float y,float brightness,float scaleX,float scaleY,MaterialBinder material,const void* context,std::size_t contextSize){
    if(!recording()){
        if(!renderThread){execFrame_=animationFrame;execAdditive_=additiveColor;}
        drawAtScreenNow(model,model.vertices,transform,x,y,brightness,scaleX,scaleY,material,const_cast<void*>(context));
        return;
    }
    void* copied=nullptr;
    if(context&&contextSize){copied=data(contextSize);std::memcpy(copied,context,contextSize);}
    auto& c=command<DrawAtScreenCommand>();
    c.model=&model;c.vertices=model.vertices;c.transform=transform;c.x=x;c.y=y;c.brightness=brightness;c.scaleX=scaleX;c.scaleY=scaleY;
    c.frame=animationFrame;c.additive=additiveColor;c.material=material;c.context=copied;
}
void Renderer::drawMenuIcon(const Model& model,const C3D_Mtx& transform,const UiQuad& quad){
    if(!recording()){
        if(!renderThread){execFrame_=animationFrame;execAdditive_=additiveColor;}
        drawMenuIconNow(model,model.vertices,transform,quad);
        return;
    }
    const UiQuad* q=copy(&quad,1);
    auto& c=command<MenuIconCommand>();
    c.model=&model;c.vertices=model.vertices;c.transform=transform;c.quad=q;c.frame=animationFrame;c.additive=additiveColor;
}
void Renderer::drawMenuModelLayer(const Model& model,const C3D_Mtx& transform){
    if(!recording()){
        if(!renderThread){execFrame_=animationFrame;execAdditive_=additiveColor;}
        drawMenuModelLayerNow(model,model.vertices,transform);return;
    }
    auto& c=command<MenuModelLayerCommand>();
    c.model=&model;c.vertices=model.vertices;c.transform=transform;c.frame=animationFrame;c.additive=additiveColor;
}
void Renderer::drawCoinHudModel(const Model& model,const C3D_Mtx& transform,bool firstColumn){
    if(!recording()){
        if(!renderThread){execFrame_=animationFrame;execAdditive_=additiveColor;}
        drawCoinHudModelNow(model,model.vertices,transform,firstColumn);return;
    }
    auto& c=command<CoinHudModelCommand>();
    c.model=&model;c.vertices=model.vertices;c.transform=transform;c.frame=animationFrame;c.additive=additiveColor;c.firstColumn=firstColumn;
}
void Renderer::drawCameraFacing(const Model& model,Vec3 anchor,float upOffset,float forwardOffset,float scale){
    // The view rotation is orthonormal: its transpose maps camera axes back
    // into the world. Local +Z faces the viewer, as in NW4R's screen planes.
    C3D_Mtx transform;Mtx_Identity(&transform);
    for(unsigned row=0;row<3;++row){
        for(unsigned col=0;col<3;++col)
            transform.r[row].c[3-col]=mainView_.r[col].c[3-row]*scale;
    }
    const Vec3 up{mainView_.r[1].x,mainView_.r[1].y,mainView_.r[1].z};
    const Vec3 forward{-mainView_.r[2].x,-mainView_.r[2].y,-mainView_.r[2].z};
    const Vec3 position=anchor+up*upOffset+forward*forwardOffset;
    transform.r[0].w=position.x;transform.r[1].w=position.y;transform.r[2].w=position.z;
    draw(model,transform);
}
void Renderer::setRigidPose(const RigidPose* pose){
    if(!recording()){rigid_=pose;return;}
    RigidPose* copied=nullptr;
    if(pose){
        copied=copy(pose,1);
        unsigned matrices=pose->matrices;
        if(!matrices&&pose->parts)for(const auto& parts:*pose->parts)for(const auto& part:parts)matrices=std::max(matrices,unsigned(part.matrix)+1);
        copied->palette=copy(pose->palette,std::size_t(matrices)*pose->stride);
    }
    command<RigidCommand>().pose=copied;
}
void Renderer::setSkinPalette(const float* palette,unsigned bones,unsigned stride){
    if(!recording()){skinPalette_=bones<=MaxSkinBones?palette:nullptr;skinBones_=bones;skinStride_=stride;return;}
    // Each bone's 3x4 rows, packed.
    float* rows=nullptr;
    if(palette&&bones<=MaxSkinBones){
        rows=static_cast<float*>(data(std::size_t(bones)*12*sizeof(float)));
        for(unsigned b=0;b<bones;++b)std::memcpy(rows+b*12,palette+b*stride,12*sizeof(float));
    }
    auto& c=command<SkinCommand>();c.palette=rows;c.bones=bones;c.stride=12;
}
void Renderer::setOverrides(const SubmeshOverride* overrides,unsigned count){
    if(!recording()){overrides_=overrides;overrideCount_=overrides?count:0;return;}
    const SubmeshOverride* copied=copy(overrides,count);
    auto& c=command<OverridesCommand>();c.overrides=copied;c.count=copied?count:0;
}
void Renderer::setEnvironmentUv(const C3D_Mtx& transform,const float (&uv)[6],bool texture2,float blend){
    if(!recording()){environmentUvNow(transform,uv,texture2,blend);return;}
    auto& c=command<EnvironmentUvCommand>();c.transform=transform;std::memcpy(c.uv,uv,sizeof c.uv);c.texture2=texture2;c.blend=blend;
}
void Renderer::setUv1Transform(const float (&uv)[6]){
    if(!recording()){uv1TransformNow(uv);return;}
    std::memcpy(command<Uv1Command>().uv,uv,sizeof uv);
}
void Renderer::setUvOffset(float u,float v){
    if(!recording()){C3D_FVUnifSet(GPU_VERTEX_SHADER,uvScrollLoc_,u,v,u,v);return;}
    auto& c=command<UvOffsetCommand>();c.u=u;c.v=v;
}
void Renderer::clearEnvironmentUv(){
    if(!recording()){clearEnvironmentUvNow();return;}
    command<ClearEnvironmentCommand>();
}
void Renderer::drawPlayerShadow(const ShadowCaster* casters,unsigned count,const Model* const* receivers,unsigned receiverCount,Vec3 origin,Vec3 lightDirection,float scale,const ShadowVolume* fallback,const ShadowReceiver* objects,unsigned objectCount,bool actor){
    if(!count||(!receiverCount&&!objectCount)||!playerShadowTarget_){if(fallback)drawShadowVolumes(fallback,1,.5f);return;}
    if(!recording()){playerShadowNow(casters,count,receivers,receiverCount,origin,lightDirection,scale,fallback,objects,objectCount,actor);return;}
    auto* parts=copy(casters,count);
    for(unsigned i=0;i<count;++i)if(parts[i].palette){
        const unsigned stride=parts[i].stride;
        auto* rows=static_cast<float*>(data(parts[i].bones*12*sizeof(float)));
        for(unsigned b=0;b<parts[i].bones;++b)std::memcpy(rows+b*12,parts[i].palette+b*stride,12*sizeof(float));
        parts[i].palette=rows;parts[i].stride=12;
    }
    auto& c=command<PlayerShadowCommand>();c.casters=parts;c.count=count;
    c.receivers=copy(receivers,receiverCount);c.receiverCount=receiverCount;c.origin=origin;c.lightDirection=lightDirection;c.scale=scale;c.fallback=fallback?copy(fallback,1):nullptr;
    auto* receiving=copy(objects,objectCount);
    for(unsigned i=0;i<objectCount;++i)if(receiving[i].palette)
        receiving[i].palette=copy(receiving[i].palette,receiving[i].bones*12);
    c.objects=receiving;c.objectCount=objectCount;c.actor=actor;
}
void Renderer::drawShadowVolumes(const ShadowVolume* volumes,unsigned count,float darkness){
    if(!count)return;
    if(!recording()){shadowVolumesNow(volumes,count,darkness);return;}
    const ShadowVolume* copied=copy(volumes,count);
    auto& c=command<ShadowsCommand>();c.volumes=copied;c.count=count;c.darkness=darkness;
}
void Renderer::drawSparks(const DriftEffects& effects,const Texture* textures){
    if(!effects.ready)return;
    if(!recording()){
        unsigned count=0;for(const auto& p:effects.sparks)count+=p.age<p.life;
        std::vector<Spark> live;live.reserve(count);for(const auto& p:effects.sparks)if(p.age<p.life)live.push_back(p);
        drawSparksNow(effects.specs.data(),unsigned(effects.specs.size()),live.data(),unsigned(live.size()),textures);
        return;
    }
    // Live sparks in their slot order (the draw order).
    unsigned count=0;for(const auto& p:effects.sparks)count+=p.age<p.life;
    Spark* live=count?static_cast<Spark*>(data(sizeof(Spark)*count)):nullptr;
    unsigned n=0;for(const auto& p:effects.sparks)if(p.age<p.life)std::memcpy(static_cast<void*>(live+n++),&p,sizeof p);
    auto& c=command<SparksCommand>();
    c.specs=effects.specs.data();c.specCount=unsigned(effects.specs.size());c.sparks=live;c.count=count;c.textures=textures;
}
void Renderer::drawEffects(RaceEffects& effects,const Texture* textures){
    if(!effects.ready||!effects.liveParticles()||!effectVertices_||!effectProgramReady_)return;
    if(!recording()){drawEffectsImmediate(effects,textures);return;}
    updateEffectState(effects);
    // Built now from this step's particles and camera; the list belongs to this
    // frame's command list, which is not reused until the frame has been drawn.
    auto& c=command<EffectsCommand>();
    buildEffectList(effects,mainView_,*recording_->effects);
    c.effects=&effects;c.list=recording_->effects.get();c.textures=textures;
}
void Renderer::drawSceneEffects(RaceEffects& effects,const Texture* textures,EffectLayerBuffer& buffer){
    if(!effects.ready||!effects.liveParticles()||!buffer.vertices||!buffer.lookup.data||!effectProgramReady_)return;
    updateEffectState(effects);
    if(!recording()){
        drawSceneEffectsImmediate(effects,textures,buffer);return;
    }
    // Each scene bank owns a list and GPU buffer. Reuse its allocations across
    // frames; both LCDs replay the same list without advancing any particles.
    const auto at=recording_->sceneEffectsUsed++;
    if(at==recording_->sceneEffects.size())recording_->sceneEffects.push_back(std::make_unique<ef::DrawList>());
    auto& list=*recording_->sceneEffects[at];buildEffectList(effects,mainView_,list);
    auto& c=command<SceneEffectsCommand>();c.effects=&effects;c.list=&list;c.textures=textures;c.buffer=&buffer;
}
void Renderer::drawCinemaBars(float topHeight,float bottomHeight,float opacity){
    if(opacity<=0||(topHeight<=0&&bottomHeight<=0))return;
    if(!recording()){cinemaBarsNow(topHeight,bottomHeight,opacity);return;}
    auto& c=command<CinemaCommand>();c.top=topHeight;c.bottom=bottomHeight;c.opacity=opacity;
}
void Renderer::drawScreenFade(float opacity,bool white){
    if(opacity<=0)return;
    if(!recording()){screenFadeNow(opacity,white);return;}
    auto& c=command<FadeCommand>();c.opacity=opacity;c.white=white;
}
void Renderer::drawScreenSprite(const Texture& texture,float x,float y,float width,float height,float u1,float v1,float alpha,uint32_t rgba){
    drawScreenSpriteRegion(texture,x,y,width,height,0,0,u1,v1,alpha,rgba);
}
void Renderer::drawScreenSpriteRegion(const Texture& texture,float x,float y,float width,float height,float u0,float v0,float u1,float v1,float alpha,uint32_t rgba,const uint32_t* corners,bool additive,float angle){
    if(!texture.ready||alpha<=0)return;
    if(!recording()){spriteNow(texture,x,y,width,height,u0,v0,u1,v1,alpha,rgba,corners,additive,angle);return;}
    auto& c=command<SpriteCommand>();
    c.texture=&texture;c.x=x;c.y=y;c.width=width;c.height=height;c.u0=u0;c.v0=v0;c.u1=u1;c.v1=v1;c.alpha=alpha;c.angle=angle;
    c.rgba=rgba;c.hasCorners=corners!=nullptr;if(corners)std::memcpy(c.corners,corners,sizeof c.corners);c.additive=additive;
}
void Renderer::drawScreenFontSprite(const Texture& texture,float x,float y,float width,float height,float u1,float v1,float alpha,uint32_t gradientTop,uint32_t gradientBottom,uint32_t color0,uint32_t color1){
    if(!texture.ready||alpha<=0)return;
    if(!recording()){fontSpriteNow(texture,x,y,width,height,u1,v1,alpha,gradientTop,gradientBottom,color0,color1);return;}
    auto& c=command<FontSpriteCommand>();
    c.texture=&texture;c.x=x;c.y=y;c.width=width;c.height=height;c.u1=u1;c.v1=v1;c.alpha=alpha;
    c.gradientTop=gradientTop;c.gradientBottom=gradientBottom;c.color0=color0;c.color1=color1;
}
void Renderer::drawScreenMaterialQuad(const Texture* texture,const float corners[4][2],const float uv[4][2],const uint32_t vertexColors[4],uint32_t color0,uint32_t color1,float alpha,bool additive){
    if(alpha<=0)return;
    MaterialQuad quad;
    for(unsigned k=0;k<4;++k){
        quad.x[k]=corners[k][0];quad.y[k]=corners[k][1];quad.u[k]=uv[k][0];quad.v[k]=uv[k][1];
        const uint32_t rgba=vertexColors[k];
        quad.color[k]=(rgba&0xffffff00u)|uint32_t(std::lround(std::clamp(alpha,0.f,1.f)*float(rgba&0xff)));
    }
    drawScreenMaterialQuads(texture,&quad,1,color0,color1,-1,-1,additive);
}
void Renderer::drawScreenMaterialQuads(const Texture* texture,const MaterialQuad* quads,unsigned count,uint32_t color0,uint32_t color1,int wrapS,int wrapT,bool additive){
    if((texture&&!texture->ready)||!count)return;
    if(!recording()){materialQuadsNow(texture,quads,count,color0,color1,wrapS,wrapT,additive);return;}
    auto& c=command<MaterialQuadsCommand>();
    c.texture=texture;c.quads=copy(quads,count);c.count=count;c.color0=color0;c.color1=color1;c.wrapS=wrapS;c.wrapT=wrapT;c.additive=additive;
}
void Renderer::drawScreenMaterialQuadsInPlace(const Texture* texture,const MaterialQuad* quads,unsigned count,uint32_t color0,uint32_t color1){
    if((texture&&!texture->ready)||!count||!quads)return;
    if(!recording()){materialQuadsNow(texture,quads,count,color0,color1,-1,-1);return;}
    // The caller keeps quads unchanged until this frame is drawn.
    auto& c=command<MaterialQuadsCommand>();
    c.texture=texture;c.quads=quads;c.count=count;c.color0=color0;c.color1=color1;c.wrapS=-1;c.wrapT=-1;c.additive=false;
}
void Renderer::drawScreenText(const char* text,float x,float y,float size,uint32_t rgba){
    if(!text||size<=0)return;
    if(!recording()){textNow(text,x,y,size,rgba);return;}
    const std::size_t length=std::strlen(text)+1;
    char* copied=static_cast<char*>(data(length));std::memcpy(copied,text,length);
    auto& c=command<TextCommand>();c.text=copied;c.x=x;c.y=y;c.size=size;c.rgba=rgba;
}
}

namespace mkw { void Renderer::setShadowUnion(bool active){
 if(!recording()){shadowUnionNow(active);return;}
 command<ShadowUnionCommand>().active=active;
} }
