#pragma once
#include <mkw/menu_index.hpp>
#include <initializer_list>

namespace mkw {
struct UiTextClipFrame {const UiAnimationClip* clip=nullptr;float frame=0,reference=0;const UiAnimationClip* referenceClip=nullptr;};
// Transform from the cooked reference pose to the current authored pose.
// Coordinates use the source layout's +Y-up convention, before control scaling.
struct UiTextMotion {
 float sx=1,sy=1,tx=0,ty=0,alpha=1;
 bool visible=true;
 bool absoluteAlpha=false;
 // The live pane alpha including its inherited ancestors (InfluencedAlpha),
 // independent of the reference pose; meaningful when evaluated is true.
 float absolute=1;
 bool evaluated=false;
};
UiTextMotion uiTextMotion(const UiPaneInfo&,std::initializer_list<UiTextClipFrame>);
}
