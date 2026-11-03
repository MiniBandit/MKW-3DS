#pragma once
#include <3ds.h>
#include <citro3d.h>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <memory>
#include <type_traits>
#include <vector>
#include "mkw/math.hpp"
#include "mkw/effects.hpp"
#include "mkw/visibility.hpp"
#include "mkw/job_worker.hpp"
#include "mkw/render_metrics.hpp"
#include "mkw/course_bloom.hpp"
#include "mkw/display_layout.hpp"
#include "mkw/stereo.hpp"
#include "mkw/water_indirect.hpp"

namespace mkw {
struct UiQuad;
class RaceEffects;
// A second particle pass must not overwrite the scene pass's queued GPU data.
struct EffectLayerBuffer {
    static constexpr unsigned VertexCapacity=8192;
    void* vertices=nullptr;
    C3D_Tex lookup{};
    EffectLayerBuffer()=default;
    EffectLayerBuffer(const EffectLayerBuffer&)=delete;
    EffectLayerBuffer& operator=(const EffectLayerBuffer&)=delete;
    ~EffectLayerBuffer();
    bool init();
};
namespace ef {struct DrawList;}
struct EffectParticle;
// GPU loaders: position float3, normal float3, paired UV float4, color ubyte4.
// Disk records stay GMDL v3; both UV pairs are adjacent and consume 16 bytes.
struct Vertex { Vec3 position,normal; float uv[2],uv1[2]; uint8_t color[4]; };
static_assert(sizeof(Vertex)==44,"GMDL vertex stride");
static_assert(offsetof(Vertex,normal)==12 && offsetof(Vertex,uv)==24 &&
              offsetof(Vertex,uv1)==32 && offsetof(Vertex,color)==40,"PICA attributes consume the entire vertex record");
struct Submesh { uint32_t first,count; int32_t texture; uint32_t flags; int32_t secondary; uint32_t constant; float uvVelocity[4]; uint32_t animationFrames;
    // GMDL v4 only (zero in v3): retail course lighting word, material
    // ambient register and the constant the light-texture albedo stage adds
    // (GX C1), both 0xRRGGBBAA (include/mkw/retail_lighting.hpp).
    uint32_t lighting=0,ambient=0,add=0;
    // GMDL v5: original menu vehicle TEV and EnvCamera SRT. Mode 1 masks
    // the reflection with the albedo alpha; mode 2 uses a GX konst colour;
    // mode 3 adds C1 to the albedo without a reflection texture. World items
    // also use mode 4 (banana's raster lerp) and 6 (shell's raster-alpha mask).
    // Mode 7 keeps a course object's two colour-ramp endpoints and raster light.
    // Mode 8 is the three-map marble statue: reflectionConstant packs the
    // third texture index+1 (low 16 bits), wrap S/T (bits 16/18, two each).
    // Mode 9 is a two-map world statue with the source final-stage scale.
    uint32_t menuReflection=0,reflectionConstant=0,reflectionAdd=0;
    float reflectionUv[6]{}; };
static_assert(offsetof(Submesh,menuReflection)==56&&sizeof(Submesh)==92,"GMDL v5 submesh stride");
struct CourseLighting;
struct RetailFog;
struct MaterialSampler {float lodBias=0;uint8_t minFilter=0,magFilter=0,valid=0,reserved=0;};
static_assert(sizeof(MaterialSampler)==8);
class Texture {
public:
    C3D_Tex gpu{};
    bool ready=false;
    C3D_Tex noMipSampler{}; // diagnostic view; never owns/frees its data
    std::array<C3D_Tex,3> materialSamplers{}; // one view per texture unit
    C3D_Tex* sampledGpu();
    C3D_Tex* sampledGpu(const MaterialSampler* sampler,unsigned unit=0);
    ~Texture();
    void reset();
    // Reads the whole .t3x in one FS request. Safe on the asset loader
    // thread: it allocates LINEAR memory and flushes, but touches no GPU state.
    bool load(const char* path);
    // The same texture in VRAM: citro3d decodes into a temporary LINEAR buffer and copies
    // it with a GX transfer, so LINEAR is unchanged afterwards. Main thread only, while no
    // frame is recorded or replayed (startup).
    bool loadVram(const char* path);
    bool loadMemory(const void* data,size_t size,bool vram=false);
    uint32_t bytes() const;
    // GMV movie frames are already GPU ETC1 payloads. This keeps the opening
    // preview on the same native presentation path as the 3DS renderer.
    bool initMovieEtc1(std::uint16_t width, std::uint16_t height);
    bool initRgba8(unsigned width,unsigned height,const std::uint8_t* pixels,std::size_t bytes);
};
class Model {
public:
    bool roadSamplerPolicy=false; // executable-only static-road sampler ranges
    int stadiumTexture=-1; // WLscreenGC/dkmonitor/fks_screen_wii wl_screen1 replacement.
    Vertex* vertices=nullptr;
    // Lit GPU skinning keeps the original normal/UV attributes intact.
    // One immutable float (3 * palette index) per vertex, allocated on load.
    float* skinBoneIndices=nullptr;
    uint16_t* indices=nullptr;
    uint32_t vertexCount=0,indexCount=0;
    std::vector<Submesh> submeshes;
    std::vector<std::array<MaterialSampler,3>> samplers;
    std::vector<std::shared_ptr<Texture>> textures;
    std::vector<WaterIndirectField> waterIndirect;
    struct UvTrack {uint32_t frames=0,loop=0;std::vector<float> samples;};
    std::vector<UvTrack> uvTracks;
    Vec3 minimum{},maximum{};
    // Opt-in for immutable geometry only; dynamic skinning must never use stale bounds.
    std::vector<SubmeshVisibility> visibility;
    // Track-only fine immutable bounds; built at load, never in a frame.
    std::vector<SubmeshVisibility> contactVisibility,contactCoarseVisibility;
    uint16_t* contactIndices=nullptr;
    void enableStaticCulling(bool contact=false);
    // Immutable checkpoint-selected terrain index copies, original kept whole.
    std::array<uint16_t*,2> regionIndices{};
    std::array<std::vector<SubmeshVisibility>,2> regionVisibility;
    bool enableCastleRegions();
    std::vector<std::vector<SkinVisibilityBlock>> skinVisibility;
    void enableSkinCulling(unsigned bones);
    bool fixedLighting=false,staticLightingBaked=false;
    uint32_t sourceVertexHash=0;
    float* staticEnvironment2=nullptr;
    static void setLightingCourse(const CourseLighting* course);
    void bakeStaticLighting(const CourseLighting* course);
    void bakeStaticLighting();
    // Compatibility helper; load-time static baking already covers ordinary models.
    void freezeLighting(const float* palette=nullptr,unsigned stride=21);
    void bakeEnvironment(const float* palette=nullptr,unsigned stride=21,Vec3 referenceUp={0,.948683298f,.316227766f});
    // Use the original GPU lighting/EnvCamera path and the throttled light maps.
    bool dynamicLightTextures=false;
    bool nativeMiiLighting=false;
    ~Model();
    bool load(const char* path,bool dynamicLighting=false);
};
struct Instance { Model* model=nullptr; C3D_Mtx transform; };
// One run of a submesh's triangles that all follow palette matrix `matrix`.
// bounds: the part's bone-local vertex bounds (drawNow culls parts by them).
struct RigidPart { uint32_t first,count,matrix; Bounds bounds{}; };
// A rigidly skinned pose: vertices are bone-local, each part is drawn with
// its palette matrix (row-major 3x4 affine then 3x3 normal, 21 floats) and
// environment-mapped parts derive UV1 on the GPU from that normal matrix.
// `matrices` is the palette's matrix count (0: the highest part index + 1).
struct RigidPose { const std::vector<std::vector<RigidPart>>* parts=nullptr; const float* palette=nullptr; float environmentYaw=0; unsigned stride=21,matrices=0; };
// Per-submesh material animation for one draw: affine rows (a b tx / c d ty)
// for both texture coordinates, texture/TEV-constant overrides, an RGBA
// multiplier on the vertex colour and an additive colour (0xAABBGGRR).
// Alpha: tint[3] is the submesh's whole alpha (ObjectBank::markRegisterAlpha).
struct SubmeshOverride {
    enum : uint32_t { Constant=1, Tint=2, Alpha=4, Add=8, Uv=16, Texture=32 };
    float uv[12]; float tint[4]; int32_t texture[2]; uint32_t constant,add,flags;
};
class Scene {
public:
    Vec3 spawn{},spawnRotation{};
    std::vector<Instance> instances;
    std::vector<Model*> models;
    ~Scene();
    bool load(const char* path);
};
// The frame's camera, copied for culling work off the rendering thread.
struct ViewFrustum {
    C3D_Mtx view,projection;
    ClipFrustum model(const C3D_Mtx& transform) const;
    bool visible(const Model& model,const C3D_Mtx& transform) const {return this->model(transform).intersects({model.minimum,model.maximum});}
    // Renderer::cameraFacing for this camera.
    C3D_Mtx cameraFacing(Vec3 position,float scale) const;
};
// CPU writes to memory the GPU reads (model vertices, the submesh textures a
// draw binds). While the render thread draws, the frame being recorded may
// not touch them: the GPU can still be reading the previous frame's. Such a
// writer copies what it needs into allocate()d storage and queues the write,
// which the render thread runs after that frame's FrameBegin (every earlier
// frame finished) and before the frame's draws, in queue order per thread.
// deferredWrites() is null when writes may run at once (immediate frames).
// Safe on job threads while a frame records.
class DeferredWrites {
public:
    void* allocate(std::size_t bytes);
    void queue(void (*run)(void*),void* payload);
    struct Write {void (*run)(void*);void* payload;};
private:
    friend class Renderer;
    struct Storage;
    Storage* storage_=nullptr;
};
DeferredWrites* deferredWrites();
class Renderer {
public:
    Renderer();
    ~Renderer();
    bool init();
    void shutdown();
    // Render thread (second core): deferred frames record their draws on the
    // calling thread and are drawn there one frame later. Without it every
    // frame is immediate.
    bool startRenderThread();
    // Deferred frames: every call between begin() and end() (and the state
    // calls before begin) records by value; nothing referenced may change
    // until the frame is drawn except through deferredWrites(). Switching
    // finishes all recorded frames. Menus stay immediate.
    void setDeferred(bool on);
    bool deferred() const {return deferred_;}
    // Waits until the render thread has drawn every recorded frame.
    void drain();
    // Race simulation may continue while both recording lists are occupied.
    bool canRecordFrame() const;
    // Race bottom screen (docs/BOTTOM_SCREEN.md): a 320x240 RGBA8 VRAM target
    // with depth/stencil for menu icons and dual-screen intros; transferred only when drawn.
    // Create and release with the render thread idle, outside a frame:
    // releaseBottom() waits for the GPU, deletes the target and blanks both
    // lower LCD framebuffers, so menus keep their black lower screen.
    bool enableBottom();
    void releaseBottom();
    bool bottomEnabled() const {return bottomTarget_!=nullptr;}
    // Later sprites, font sprites and material quads of this frame go to the
    // bottom screen (2D: no parallax; drawn once, with the last eye), at most
    // BottomQuadCapacity of them; the top screen keeps all of its own.
    void beginBottom();
    static constexpr int BottomQuadCapacity=512;
    // Before begin(): replay the recorded 3D scene over a continuous 400x560
    // virtual view with an 80px hidden hinge (bottom uses central 320 columns). The simulation
    // and effect-list generation still run once. End the world section before
    // drawing the intro title; later screen fades also cover the bottom LCD.
    void setDualScreenIntro(bool on){dualScreenIntro_=on;}
    void markIntroWorldEnd();
    // Creates bloom's VRAM targets now (render thread idle) so VRAM loaded
    // afterwards can never take their place; its LINEAR vertices still wait
    // for the first requestBloom(). For races that request bloom. False if
    // the targets cannot exist.
    bool reserveBloom();
    bool configureStadiumScreen(bool enabled);
    // Optional conservative world bounds of the display faces. If all are
    // outside both eyes, retain the last capture without any GPU copy.
    void requestStadiumScreen(const Bounds* screens=nullptr,unsigned count=0);
    // A t3x (one uncompressed subtexture, no mipmaps, power-of-two sides of 8
    // to 1024) uploaded to VRAM in chunks through an idle LINEAR staging
    // region: no temporary LINEAR or heap allocation. On failure the texture
    // is left empty. Main thread, render thread idle (it waits for the GPU).
    bool loadVramTexture(Texture& texture,const char* path,GPU_TEXTURE_WRAP_PARAM wrap=GPU_CLAMP_TO_EDGE);
    // Moves a loaded single-level LINEAR texture into VRAM (a GPU copy, then
    // its LINEAR block is freed), unless that would leave under `reserve`
    // bytes of VRAM free. False leaves it untouched in LINEAR. Main thread;
    // call waitForGpu() first: no recorded frame may still sample it.
    bool moveToVram(Texture& texture,std::size_t reserve);
    void clearUnusedTextureCache();
    // Index of the frame being recorded: storage a deferred frame references
    // in place alternates on its parity (two frames are in flight).
    uint32_t recordingFrame() const {return submitted_.load(std::memory_order_relaxed);}
    // Outside a frame, drain submitted GPU work before retiring its resources.
    void waitForGpu();
    void setFov(float degrees,float nearPlane=10.f,float farPlane=400000.f);
    void setCourseLighting(bool enabled);
    void setSceneRegion(unsigned mask);
    unsigned sceneRegion()const{return recordSceneRegion_;}
    // Retail course lighting (src/renderer_retail.cpp): EGG lights, light
    // textures and fog for course draws whose submeshes carry GMDL v4 state.
    // The course table outlives the renderer's use; nullptr turns it off.
    void setRetailLighting(const CourseLighting* course,bool fixedLightTextures=false);
    bool retailLightingReady(const CourseLighting* course) const {return retail_==course;}
    // Current course fog object (RetailFogState::value), once per frame.
    void setRetailFog(const RetailFog& fog);
    float fov() const { return fov_; }
    // Stereoscopic 3D (include/mkw/stereo.hpp, docs/STEREO_3D.md): New 3DS
    // models with a 3D screen only, driven by the 3D slider. The slider at
    // zero draws exactly the 2D frame. Old 3DS and New 2DS XL never draw a
    // second eye or allocate its target.
    bool stereoCapable() const {return targetRight_!=nullptr;}
    // The next begin()'s stereo camera (one of Mario Kart 7's, stereo.hpp)
    // and the view depth of its subject: the player's kart along the chase
    // camera, or the menu models. Without a call, the race camera with the
    // camera target's distance.
    void setStereoSubject(const stereo::Scene& scene,float viewDepth){stereoScene_=scene;stereoSubject_=viewDepth;stereoSubjectSet_=true;}
    // The frame being recorded (inactive in 2D frames).
    const stereo::Frame& stereoFrame() const {return frameStereo_;}
    // Disparity of a point at this view depth in the frame being recorded.
    float stereoParallax(float viewDepth) const {return frameStereo_.parallax(viewDepth);}
    // Disparity of a screen layer `depth` pixels behind the screen plane
    // (stereo::HudLayer, CountdownLayer, PaneLayer) in this frame.
    float stereoLayer(float depth) const {return frameStereo_.active()?stereo::layerParallax(frameSlider_,stereoDisplay_,depth):0.f;}
    // Disparity in pixels (negative: in front of the screen) of later
    // screen-space sprites, text and menu icons, until the frame ends or the
    // next call. Fades, cinema bars and bloom stay on the screen plane.
    // Ignored in 2D frames, which therefore record nothing.
    void setScreenDepth(float parallax);
    // QA: a fixed 3D slider value (0..1) instead of the hardware slider; < 0 restores it.
    void setSliderOverride(float slider){sliderOverride_=slider;}
    // Whether the next begin() draws both eyes: a 3D screen with the slider
    // (or its QA override) above zero.
    bool stereoWanted() const {return targetRight_&&slider()>0;}
    Vec3 cameraBackward() const { return {mainView_.r[2].x,mainView_.r[2].y,mainView_.r[2].z}; }
    // World point -> top-screen pixels with the active camera; viewZ < 0 is in front.
    bool projectTop(Vec3 world,float& x,float& y,float& viewZ) const;
    bool projectChaseTop(Vec3 world,float& x,float& y,float& viewZ,bool lookBack=false) const;
    void begin(Vec3 eye,Vec3 target,Vec3 up,bool mirror=false);
    void beginSourceCamera(const float* view,const float* projection);
    // Called per submesh after its default textures and fixed-function state
    // are bound: the binder configures every combiner and may override any of it.
    // In a deferred frame it runs on the render thread with a copy of its context.
    using MaterialBinder = void (*)(void* context,std::size_t submesh);
    using ShapeVisible = bool (*)(void* context,std::size_t submesh);
    ClipFrustum modelFrustum(const C3D_Mtx& transform) const;
    // Culling covers both eyes in stereo frames (stereo::cullingUnion).
    ViewFrustum viewFrustum() const {return {mainView_,cullProjection_};}
    bool visible(const Model& model,const C3D_Mtx& transform) const;
    void draw(const Model& model,const C3D_Mtx& transform,bool backdrop=false){drawWith(model,transform,backdrop,nullptr,nullptr,0,nullptr);}
    // The binder context is copied (a deferred frame draws later); it must not
    // point at anything that changes before the frame is drawn.
    template<class Context> void draw(const Model& model,const C3D_Mtx& transform,bool backdrop,MaterialBinder material,const Context& context,ShapeVisible visible=nullptr){
        static_assert(std::is_trivially_copyable_v<Context>,"binder contexts are copied");
        drawWith(model,transform,backdrop,material,&context,sizeof(Context),visible);
    }
    // GX EnvCamera texture coordinates for UV1 (texture unit 1) from the view-space
    // normals of `transform`, followed by the material SRT as GX rows
    // {a,b,tx,c,d,ty}. Stays set for later draws until clearEnvironmentUv().
    // texture2 keeps UV0/UV1 independent and writes EnvCamera to unit 2.
    void setBakedEnvironmentUv(); // render-thread material binder; reads baked UV2
    void setEnvironmentUv(const C3D_Mtx& transform,const float (&uv)[6],bool texture2=false,float blend=1.f);
    // Replaces only UV1's SRT (GX rows); environment mapping state is unchanged.
    void setUv1Transform(const float (&uv)[6]);
    // Offset added to UV0 and UV1, replacing the submesh's authored scroll.
    void setUvOffset(float u,float v);
    // Material-binder only: use the active render eye, including Mirror winding.
    void setMaterialCullNow(GPU_CULLMODE mode);
    void clearEnvironmentUv();
    // While set, draw() renders each submesh's rigid parts with their bone.
    void setRigidPose(const RigidPose* pose);
    // While set, draw() skins on the GPU: the model holds bone-local positions
    // with 3 * bone index in normal.x (DriverAnimation/ObjectBank::gpuSkin);
    // palette rows are the bones' 3x4 matrices, `stride` floats per bone.
    static constexpr unsigned MaxSkinBones=19;
    void setSkinPalette(const float* palette,unsigned bones,unsigned stride);
    // While set, draw() applies these to submeshes [0,count).
    void setOverrides(const SubmeshOverride* overrides,unsigned count);
    // Course-object -shadow meshes are closed shadow volumes. z-fail stencil
    // counting marks the geometry drawn so far inside a volume, which is then
    // multiplied by darkness once. Call after everything that receives shadows.
    struct ShadowVolume {const Model* model;C3D_Mtx transform;};
    void drawShadowVolumes(const ShadowVolume* volumes,unsigned count,float darkness);
    struct ShadowCaster {const Model* model=nullptr;C3D_Mtx transform;const float* palette=nullptr;unsigned bones=0,stride=21,first=0,count=0;bool wheelContact=false,softTerrain=false,movingSupport=false,hideContact=false;Vec3 contactCenter{},contactNormal{0,1,0};float contactRadius=0,contactSpread=1.3f,contactHalfWidth=0;};
    // A solid receiver from the current course-object draw, including its
    // exact transform/pose. Rigid parts arrive as individual index ranges.
    struct ShadowReceiver {const Model* model=nullptr;C3D_Mtx transform;unsigned first=0,count=0,lighting=0;const float* palette=nullptr;unsigned bones=0;Bounds bounds;};
    // Allocate/release while the rendering thread is idle. Only the local
    // racer is captured; the same completed texture is shared by both eyes.
    bool configurePlayerShadow(bool enabled);
    bool playerShadowReady()const{return playerShadowTarget_!=nullptr;}
    void drawPlayerShadow(const ShadowCaster* casters,unsigned count,const Model* const* receivers,unsigned receiverCount,Vec3 origin,Vec3 lightDirection,float scale,const ShadowVolume* fallback=nullptr,const ShadowReceiver* objects=nullptr,unsigned objectCount=0,bool actor=false);

