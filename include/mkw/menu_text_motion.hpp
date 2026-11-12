#pragma once
#include <mkw/menu_index.hpp>
#include <initializer_list>

namespace mkw {
struct UiTextClipFrame {const UiAnimationClip* clip=nullptr;float frame=0,reference=0;const UiAnimationClip* referenceClip=nullptr;};
// Transform from the cooked reference pose to the current authored pose.
// Coordinates use the source layout's +Y-up convention, before control scaling.
struct UiTextMotion {
 float sx=1,sy=1,tx=0,ty=0,alpha=1;
 float xy=0,yx=0; // Off-diagonal terms of the authored affine transform.
 bool visible=true;
 bool absoluteAlpha=false;
 bool fontMaterial=false;
 unsigned color0=0,color1=0xffffffff;
 std::array<unsigned,4> corners{0xffffffff,0xffffffff,0xffffffff,0xffffffff};
};
// Glows are baked with unit leaf scale/alpha, while their parent hierarchy
// retains the selected pose. Match that reference before applying live clips.
UiTextMotion uiTextMotion(const UiPaneInfo&,std::initializer_list<UiTextClipFrame>,bool unitLeafReference=false);
}
