#pragma once
#include <mkw/race_flow_layout.hpp>
#include <array>
#include <cstdint>

namespace mkw {
// One BRCTR animation group's current clip (UIAnimator AnimationGroup).
struct FlowPlay {int16_t clip=-1;float frame=0;};

// Hermite / step sampling of one cooked BRLAN track (NW4R AnimTransform).
float flowSample(const FlowKey* keys,unsigned count,float frame,bool step);
// Plays `clip` of `def` from frame 0 (AnimationGroup::PlayAnimationAtFrame).
FlowPlay flowPlay(const FlowLayoutDef& def,const char* group,const char* clip);
// Advances by `frames`: loops wrap, a finished clip continues into its BRCTR
// "next" clip, otherwise it holds its last frame.
void flowAdvance(const FlowLayoutDef& def,FlowPlay& play,float frames);
bool flowFinished(const FlowLayoutDef& def,const FlowPlay& play);

// Evaluated layout: affine pane transforms in layout units (y up) relative to
// the control origin, propagated alpha/visibility, and animated materials.
struct FlowPanePose {
 float m[6]{1,0,0,0,1,0}; // x' = m0 x + m1 y + m2, y' = m3 x + m4 y + m5
 float w=0,h=0,z=0,alpha=1;bool visible=true; // z: summed pane translations
 std::array<uint32_t,4> vertex{};
};
struct FlowMaterialPose {std::array<float,4> c0{},c1{};std::array<float,5> srt{};};
class FlowPose {
public:
 static constexpr unsigned MaxPanes=32,MaxMaterials=20;
 void evaluate(const FlowLayoutDef& def,const FlowPlay* plays,unsigned playCount);
 std::array<FlowPanePose,MaxPanes> panes{};
 std::array<FlowMaterialPose,MaxMaterials> materials{};
private:
 // Scratch for evaluate(), kept in the object (the race loop's stack is tight).
 struct Local {float v[10];float alpha;bool visible;};
 std::array<Local,MaxPanes> local_{};std::array<float,MaxPanes> passed_{};
};
int flowPaneIndex(const FlowLayoutDef& def,const char* name);
int flowMaterialIndex(const FlowLayoutDef& def,const char* name);
}
