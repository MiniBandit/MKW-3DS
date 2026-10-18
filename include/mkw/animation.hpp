#pragma once
#include "mkw/renderer.hpp"
#include <array>
#include <mkw/visibility.hpp>
#include <mkw/skin_positions.hpp>
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
    int find(const char* name) const;
    // Vehicle-part clips chosen every tick, resolved at load (-1 if absent).
    int handleDriveClip() const {return handleDrive_;}
    int dashClip() const {return dash_;}
    int shakeClip() const {return shake_;}
    int face(const char* name) const;
    const char* name(unsigned clip) const {return clip<count_?clips_[clip].name:"none";}
    unsigned count() const {return count_;}
    float duration(unsigned clip) const {return clip<count_?clips_[clip].duration:0;}
    bool loop(unsigned clip) const {return clip<count_&&clips_[clip].loop;}
    unsigned faceCount() const {return faceCount_;}
    const char* faceName(unsigned face) const {return face<faceCount_?faces_[face].name:"none";}
    float faceDuration(unsigned face) const {return face<faceCount_?faces_[face].duration:0;}
    bool faceLoop(unsigned face) const {return face<faceCount_&&faces_[face].loop;}
    // A rejected current-pose bound skips only skinning/upload; palette transitions still advance.
    bool apply(Model& model,int clip,float frame,unsigned tick=0,int expression=-1,float expressionFrame=0,const ClipFrustum* frustum=nullptr);
    // Gameplay driver pose with the retail crossfade (PlayerModel blend, fn_1_49114
    // 0x805594C8): `from` keeps animating at its own frame while `clip` takes
    // `weight` of the pose. Replaces apply()'s automatic six-frame transition.
    struct ClipBlend {int clip=-1;float frame=0,weight=1;};
    bool apply(Model& model,int clip,float frame,const ClipBlend& from,unsigned tick,int expression=-1,float expressionFrame=0,const ClipFrustum* frustum=nullptr);
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
    const float* pose(int clip,float frame,unsigned tick=0);
    // GPU skinning rewrites positions to bone-local space. Lit models keep
    // source normals and a separate bone-index attribute; unlit models store
    // the index in normal.x. apply() only writes visibility, UVs and textures;
    // draw with palette(). The GPU needs just the first 12 affine floats.
    bool gpuSkin(Model& model);
    bool gpuSkinned() const {return gpu_;}
    const float* palette() const {return palette_.data();}
    unsigned matrixCount() const {return matrices_;}
    bool ready=false;
private:
    struct Clip { char name[32];unsigned duration,loop,count;const uint8_t* data; };
    struct SourceVertex {float p[3],n[3];uint32_t matrix;};
    struct Range {uint32_t first,count;};
    struct HairNode {uint32_t matrix,parent;};
    struct HairClip {Clip clip;const float* inverseRest;};
    std::array<Clip,64> clips_{},faces_{},uvClips_{};
    unsigned uvClipCount_=0;
    std::vector<std::array<float,2>> baseUV_;
    std::array<Bounds,128> sourceBounds_{};
    std::array<bool,128> usedMatrices_{};
    Bounds poseBounds_{};bool poseBoundsValid_=false;
    std::array<float,128*21> palette_{};
    std::array<float,128*21> previousPalette_{};
    bool hasPreviousPose_=false;
    std::array<HairNode,8> hairNodes_{};
    std::array<HairClip,2> hairClips_{};
    unsigned hairNodeCount_=0,hairClipCount_=0;
    void applyHair(const Clip& primary,unsigned tick);
    // apply()'s writes to the model (vertices, submesh textures): GPU memory a
    // drawing frame may still read, so a deferred frame runs them on the
    // render thread (deferredWrites). `palette` is the CPU path's pose. Only
    // write() touches the shape/UV state below and the model's buffer.
    void write(Model& model,const uint32_t* shown,const int32_t* textures,const float* uv,const float* palette);
    // Shared by apply() and pose(): interpolated (and blended) palette.
    void evaluate(const Clip& c,unsigned a,unsigned b,float t,unsigned tick,float blend);
    // apply() after the transition weight is known: cache, skinning and face writes.
    bool applyPose(Model& model,int clip,float frame,unsigned tick,int expression,float expressionFrame,const ClipFrustum* frustum,float blend);
    std::vector<std::vector<RigidPart>> rigidParts_;
    std::array<int,64> baseTextures_{};
    uint8_t* data_=nullptr;bool dataLinear_=false;const SourceVertex* source_=nullptr;const Range* ranges_=nullptr;
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