    // Translate the rendered image in LCD pixels after perspective projection.
    // Wii menu models are rendered with a centered camera into a texture before
    // their UI pane moves; world translation would change the viewing angle.
    void drawAtScreen(const Model& model,const C3D_Mtx& transform,float x,float y,float brightness=1.f,
                      float scaleX=1.f,float scaleY=1.f);
    template<class Context> void drawAtScreen(const Model& model,const C3D_Mtx& transform,float x,float y,float brightness,
                                              float scaleX,float scaleY,MaterialBinder material,const Context& context){
        static_assert(std::is_trivially_copyable_v<Context>,"binder contexts are copied");
        drawAtScreenWith(model,transform,x,y,brightness,scaleX,scaleY,material,&context,sizeof(Context));
    }
    // Native MenuKartModel icon: centered 192x44 crop of the source camera,
    // projected into its authored pane. Restores the current scene camera.
    void drawMenuIcon(const Model&,const C3D_Mtx&,const UiQuad&);
    // Full-frame menu-model layer over a race/demo camera; keeps model depth
    // internally but composites in front of the previously rendered world.
    void drawMenuModelLayer(const Model&,const C3D_Mtx&);
    // Coin Runners' group2: fixed native orthographic HUD camera. The first
    // column starts a fresh depth layer after the last world draw.
    void drawCoinHudModel(const Model&,const C3D_Mtx&,bool firstColumn);
    // Billboard in the active camera's basis. Positive forwardOffset moves
    // toward the camera's viewing direction; upOffset uses camera up.
    void drawCameraFacing(const Model& model,Vec3 anchor,float upOffset,float forwardOffset,float scale);
    // The same camera-facing basis as a transform (local +Z toward the viewer).
    C3D_Mtx cameraFacing(Vec3 position,float scale) const;
    // Camera position in world space for the current frame.
    Vec3 cameraPosition() const;
    float cameraFov() const {return fov_;}
    void end();
    // Course bloom (posteffect.bblm) for the next frame only: the 3D scene renders
    // off screen; applyBloom() composites it with blur(max(scene-threshold,0))
    // *tint*intensity, after which draws (HUD) go straight to the top screen.
    // CourseBloom is a generated static resource; it outlives deferred frames.
    void requestBloom(const CourseBloom& settings);
    void applyBloom();
    // Live sparks and effect particles are copied when recorded; their quads
    // are built where the frame is drawn. Specs and textures must outlive it.
    void drawSparks(const DriftEffects& effects,const Texture* textures);
    // RKRace effects through the NW4R EF draw strategies (src/nw4r_ef_draw.cpp). Drawing
    // advances the strategies' tracked axes, as EF does.
    void drawEffects(RaceEffects& effects,const Texture* textures);
    void drawSceneEffects(RaceEffects&,const Texture*,EffectLayerBuffer&);
    // Immediate ceremony UI layer, with the same camera/depth as its trophy.
    void drawMenuEffects(RaceEffects&,const Texture*,EffectLayerBuffer&);
    void drawCinemaBars(float topHeight,float bottomHeight,float opacity);
    void drawScreenFade(float opacity,bool white);
    void setShadowUnion(bool active);
    // Screen-space sprite in top-screen pixels (origin top-left), texture
    // region [0,u1]x[0,v1] modulated by alpha and RRGGBBAA tint.
    // Up to kSpriteCapacity per frame; each submitted quad survives to frame end.
    void drawScreenSprite(const Texture& texture,float x,float y,float width,float height,float u1,float v1,float alpha=1.f,uint32_t rgba=0xffffffff);
    // A clipped source pane uses a subrectangle of its authored texture.
    // angle (radians, screen y-down) turns the quad about its centre.
    void drawScreenSpriteRegion(const Texture& texture,float x,float y,float width,float height,float u0,float v0,float u1,float v1,float alpha=1.f,uint32_t rgba=0xffffffff,const uint32_t* corners=nullptr,bool additive=false,float angle=0.f);
    // NW4R font colour remap: (C0 + texture * (C1-C0)) * vertex gradient.
    // Every colour is RRGGBBAA, matching drawScreenSprite and drawScreenText.
    void drawScreenFontSprite(const Texture& texture,float x,float y,float width,float height,float u1,float v1,float alpha,uint32_t gradientTop,uint32_t gradientBottom,uint32_t color0,uint32_t color1);
    // NW4R default material (no TEV stages) on the top screen: lerp(color0,
    // color1, texture) times the vertex colours; without a texture, color1
    // times them. Corners TL, TR, BL, BR in pixels; uv per corner with v=0 the
    // top image row, so pane texture SRT and wrapping carry over unchanged.
    void drawScreenMaterialQuad(const Texture* texture,const float corners[4][2],const float uv[4][2],const uint32_t vertexColors[4],uint32_t color0,uint32_t color1,float alpha,bool additive=false);
    // The same material for many quads in one draw (corners TL, TR, BL, BR;
    // colours rgba with the pane alpha already applied).
    struct MaterialQuad {float x[4],y[4],u[4],v[4];uint32_t color[4];};
    // wrapS/wrapT (GPU_TEXTURE_WRAP_PARAM, -1 keeps the texture's) apply where the quads are drawn.
    void drawScreenMaterialQuads(const Texture* texture,const MaterialQuad* quads,unsigned count,uint32_t color0,uint32_t color1,int wrapS=-1,int wrapT=-1,bool additive=false);
    // The same without copying the quads: a deferred frame reads them when it
    // is drawn, so they must not change until then (alternate storage on
    // recordingFrame()'s parity). Keeps per-frame storage fixed and preallocated.
    void drawScreenMaterialQuadsInPlace(const Texture* texture,const MaterialQuad* quads,unsigned count,uint32_t color0,uint32_t color1);
    // Small native HUD font; geometry is batched and remains alive for the frame.
    void drawScreenText(const char* text,float x,float y,float scale,uint32_t rgba=0xffffffff);
    // Call after end(). Waits for the finished frame and copies the top-screen
    // render target to rgb (400*240*3 bytes, rows top to bottom). Test capture
    // only. eye 1 is the right eye of a stereo frame (false otherwise).
    bool captureTop(uint8_t* rgb,unsigned eye=0);
    // The same for the bottom screen's target (320*240*3 bytes); false without one.
    bool captureBottom(uint8_t* rgb);
    bool copyPresentedBottom(uint8_t* bgr);
    // Whether the last finished frame showed both eyes (QA captures).
    bool stereoShown() const {return stereoShown_;}
    // QA, after captureTop of a stereo frame: whether the LCD framebuffer
    // that frame was presented in holds this eye's image (rgb as captureTop).
    bool presentedMatches(const uint8_t* rgb,unsigned eye);
    // Keeps a GPU resource alive until every frame recorded before this call
    // has been drawn (or waitForGpu), when no command list can still sample it.
    template<class T> void retire(std::unique_ptr<T> object){if(object)retireShared(std::shared_ptr<void>(std::move(object)));}
    template<class T> void retire(std::shared_ptr<T> object){if(object)retireShared(std::shared_ptr<void>(std::move(object)));}
    // The last drawn frame's counts (a deferred frame's arrive one frame later).
    uint32_t triangles=0,drawCalls=0;
    uint32_t culledTriangles=0,culledSubmeshes=0;
    float previousGpuMs=0,beginWaitMs=0,commandUsage=0;
    // Render thread time for the last drawn frame (excluding its FrameBegin
    // wait, beginWaitMs) and this thread's wait for a free command list.
    float renderThreadMs=0,recordWaitMs=0;
    // Menus/QA synchronize here. Racing waits for VBlank before polling input.
    bool loadingFrame=false; // first race renders complete under the loading cover
    bool frameSync=true;
    uint32_t animationFrame=0;
    // RRGGBB00 added after the model's material colour, preserving texture alpha.
    // Zero outside Star racer draws; no model/texture mutation is involved.
    static constexpr uint32_t InkBlack=255u; // low byte: ink coverage, original alpha preserved
    uint32_t additiveColor=0;
private:
    // Recording (src/render_thread.cpp).
    struct FrameList;
    struct Stats {uint32_t triangles=0,drawCalls=0,culledTriangles=0,culledSubmeshes=0;float previousGpuMs=0,beginWaitMs=0,commandUsage=0,renderMs=0;RenderMetrics metrics{};};
    bool recording() const;
    // The 3D slider's position (0..1), or its QA override.
    float slider() const;
    // Immediate frames in stereo (menus): recorded between begin() and end()
    // on this thread, then drawn once per eye at end().
    bool localRecording_=false,localReplay_=false;
    std::unique_ptr<FrameList> localList_;
    void endLocal();
    void* command(uint16_t type,std::size_t bytes);
    template<class T> T& command(){return *static_cast<T*>(command(T::Type,sizeof(T)));}
    void* data(std::size_t bytes);
    DeferredWrites* writes();
    friend DeferredWrites* deferredWrites();
    template<class T> T* copy(const T* source,std::size_t count){if(!source||!count)return nullptr;auto* out=static_cast<T*>(data(sizeof(T)*count));std::memcpy(static_cast<void*>(out),source,sizeof(T)*count);return out;}
    FrameList* acquire();
    void submit();
    void execute(FrameList& list);
    void runWrites(FrameList& list);
    static void threadEntry(void* self);
    static void aptEvent(APT_HookType type,void* self);
    aptHookCookie aptCookie_{};
    void threadLoop();
    void publish();
    void retireShared(std::shared_ptr<void> object);
    void releaseRetired();
    std::unique_ptr<FrameList> lists_[2];
    FrameList* recording_=nullptr;
    Thread thread_=nullptr;
    LightEvent wake_{},done_{};
    std::atomic<unsigned> submitted_{0},completed_{0},gpuDone_{0};
    std::atomic<bool> quit_{false};
    std::atomic<FrameList*> writeTarget_{nullptr};
    bool deferred_=false;
    // No recorded frame since the last waitForGpu() (this thread's view).
    bool gpuIdle_=true;
    uint32_t fpscr_=0;
    struct Retired {unsigned frame;std::shared_ptr<void> object;};
    std::vector<Retired> retired_;
    // Frame stats published by the render thread (Stats of frame completed_-1).
    Stats published_{};
    LightLock publishLock_{};
    // This thread's camera (queries) and the drawing camera, equal in
    // immediate frames.
    C3D_Mtx mainView_{},mainProjection_{},cullProjection_{},chaseView_{};
    float mainScreenWidth_=400.f;
    bool dualScreenIntro_=false,frameDualScreenIntro_=false;
    bool execLoadingFrame_=false;
    // Stereo: the console's display settings (read once on a 3D screen) and
    // the recording side's frame, slider and next subject.
    stereo::Display stereoDisplay_{};
    stereo::Frame frameStereo_{};
    stereo::Scene stereoScene_=stereo::Race;
    float frameSlider_=0,stereoSubject_=0,sliderOverride_=-1;
    bool stereoSubjectSet_=false;
    void beginStereo(float targetDistance);
    void beginRecorded();
    // Drawing side: the frame being drawn, the eye (0 left / 2D, 1 right) and
    // whether the screen currently shows both eyes.
    stereo::Frame execStereo_{};
    unsigned eye_=0;
    bool stereoShown_=false;
    bool execDualScreenIntro_=false,bottomWorldPass_=false;
    // The top-screen framebuffers the last stereo frame was transferred to.
    const uint8_t* presented_[2]={nullptr,nullptr};
    const uint8_t* presentedBottom_=nullptr;
    float screenParallax_=0;
    // Clip-space sign of screen-right on the tilted projections' row 1.
    float horizontalSign_=-1.f,verticalSign_=1.f;
    C3D_Mtx eyeProjection(const C3D_Mtx& center) const;
    C3D_Mtx screenProjection(float parallax) const;
    C3D_RenderTarget* topTarget() const {return eye_?targetRight_:target_;}
    void beginEyeNow(const C3D_Mtx& view,const C3D_Mtx& projection);
    void beginBottomWorldNow(const C3D_Mtx& view,const C3D_Mtx& projection);
    void endEyeNow();
    void screenDepthNow(float parallax);
    // Drawing (the render thread in deferred frames).
    void beginFrame();
    // begun: C3D_FrameBegin already ran (stereo immediate frames begin at once).
    void beginNow(const C3D_Mtx& view,const C3D_Mtx& projection,bool sync,bool begun=false);
    void endNow();
    void drawNow(const Model& model,const Vertex* vertices,const C3D_Mtx& transform,bool backdrop,MaterialBinder material,void* context,ShapeVisible visible);
    void drawWith(const Model& model,const C3D_Mtx& transform,bool backdrop,MaterialBinder material,const void* context,std::size_t contextSize,ShapeVisible visible);
    void drawAtScreenWith(const Model&,const C3D_Mtx&,float x,float y,float brightness,float scaleX,float scaleY,MaterialBinder,const void*,std::size_t);
    void drawAtScreenNow(const Model& model,const Vertex* vertices,const C3D_Mtx& transform,float x,float y,float brightness,float scaleX,float scaleY,MaterialBinder material=nullptr,void* context=nullptr);
    void drawMenuModelLayerNow(const Model&,const Vertex*,const C3D_Mtx&);
    void drawCoinHudModelNow(const Model&,const Vertex*,const C3D_Mtx&,bool firstColumn);
    void drawMenuIconNow(const Model&,const Vertex* vertices,const C3D_Mtx&,const UiQuad&);
    void drawSparksNow(const SparkSpec* specs,unsigned specCount,const Spark* sparks,unsigned count,const Texture* textures);
    // NW4R EF: materials/texture sizes follow the effect resources (rebuilt with the
    // render thread idle); the draw list is built where the frame is recorded and
    // submitted where it is drawn.
    void updateEffectState(const RaceEffects& effects);
    void buildEffectList(RaceEffects& effects,const C3D_Mtx& view,ef::DrawList& list);
    void effectListNow(const RaceEffects& effects,const ef::DrawList& list,const Texture* textures);
    void drawSceneEffectsImmediate(RaceEffects&,const Texture*,const EffectLayerBuffer&);
    void sceneEffectListNow(const RaceEffects&,const ef::DrawList&,const Texture*,const EffectLayerBuffer&);
    void drawEffectsImmediate(RaceEffects& effects,const Texture* textures);
    void requestBloomNow(const CourseBloom& settings);
    void applyBloomNow();
    void shadowVolumesNow(const ShadowVolume* volumes,unsigned count,float darkness,uint8_t excludedStencil=0);
    void playerShadowNow(const ShadowCaster*,unsigned,const Model* const*,unsigned,Vec3,Vec3,float,const ShadowVolume*,const ShadowReceiver*,unsigned,bool);
    C3D_Tex playerShadowTex_{},playerShadowClipTex_{};
    Texture playerContactTex_,playerShadowDepthTex_,playerShadowFadeTex_;
    int shadowReceiverFadeLoc_=-1;
    int playerShadowProjectLoc_=-1,shadowCaptureLoc_=-1,shadowDepthLoc_=-1,shadowFloorLoc_=-1;
    C3D_RenderTarget* playerShadowTarget_=nullptr;
    C3D_Tex actorShadowTex_{};
    C3D_RenderTarget* actorShadowTarget_=nullptr;
    Vec3 playerShadowGround_{},playerShadowGroundNormal_{0,1,0};
    bool playerShadowReceiver_=false,playerShadowBuried_=false;
    unsigned playerShadowBuriedEnter_=0,playerShadowBuriedExit_=0;
    Vec3 playerShadowLastOrigin_{};
    float playerShadowRoadOpacity_=1.f;

