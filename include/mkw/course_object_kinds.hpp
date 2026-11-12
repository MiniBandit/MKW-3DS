#pragma once
// Internal to the course-object renderer: cooked assets, placement records and
// the per-object presentation controllers (src/course_object_kinds.cpp).
#include "mkw/animation.hpp"
#include "mkw/fire_phase_stream.hpp"
#include "mkw/object_bank.hpp"
#include "mkw/inseki_motion.hpp"
#include "mkw/renderer.hpp"
#include <egg/math/Matrix.hh>
#include <game/system/map/MapdataGeoObj.hh>
#include <array>
#include <memory>
#include <string>
#include <vector>
namespace Kinoko::Field {class ObjectBase;class RailInterpolator;}
namespace mkw {
struct ObjModel {
    std::unique_ptr<FirePhaseStream> firePhases;
    int fireTexture=-1,firePhase=-1;
    int budgetClip=-2;float budgetFrame=-1;std::vector<float> budgetPalette;

    std::string resource,name;
    std::unique_ptr<Model> model;
    std::unique_ptr<ObjectBank> bank;
    Bounds bounds{};
    Bounds stadiumFace{};bool hasStadiumFace=false;
    bool rigid=false,cpuSkin=false,gpuSkin=false;
    // Retail's spectator crowds (group_* and monte_* banks): decorative,
    // CPU-skinned, and the costliest skins on Daisy Circuit and Coconut Mall.
    bool crowd=false;
    // A retail-lit submesh (GX lit channel or light textures) shades by
    // normals, so CPU skinning must transform them too.
    bool litNormals=false;
    // CPU-skinned poses drawn this frame each own a vertex copy (LINEAR),
    // untouched until the GPU has drawn the frame. Instances at the same clip
    // frame (crowds) share one; skinKeys[i] is copy i's clip and frame. Copies
    // past skinUsed still hold the pose last written to them (writes run in
    // frame order), so a pose that repeats on a later frame needs no skinning.
    struct SkinKey {int clip;float frame;};
    std::vector<std::unique_ptr<Model>> skins;
    std::vector<SkinKey> skinKeys;
    unsigned skinUsed=0;
    std::vector<SubmeshOverride> overrides;
    // Vertex bounds per palette slot (slot space), for procedural poses.
    std::vector<Bounds> slots;
    std::vector<bool> slotUsed;
    struct MiiBillboard {unsigned slot,mode;};
    std::vector<MiiBillboard> miiBillboards;
};
struct Tracks {ObjModel* model=nullptr;int clip=-1,pat=-1,srt=-1,clr=-1;};
struct ObjectAssets {
    std::vector<std::unique_ptr<ObjModel>> models;
    ~ObjectAssets();
    bool load(int course);
    void beginFrame();
    ObjModel* find(const char* resource,const char* name) const;
    // The archive's primary model: same name as the archive, else its first
    // model that is neither a -low LOD nor a -shadow.
    ObjModel* main(const char* resource) const;
    ObjModel* low(const ObjModel* model) const;
    ObjModel* shadow(const ObjModel* model) const;
    // Tracks named name, else the model's or archive's own name.
    Tracks tracks(const ObjModel* model,const char* name=nullptr) const;
    // The common Bob-omb explosion (Bowser's Castle fireball, Shy Guy Beach cannonballs, Moonview's Bob-omb cars).
    std::unique_ptr<Model> explosion;
    std::unique_ptr<DriverAnimation> explosionBank;
    std::vector<std::unique_ptr<Model>> explosionPoses;
    unsigned explosionUsed=0;
};
struct Anim {
    int clip=-1;float frame=0;
    int pat=-1;float patFrame=0;
    int srt=-1;float srtFrame=0;
    int clr=-1;float clrFrame=0;
    // Per-instance modulation after CLR0; preserves the texture alpha mask.
    float opacity=1;
    bool mirrorU=false;
    const float* palette=nullptr;
    // Model-space bounds of a procedural palette (model bounds otherwise).
    const Bounds* bounds=nullptr;
};
// Every track of t at one shared frame.
inline Anim animAt(const Tracks& t,float frame){Anim a;a.clip=t.clip;a.frame=frame;a.pat=t.pat;a.patFrame=frame;a.srt=t.srt;a.srtFrame=frame;a.clr=t.clr;a.clrFrame=frame;return a;}
struct ObjectDraw;
struct Visual;
using ObjectPresenter=void (*)(ObjectDraw&,Visual&);
struct Visual {
    const Kinoko::System::MapdataGeoObj* geo=nullptr;
    unsigned index=0;
    const Kinoko::Field::ObjectBase* object=nullptr;
    // Every Kinoko object built from this placement, in registration order:
    // spawned parts first, the placement's own object last.
    std::vector<const Kinoko::Field::ObjectBase*> parts;
    // Another placement's object this presentation follows (statue -> fireball).
    const Kinoko::Field::ObjectBase* partner=nullptr;
    char name[32]{},resources[64]{},resource[64]{};
    C3D_Mtx placement{};
    ObjectPresenter present=nullptr;
    Tracks main,low,shadow;
    std::array<Tracks,6> extra{};
    std::array<int,8> clips{-1,-1,-1,-1,-1,-1,-1,-1};
    InsekiMotion inseki;
    float rate=1,phase=0;
    int variant=0;
    bool kinetic=false; // Kinoko moves it: use its live transform.
    // Presentation-only simulation state (chain links and their velocities).
    std::vector<Vec3> points,velocities;
    // Procedurally posed joints (deforming roads, nets and bridges).
    std::vector<float> palette;
    std::vector<float> callbackScratch;
    Bounds posed{};
    // Presentation-only route followers (src/course_object_movers.cpp) for
    // placements Kinoko does not move, their heading and simulated frames.
    std::vector<std::shared_ptr<Kinoko::Field::RailInterpolator>> rails;
    Vec3 heading{0,0,1};
    unsigned railTick=0,frames=0;
    bool railStarted=false;
    float speed=0;
    int moverState=0;
    std::vector<int> picks;
    // Where a presenter last drew the object (review framing for movers).
    C3D_Mtx drawn{};
    bool drawnValid=false;
    unsigned lastTick=0;
    // Observed Kinoko state and the tick it was first seen (clip timing).
    int lastState=-1;
    unsigned stateTick=0;
};
// One course-object draw as ObjectDraw prepared it (pose, materials, CPU
// skinning done), replayed in order on the rendering thread.
struct ObjectDrawRecord {
    enum Kind : uint8_t {Submit,Plain,Explosion};
    Kind kind=Submit;
    bool backdrop=false;
    uint8_t pose=0; // 1 rigid parts, 2 GPU skinned
    const Model* target=nullptr;
    const std::vector<std::vector<RigidPart>>* parts=nullptr;
    unsigned bones=0,palette=0,overrides=0,overrideCount=0;
    float frame=0;
    C3D_Mtx transform{};
};
struct ObjectDrawList {
    std::vector<ObjectDrawRecord> records;
    std::vector<float> palettes;
    std::vector<SubmeshOverride> overrides;
    void clear(){records.clear();palettes.clear();overrides.clear();}
};
// Presenters read the race and the frame's camera and record draws; they never
// call the renderer, so recording may run on the job worker.
struct ObjectDraw {
    const ViewFrustum& view;
    ObjectAssets& assets;
    unsigned tick;
    Vec3 camera;
    int course;
    std::vector<float>& palette_;
    std::vector<Renderer::ShadowVolume>& shadows_;
    ObjectDrawList& list;
    // Sky pass: no depth test, write or culling (Renderer::draw backdrop).
    bool backdrop=false;
    std::size_t allocationReserve=2u*1024u*1024u;
    void submit(ObjModel* model,const C3D_Mtx& transform,const Anim& anim);
    // -shadow meshes are stencil shadow volumes, resolved after all objects.
    void shadow(ObjModel* model,const C3D_Mtx& transform,const Anim& anim={});
    // The common Bob-omb explosion, posed and drawn in order at replay.
    void explosion(const C3D_Mtx& transform,float frame);
    void plain(const Model& model,const C3D_Mtx& transform);
};
bool classifyObject(Visual& visual,const ObjectAssets& assets,int course);
// Route followers for objects Kinoko leaves static; true when v is one.
bool classifyMover(Visual& visual,const ObjectAssets& assets);
void drawObject(ObjectDraw& draw,Visual& visual);
// Drawn in the sky pass rather than with the course objects.
bool skyObject(const Visual& visual);
bool waterObject(const Visual& visual);
// The Kinoko object a review camera should frame (a roaming part, an active cart).
const Kinoko::Field::ObjectBase* reviewFocus(const Visual& visual);
C3D_Mtx objectMatrix(const Kinoko::EGG::Matrix34f& source,const Kinoko::EGG::Vector3f& scale);
C3D_Mtx placementMatrix(const Kinoko::System::MapdataGeoObj& geo);
// Columns x,y,z scaled per axis, translated to position.
C3D_Mtx basisMatrix(Vec3 x,Vec3 y,Vec3 z,Vec3 position,Vec3 scale={1,1,1});
}
