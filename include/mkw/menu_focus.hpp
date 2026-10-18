#pragma once
#include <array>
#include <cstdint>

namespace mkw {
class MenuState;
struct UiAnimationClip;
enum class UiFocusPhase : std::uint8_t {Free,FreeToSelect,Select,SelectToFree};
inline constexpr unsigned UiFocusButtonCount=26,UiFocusMainParentBase=26,UiFocusControlCount=30;
// PushButton_calc (PAL 805bdd98) holds the Loop group at frame 30 while the
// Select group plays Free; any other Select animation resumes it from there.
inline constexpr float UiFocusFreeLoopFrame=30.f;
struct UiFocusControl {
 const UiAnimationClip* clip=nullptr;
 UiFocusPhase phase=UiFocusPhase::Free;
 float frame=0;
 // SelectIn group (PushButton group 2): SelectIn restarts on every select
 // (PAL 805bdffc) and chains to SelectStop; deselecting leaves it running.
 const UiAnimationClip* selectIn=nullptr;
 float selectInFrame=0;
 // Loop group (group 0) frame, see UiFocusFreeLoopFrame.
 float loop=UiFocusFreeLoopFrame;
};
struct UiFocusState {
 std::array<UiFocusControl,UiFocusControlCount> controls{};
 unsigned page=~0u,cursor=~0u;
};
struct UiFocusView {
 const char* control="";
 const UiAnimationClip* clip=nullptr;
 UiFocusPhase phase=UiFocusPhase::Free;
 float frame=0;
 bool selected=false;
 const UiAnimationClip* selectIn=nullptr;
 float selectInFrame=0;
 float loop=UiFocusFreeLoopFrame;
 float value(const char* element,const char* kind,unsigned target,float fallback,unsigned index=0)const;
 bool changing()const{return phase==UiFocusPhase::FreeToSelect||phase==UiFocusPhase::SelectToFree;}
};
// Logical buttons occupy0..25. MainMenu's independent parent frame controls
// use UiFocusMainParentBase+tile (Single,Multi,WFC,Channel).
UiFocusView uiFocusAnimation(const MenuState&,unsigned slot);
const char* uiFocusControlName(const MenuState&,unsigned slot);
void uiFocusReset(MenuState&);
void uiFocusSelect(MenuState&,unsigned previousCursor);
void uiFocusAdvance(MenuState&,unsigned ticks);
}
