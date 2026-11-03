#pragma once
#include "mkw/renderer.hpp"
#include <array>
#include <mkw/visibility.hpp>
#include <mkw/skin_positions.hpp>
#include <mkw/toadette_braids.hpp>
#include <mkw/driver_look_palette.hpp>
#include <mkw/driver_look.hpp>
#include <mkw/driver_vehicle_response.hpp>
#include <mkw/driver_controller.hpp>
namespace mkw {
// Cooked original driver animation: local vertices, matrix palettes, visibility
// and facial texture tracks. Only the selected character is resident.
class DriverAnimation {
public:
    ~DriverAnimation(){reset();}
    bool load(const char* path,const Model& model);
    void reset();
    // Menu PlayAnmNoBlend replaces the active palette without a gameplay blend.
    void clearPoseTransition(){lastClip_=-1;hasPreviousPose_=false;cachedSettled_=false;}
    void setLookAngles(float yaw,float pitch){
        if(lookYaw_!=yaw||lookPitch_!=pitch){lookYaw_=yaw;lookPitch_=pitch;cachedSettled_=false;}
    }
    // Physics-tick update; independent of visibility and stereo render count.
    void stepLook(int clip,float frame,unsigned tick,bool active,Vec3 target,
        const C3D_Mtx& seated,bool body,const DriverLookParameters& parameters,
        const DriverPose& motion,bool handleRelativeBike,float xAxisY,float zAxisY);
    void resetLook(){motionBlendValid_=false;vehicleResponse_.reset();lookMotion_.reset();lookTickValid_=false;cachedSettled_=false;setLookAngles(0,0);}
    DriverLookMotion::Angles lookAngles() const {return lookMotion_.angles();}
    int find(const char* name) const;
    // Vehicle-part clips chosen every tick, resolved at load (-1 if absent).
    int handleDriveClip() const {return handleDrive_;}
    int dashClip() const {return dash_;}
    int shakeClip() const {return shake_;}
    int face(const char* name) const;
    unsigned faceDuration(int expression) const {return expression>=0&&unsigned(expression)<faceCount_?faces_[expression].duration:0;}
    const char* name(unsigned clip) const {return clip<count_?clips_[clip].name:"none";}
    unsigned count() const {return count_;}
    float duration(unsigned clip) const {return clip<count_?clips_[clip].duration:0;}
    bool loop(unsigned clip) const {return clip<count_&&clips_[clip].loop;}
    // A rejected current-pose bound skips only skinning/upload; palette transitions still advance.
    bool apply(Model& model,int clip,float frame,unsigned tick=0,int expression=-1,float expressionFrame=0,const ClipFrustum* frustum=nullptr);
    // Choose whether CPU fallback skins normals. Unlit course objects can
    // share each (position, matrix) pair instead (call after load).
    void skinNormals(bool on);
    // Load-only setup for the same unlit race model passed to load(). Try GPU
    // skinning before allocating CPU position-sharing maps; a rejected GPU
    // setup retains the skinNormals(false) CPU fallback. Returns GPU mode.
    // Repeated GPU setup preserves the current pose and model contents.
    bool prepareUnlitSkinning(Model& model);
    // GPU positions and inverse-transpose normals; CPU fallback for large banks.
    bool prepareLitSkinning(Model& model);
    // Rigid skinning on the GPU: when every triangle follows a single palette
    // matrix (no secondary hair, constant shape visibility), rewrites the model
    // to matrix-local vertices, groups each submesh's triangles by matrix and
    // returns true. Afterwards draw with pose() + Renderer::setRigidPose; apply()
    // must no longer be used on this model. Safe on the asset loader thread.
    bool makeRigid(Model& model);
    bool rigid() const {return !rigidParts_.empty();}
    const std::vector<std::vector<RigidPart>>& rigidParts() const {return rigidParts_;}
    // Evaluates the palette for clip/frame, including transition blends.
    const float* pose(int clip,float frame,unsigned tick=0,bool simulateBraids=true);
    // GPU skinning rewrites positions to bone-local space. Lit models keep
    // source normals and a separate bone-index attribute; unlit models store
    // the index in normal.x. apply() only writes visibility, UVs and textures;
    // draw with palette(). The GPU needs just the first 12 affine floats.
    bool gpuSkin(Model& model);
    bool gpuSkinned() const {return gpu_;}
    unsigned shareDrawVertices(Model& model);
    const float* palette() const {return palette_.data();}
    unsigned matrixCount() const {return matrices_;}
    // Current animated skl_root, including the same transition blend as
    // rendered geometry. False for banks without original root metadata.
    bool effectRoot(Vec3& origin) const {if(!ready||!effectRootReady_)return false;origin=effectRoot_;return true;}
    // Read the same evaluated face_1 palette used by the body, after
    // apply()/pose(). Separate Mii heads follow its animation and blends.
    bool headTransform(C3D_Mtx& result) const {
        if(!ready||!lookCount_||lookHead_>=matrices_||lastClip_<0)return false;
        Mtx_Identity(&result);
        const float* joint=palette_.data()+lookHead_*21;
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)result.r[row].c[3-col]=joint[row*4+col];
        return true;
    }
    // Set on the pose worker, before apply(): the same seated matrix used to draw.
    void setBraidWorld(const C3D_Mtx& world);
    bool ready=false;
    // Local player must never hold a pose for adaptive presentation budgeting.
    bool fullRate=false;
private:
    struct Clip { char name[32];unsigned duration,loop,count;const uint8_t* data; };
    struct SourceVertex {float p[3],n[3];uint32_t matrix;};
    struct Range {uint32_t first,count;};
    struct HairNode {uint32_t matrix,parent;};
    struct HairClip {Clip clip;const float* inverseRest;};
    std::array<Clip,64> clips_{},faces_{},uvClips_{};
    unsigned uvClipCount_=0;
    int32_t effectRootSlot_=-2;
    bool effectRootReady_=false;
    Vec3 effectRoot_{},previousEffectRoot_{};
    std::array<const float*,64> effectRootTracks_{};
    std::vector<std::array<float,2>> baseUV_;
    std::array<Bounds,128> sourceBounds_{};
    std::array<bool,128> usedMatrices_{};
    Bounds poseBounds_{};bool poseBoundsValid_=false;
    std::array<float,128*21> palette_{};
    std::array<float,128*21> previousPalette_{};
    std::array<uint32_t,128> lookSlots_{};
    unsigned lookHead_=0,lookCount_=0;
    struct BodyLook {unsigned joint=0,count=0;std::array<uint32_t,128> slots{};};
    std::array<BodyLook,2> bodyLook_{};
    float lookYaw_=0,lookPitch_=0;
    DriverLookMotion lookMotion_;
    DriverVehicleResponse vehicleResponse_;
    bool motionBlendValid_=false;float motionBlendWeight_=1;
    unsigned lookTick_=0;bool lookTickValid_=false;
    std::vector<float> beforeLookPalette_;
    bool hasPreviousPose_=false;
    std::array<HairNode,8> hairNodes_{};
    std::array<HairClip,2> hairClips_{};
    unsigned hairNodeCount_=0,hairClipCount_=0;
    void applyHair(const Clip& primary,unsigned tick);
    void applyBraids(unsigned tick);
    bool hasBraids_=false,braidsRacing_=false;
    uint32_t braidHead_=0;
    std::array<uint32_t,6> braidSlots_{};
    std::array<float,21> braidWorld_{{1,0,0,0,0,1,0,0,0,0,1,0,1,0,0,0,1,0,0,0,1}};
    ToadetteBraids braids_;
    // apply()'s writes to the model (vertices, submesh textures): GPU memory a
    // drawing frame may still read, so a deferred frame runs them on the
    // render thread (deferredWrites). `palette` is the CPU path's pose. Only
    // write() touches the shape/UV state below and the model's buffer.
    void write(Model& model,const uint32_t* shown,const int32_t* textures,const float* uv,const float* palette);
    // Shared by apply() and pose(): interpolated (and blended) palette.
    void evaluate(const Clip& c,unsigned a,unsigned b,float t,unsigned tick,float blend,bool simulateBraids=true);
    std::vector<std::vector<RigidPart>> rigidParts_;
    std::array<int,64> baseTextures_{};
    bool dataLinear_=true;
    uint8_t* data_=nullptr;const SourceVertex* source_=nullptr;const Range* ranges_=nullptr;
    unsigned vertices_=0,matrices_=0,shapes_=0,count_=0,faceCount_=0,transitionTick_=0;
    int lastClip_=-1;
    int handleDrive_=-1,dash_=-1,shake_=-1;
    bool gpu_=false;
    std::array<bool,64> shapeHidden_{};
    bool skinNormals_=true;
    SharedPositions shared_;
    // Vertex buffer whose UV0 currently holds the neutral (untransformed) UVs.
    const Vertex* neutralUv_=nullptr;
    // GPU path: uvBuffer_'s UV0 holds baseUV_ under appliedUv_ (or neutral).
    const Vertex* uvBuffer_=nullptr;bool uvNeutral_=true,uvDisjoint_=true;
    std::array<float,64*6> appliedUv_{};
    // Cache only an identical completed pose on the same destination buffer.
    // Secondary hair keeps its independent tick; clip transitions never skip.
    const Vertex* cachedVertices_=nullptr;
    int cachedClip_=-1,cachedExpression_=-2;
    float cachedFrame_=-1,cachedExpressionFrame_=-1;
    unsigned cachedTick_=0;
    bool cachedSettled_=false;
};
}
