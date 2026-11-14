#pragma once
#include <mkw/menu_ui.hpp>
#include <cstdint>
#include <vector>

namespace mkw {
// What a stylus point on the lower LCD (pixels, 320x240) hits in a menu page.
// Choice: the cursor slot of a button. Press: A (a page's one button, or a
// page that only waits for A). SwitchType: Y (the class switch).
struct UiTouchTarget {
 enum class Kind : std::uint8_t {None,Choice,Back,Press,SwitchType};
 Kind kind=Kind::None;unsigned slot=~0u;
 bool operator==(const UiTouchTarget&)const=default;
};
// From the lower page as last drawn (layoutMenuScreens' bottom list).
UiTouchTarget uiTouchTarget(const MenuState&,const std::vector<UiQuad>& bottom,float x,float y);

// Touch screen input for the menus, in place of the Wii pointer. Touching a
// button highlights it, and lifting the stylus on it presses it: one tap
// chooses. Dragging moves the highlight with the stylus; a drag never
// presses anything, so the previews can be browsed without choosing.
class MenuTouch {
public:
 UiInput update(const MenuState&,const std::vector<UiQuad>& bottom,bool touching,float x,float y);
 // A touch already down (a race tap, a system applet) is ignored until lifted.
 void reset(){held_=true;active_=false;}
private:
 bool held_=true,active_=false,moved_=false;
 UiTouchTarget pressed_{};float x_=0,y_=0;
};
}
