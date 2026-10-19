#pragma once
#include "mkw/skin_positions.hpp"
#include "mkw/skin_reuse.hpp"
#include "mkw/renderer.hpp"
#include <cstdint>
#include <vector>
namespace mkw {
// Cooked course-object animation bank (tools/object_bank.ts, DOBJ v1).
// One bank is shared by every instance of its model, so evaluation is
// stateless: callers pass the clip and frame and receive palettes/overrides.
class ObjectBank {
public:
    struct Node { char name[32]; int32_t parent,slot,envelope; float local[12],world[12]; };
    ~ObjectBank(){reset();}
    bool load(const char* path);
    void reset();
    bool ready() const {return data_!=nullptr;}
    unsigned matrixCount() const {return matrices_;}
    unsigned shapeCount() const {return shapes_;}
    bool skinned() const {return vertices_>0;}
    // Weighted shapes (a triangle spanning several matrices) need CPU skinning.
    bool weighted() const {return weighted_||!morph_.empty();}
    bool morphing() const {return !morph_.empty();}
    int clip(const char* name) const;
    int pattern(const char* name) const;
    int scroll(const char* name) const;
    int color(const char* name) const;
    unsigned clipFrames(int clip) const {return clip>=0&&unsigned(clip)<chr_.size()?chr_[clip].frames:0;}
    bool clipLoops(int clip) const {return clip>=0&&unsigned(clip)<chr_.size()&&chr_[clip].loop;}
    unsigned patternFrames(int i) const {return i>=0&&unsigned(i)<pat_.size()?pat_[i].frames:0;}
    bool patternLoops(int i) const {return i>=0&&unsigned(i)<pat_.size()&&pat_[i].loop;}
    unsigned scrollFrames(int i) const {return i>=0&&unsigned(i)<srt_.size()?srt_[i].frames:0;}
    unsigned colorFrames(int i) const {return i>=0&&unsigned(i)<clr_.size()?clr_[i].frames:0;}
    // Row-major 3x4 palette (matrixCount*12 floats). Loops wrap; others clamp.
    void pose(int clip,float frame,float* out) const;
    struct JointCallback {const char* node;const float* matrix;};
    bool hasJointCallbacks() const {return !callbackSteps_.empty();}
    // Original CalcWorld callback B: postmultiply the node, then evaluate
    // children, final geometry scale and envelope blends. Scratch is reused
    // per instance; animation banks remain immutable across recording jobs.
    bool poseCallbacks(int clip,float frame,const JointCallback* callbacks,unsigned count,
            float* out,std::vector<float>& scratch) const;
    const float* bind() const {return bind_;}
    const std::vector<Node>& nodes() const {return nodes_;}
    int node(const char* name) const;
    // Overrides for each GMDL submesh from the selected tracks (any may be -1).
    // Returns false when no track touches this model.
    bool material(int pat,float patFrame,int srt,float srtFrame,int clr,float clrFrame,SubmeshOverride* out,unsigned count) const;
    // Shapes whose GX alpha is a TEV colour register that is 0 until a CLR0
    // track animates it (Toad's Factory's belt arrow overlays): the cooker bakes
    // that 0 into vertex alpha (alpha mode 4), and the track stores the
    // register's absolute alpha (tools/object_bank.ts), so material() marks
    // their alpha overrides SubmeshOverride::Alpha. Call once after loading.
    void markRegisterAlpha(const Model& model);
    // Rigid GPU skinning: rewrite the shared model to matrix-local vertices.
    bool makeRigid(Model& model);
    // GPU skinning: rewrite the shared model to bone-local positions with
    // 3 * bone index in normal.x (Renderer::setSkinPalette draws it). Only for
    // unlit models (their normals never reach a pixel) with few bones.
    bool gpuSkin(Model& model) const;
    const std::vector<std::vector<RigidPart>>& rigidParts() const {return rigidParts_;}
    bool rigid() const {return !rigidParts_.empty();}
    // CPU skinning into a per-instance copy of the model's vertex buffer.
    // normals=false leaves normals untouched, for meshes whose materials
    // never read them (unlit, not environment-mapped).
    void skin(const float* palette,Model& model,bool normals=true,int clip=-1,float frame=0) const;
    // Positions-only skinning transforms each shared (position, matrix) pair once.
    void shareSkinnedPositions(){shared_.build(source_,vertices_);}
    // Load-only preparation for lit CPU skinning. Reuses exact position,
    // normal and matrix tuples; allocation failure keeps the original path.
    bool shareSkinnedVertices();
    // Union of every sampled clip pose (and the bind pose) in model space.
    bool animatedBounds(Bounds& out) const;
    // Bounds of the vertices bound to each palette slot, in slot space.
    bool slotBounds(std::vector<Bounds>& out,std::vector<bool>& used) const;
private:
    struct SourceVertex {float p[3],n[3];uint32_t matrix;};
    struct Range {uint32_t first,count;};
    struct Chr {char name[32];uint32_t frames,loop,step,animatedCount;const uint32_t* animated;const float* base;const float* samples;};
    struct Pat {char name[32];uint32_t frames,loop;const int32_t* data;};
    struct Srt {char name[32];uint32_t frames,loop,step,count;const uint32_t* shapes;const float* data;};
    struct Clr {char name[32];uint32_t frames,loop,count;const uint32_t* shapes;const uint32_t* flags;const uint32_t* data;};
    struct Morph {uint32_t clip,count,frames;const uint32_t* vertices;const float* deltas;const float* weights;};
    struct CallbackStep {char name[32];int32_t parent;};
    struct CallbackTerm {uint32_t step;float weight,inverse[12];};
    struct CallbackRecipe {unsigned count;const CallbackTerm* terms;};
    struct CallbackClip {unsigned frames,loop,step;const float* data;};
    uint8_t* data_=nullptr;
    unsigned vertices_=0,matrices_=0,shapes_=0;
    bool weighted_=false;
    const SourceVertex* source_=nullptr;const Range* ranges_=nullptr;const float* bind_=nullptr;
    SharedPositions shared_;
    SkinRepresentatives sharedVertices_;
    std::vector<Node> nodes_;
    std::vector<Chr> chr_;std::vector<Pat> pat_;std::vector<Srt> srt_;std::vector<Clr> clr_;
    std::vector<Morph> morph_;
    std::vector<std::vector<RigidPart>> rigidParts_;
    std::vector<uint8_t> registerAlpha_;
    std::vector<CallbackStep> callbackSteps_;
    std::vector<CallbackRecipe> callbackRecipes_;
    std::vector<CallbackClip> callbackClips_;
    const float* callbackBind_=nullptr;
};
// Sample position for a stepped clip: samples a,b and blend t.
struct ClipSample {unsigned a,b;float t;};
ClipSample sampleClip(unsigned frames,unsigned step,bool loop,float frame);
}