    void environmentUvNow(const C3D_Mtx& transform,const float (&uv)[6],bool texture2=false,float blend=1.f);
    void normalMatrixNow(const C3D_Mtx& modelView,bool environment=false);
    void uv1TransformNow(const float (&uv)[6]);
    void clearEnvironmentUvNow();
    void cinemaBarsNow(float topHeight,float bottomHeight,float opacity);
    bool shadowUnion_=false;
    void shadowUnionNow(bool active);
    void screenFadeNow(float opacity,bool white,bool depthOnly=false,int shadowMode=0);
    void spriteNow(const Texture& texture,float x,float y,float width,float height,float u0,float v0,float u1,float v1,float alpha,uint32_t rgba,const uint32_t* corners,bool additive,float angle);
    void fontSpriteNow(const Texture& texture,float x,float y,float width,float height,float u1,float v1,float alpha,uint32_t gradientTop,uint32_t gradientBottom,uint32_t color0,uint32_t color1);
    void materialQuadsNow(const Texture* texture,const MaterialQuad* quads,unsigned count,uint32_t color0,uint32_t color1,int wrapS,int wrapT,bool additive=false);
    void textNow(const char* text,float x,float y,float size,uint32_t rgba);
    Vertex* reserveOverlay(int count);
    unsigned recordSceneRegion_=3,execSceneRegion_=3;
    Stats stats_{};
    uint32_t execFrame_=0,execAdditive_=0;
    C3D_RenderTarget* target_=nullptr;
    // Right eye (VRAM, New 3DS with a 3D screen only; linked to GFX_RIGHT).
    C3D_RenderTarget* targetRight_=nullptr;
    C3D_RenderTarget* bottomTarget_=nullptr;
    float screenWidth_=400.f;
    DVLB_s* shader_=nullptr;
    shaderProgram_s program_{};
    shaderProgram_s miiProgram_{};bool miiProgramReady_=false;
    C3D_Mtx view_,projection_;
    float fov_=60.f;
    Vec3 light_{};
    bool courseLighting_=false;
    float modelBrightness_=1.f;
    float menuDepthBase_=-1.f,menuDepthSpan_=1.f;
    bool menuIconLayer_=false,menuIconAdditive_=false;
    float menuIconAlpha_=1;
    uint32_t menuIconColor0_=0,menuIconColor1_=0xffffffff;
    unsigned menuIconDrawCount_=0;
    int projectionLoc_=-1,modelViewLoc_=-1,lightLoc_=-1,uvScrollLoc_=-1;
    int envRowLoc_[3]={-1,-1,-1},envParamLoc_=-1,uv1RowLoc_[2]={-1,-1};
    int normalRowLoc_[3]={-1,-1,-1};
    int uvRowLoc_[4]={-1,-1,-1,-1},tintLoc_=-1;
    bool uvDirty_=false,tintDirty_=false;
    void resetMaterialUniforms();
    // Retail course lighting (renderer_retail.cpp).
    const CourseLighting* retail_=nullptr;
    bool retailLightViewReady_[16]{};
    Vec3 retailLightView_[16]{};
    C3D_Tex retailTexture_[2]{},fixedRetailTexture_[2]{},retailFogRamp_{};
    bool drawSampledLighting_=false;
    uint8_t lightTextureFrom_[2][32*32*4]{},lightTextureTo_[2][32*32*4]{};
    unsigned lightTextureTick_=0,lightTextureAppliedTick_=~0u;
    bool lightTextureTransition_[2]{};
    int retailTextureKeys_[2][12]{}; // last generated GX direction matrices
    bool retailTextureDynamic_[2]{};
    float retailFog_[8]{};      // type, enabled, start, end, r, g, b, ramp type
    // Light textures generated; light uniforms, light-texture state or unit 2 to restore after a submesh.
    bool fixedLightTextures_=false;
    bool retailTexturesValid_=false,retailLightDirty_=false,retailTextureDirty_=false,retailFogBound_=false;
    int lightDiffLoc_=-1,lightDirLoc_=-1,lightFnLoc_=-1,fogParamLoc_=-1,retailBoolLoc_=-1;
    int environmentMappedLoc_=-1,environmentTexture2Loc_=-1,environmentPrimaryLoc_=-1,diffuseLightingLoc_=-1,projectedWaterLoc_=-1,staticLightingLoc_=-1;
    bool retailUniformLocations();
    void retailBeginFrame();
    void retailShutdown();
    void retailFogNow(const RetailFog& fog);
    // Light uniforms of one submesh; false leaves the legacy path to the caller.
    bool retailLight(const Submesh& s);
    // Combiners/textures of a retail submesh after the generic setup; returns
    // true when it owns the environment rows (light textures).
    bool retailCombiners(const Submesh& s,const C3D_Mtx& modelView,bool backdrop,bool additive,uint32_t additiveRgb,uint32_t constant);
    void retailEnvironment(const C3D_Mtx& modelView);
    void retailEndSubmesh();
    const RigidPose* rigid_=nullptr;
    const float* skinPalette_=nullptr;
    unsigned skinBones_=0,skinStride_=12;
    int bonesLoc_=-1,skinnedLoc_=-1,skinnedNormalsLoc_=-1;
    const SubmeshOverride* overrides_=nullptr;
    unsigned overrideCount_=0;
    bool initBloomTargets();
    bool initBloom();
    void freeBloom();
    void bloomPass(C3D_RenderTarget* target,unsigned width,unsigned height);
    void bloomQuad(const C3D_Tex& source,unsigned sourceWidth,unsigned sourceHeight,float width,float height,float du,float dv,unsigned secondWidth=0,unsigned secondHeight=0,const C3D_Tex* secondSource=nullptr,float secondDu=0,float secondDv=0);
    void freeStadiumScreen();
    void captureStadiumScreenNow();
    C3D_Tex stadiumTex_[2]{};
    C3D_RenderTarget* stadiumTarget_[2]{};
    unsigned stadiumRead_=0;
    bool stadiumRequested_=false,stadiumFrame_=false,stadiumPending_=false;
    bool stadiumVisible_=true;
    unsigned stadiumRenderFrames_=0;
    bool stadiumCaptureFrame_=false;
    std::vector<Bounds> stadiumScreens_;
    C3D_Tex sceneTex_{},bloomTex_[4]{};
    C3D_RenderTarget* sceneTarget_=nullptr;
    C3D_RenderTarget* bloomTarget_[4]{};
    Vertex* bloomVertices_=nullptr;
    unsigned bloomCount_=0;
    bool bloomRequested_=false,bloomFrame_=false,bloomFailed_=false;
    const CourseBloom* bloomSettings_=nullptr;
    C3D_Tex white_{};
    // Existing screen quads plus up to four textured wheel-contact quads.
    static constexpr int kOverlayCapacity=48+4*6;
    // Both eyes share one submission. Reserve disjoint quad slots, including
    // the left-only stadium capture/composite (no extra render targets).
    static constexpr unsigned kBloomVertices=6*52;
    Vertex* overlay_=nullptr;
    int overlayCount_=0;
    // Authored Wii HUD panes include layered frames, glyphs and animation quads.
    static constexpr int kSpriteCapacity=512;
    Vertex* sprites_=nullptr;
    // Each water draw owns its sampled vertices until the next GPU frame fence.
    // The sprite writers' current buffer: the top screen's (sprites_), or after
    // beginBottom the bottom screen's own quads, the last kBottomSpriteCapacity*6
    // vertices of the existing text_ allocation (no LINEAR is added). The bottom
    // pass runs once, in the frame's last eye, and writes its own vertices.
    static constexpr int kBottomSpriteCapacity=BottomQuadCapacity;
    Vertex* spriteBase_=nullptr;
    int spriteCapacity_=kSpriteCapacity,topSpriteCount_=0;
    bool bottomPass_=false;
    bool writesVertices() const {return !eye_||bottomPass_;}
    void beginBottomNow();
    Vertex* particles_=nullptr;
    uint8_t sparkCounts_[DriftEffects::Capacity*2]{}; // vertices per spark, at its first vertex / 6
    // NW4R EF: second shader (DVLE 1), view-space vertices and the colour-lookup texture.
    static constexpr unsigned kEffectVertices=8192;
    void* effectVertices_=nullptr;
    shaderProgram_s effectProgram_{};
    bool effectProgramReady_=false;
    int effectProjectionLoc_=-1;
    C3D_Tex effectLookup_{};
    struct EffectDrawState* effectDraw_=nullptr; // owned; deleted in shutdown()
    int spriteCount_=0;
    // Consecutive screen sprites sharing texture and blend are one draw call.
    // Their vertices are flushed once per frame, not once per quad.
    void flushSprites();
    void spriteState();
    bool spriteStateReady_=false;
    int spriteRunFirst_=0;
    const C3D_Tex* spriteRunTexture_=nullptr;
    bool spriteRunAdditive_=false;
    static constexpr unsigned kTextVertices=12288;
    // Text uses the head of text_; the bottom screen's sprites its tail. While no
    // frame draws, loadVramTexture stages uploads through the head as well.
    static constexpr unsigned kTextVertexLimit=kTextVertices-kBottomSpriteCapacity*6;
    static constexpr std::size_t kStagingBytes=256*1024;
    static_assert(kStagingBytes<=kTextVertexLimit*sizeof(Vertex),"staging must not reach the bottom sprites");
    Vertex* text_=nullptr;
    unsigned textCount_=0;
    bool ready_=false;
    bool deviceReady_=false;
    bool programReady_=false;
};
}
