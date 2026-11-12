#pragma once
#include <array>
#include <cstdint>

namespace mkw {
class MenuState;
struct UiAnimationClip;
enum class UiFocusPhase : std::uint8_t {Free,FreeToSelect,Select,SelectToFree};
inline constexpr unsigned UiFocusButtonCount=26,UiFocusMainParentBase=26,UiFocusControlCount=30;
struct UiFocusControl {
 const UiAnimationClip* clip=nullptr;
 UiFocusPhase phase=UiFocusPhase::Free;
 float frame=0;
 const UiAnimationClip* selectInClip=nullptr;
 float selectInFrame=0;
};
struct UiFocusState {
 std::array<UiFocusControl,UiFocusControlCount> controls{};
 unsigned page=~0u,cursor=~0u;
 // Main-menu heading feedback, independent of selection and page exit.
 unsigned mainConfirmSlot=~0u;
 float mainConfirmFrame=0;
};
struct UiFocusView {
 const char* control="";
 const UiAnimationClip* clip=nullptr;
 UiFocusPhase phase=UiFocusPhase::Free;
 float frame=0;
 bool selected=false;
 const UiAnimationClip* selectInClip=nullptr;
 float selectInFrame=0;
 float value(const char* element,const char* kind,unsigned target,float fallback,unsigned index=0)const;
 bool changing()const{return phase==UiFocusPhase::FreeToSelect||phase==UiFocusPhase::SelectToFree;}
};
// Logical buttons occupy0..25. MainMenu's independent parent frame controls
// use UiFocusMainParentBase+tile (Single,Multi,WFC,Channel).
UiFocusView uiFocusAnimation(const MenuState&,unsigned slot);
const char* uiFocusControlName(const MenuState&,unsigned slot);
void uiFocusReset(MenuState&);
void uiFocusMovieChapter(MenuState&,int chapter);
void uiFocusSelect(MenuState&,unsigned previousCursor);
void uiFocusAdvance(MenuState&,unsigned ticks);
void uiFocusMainConfirm(MenuState&,unsigned slot);
}
