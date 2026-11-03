#pragma once
namespace mkw::display {
// Native top LCD. Menus previously mapped to a letterboxed 400x225 canvas;
// all presentation now uses the full 5:3 frame requested for this build.
inline constexpr float Width=400.f,Height=240.f,Aspect=Width/Height;
// Shared virtual canvas: the physical hinge is unseen travel space.
inline constexpr float HingeGap=80.f,LowerOriginY=Height+HingeGap;
// Retain the original camera target at the upper LCD centre; the lower
// viewport continues below the hidden hinge at the same pixel/world scale.
inline constexpr float IntroHeight=2.f*Height+HingeGap;
inline constexpr float IntroBottomClipOffset=2.f*LowerOriginY/Height;
inline constexpr float IntroCullScale=Height/IntroHeight;
inline constexpr float IntroCullOffset=(IntroHeight-Height)/IntroHeight;
inline constexpr float MenuScaleX=Width/608.f,MenuScaleY=Height/456.f;
static_assert(Width*3==Height*5);
}
