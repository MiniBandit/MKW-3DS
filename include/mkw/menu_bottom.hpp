#pragma once
#include <mkw/menu_ui.hpp>
#include <array>
#include <cstdint>
namespace mkw {
class MiiStore;
struct MenuBottomMap;
// The menus' lower screen (320 x 240): the current page's choices as large
// touch targets, mirrored from the same MenuState the top screen shows. A tap
// selects a control and accepts it, exactly as the d-pad and A would; the Back
// button is B. Pure layout (no drawing, no 3DS headers): host-testable, and the
// one place future pages are added, so later UI work is a code-only update.
struct BottomWidget {
    enum Kind : std::uint8_t {Tile,Button,Label};
    Kind kind=Label;
    float x=0,y=0,w=0,h=0;
    char text[32]{};
    char sub[32]{};
    char icon[64]{};      // optional .t3x (power-of-two sides) drawn at the left of a tile, e.g. a Mii portrait
    float textSize=1;
    int slot=-1;          // MenuState cursor slot a tap selects and accepts (-1: none)
    bool back=false;      // a tap is Back
    bool enabled=true;    // false: drawn dimmed (a tap still reports "not available")
    bool selected=false;  // the page's current cursor
    bool hidden=false;    // tappable but not drawn (the control's art is drawn by the menu quads)
    std::uint32_t color=0;// RRGGBBAA fill (Tile and Button)
};
struct MenuBottomLayout {
    static constexpr unsigned Capacity=48;
    std::array<BottomWidget,Capacity> widgets{};
    unsigned count=0;
    UiPage page=UiPage::Strap;
    unsigned cursor=0;
    bool message=false;   // "not available" notice
    BottomWidget* add();  // null when full
};
// Names the 32 race courses in menu order (src/main.cpp passes the course catalog;
// kept out of this layer so it builds on the host).
using CourseNameFn=const char* (*)(unsigned course);
// Text drawn on the top screen over a page's own art (the ghost page's record panel).
struct MenuTopText {
    char text[40]{};
    float x=0,y=0,size=1;
    std::uint32_t rgba=0xffffffffu;
};
struct MenuTopInfo {
    const char* courseName=nullptr;
    unsigned bestMs=0;       // saved best time of the selected course (0: none)
    bool ghostSaved=false;   // a ghost is saved for it
};
struct MenuTopTexts {
    std::array<MenuTopText,8> lines{};
    unsigned count=0;
};
// What the top screen shows beyond the authored art for this page (empty for most pages).
void composeMenuTopText(const MenuState& state,const MenuTopInfo& info,MenuTopTexts& out);
// Fills the layout for the state. miis (may be null) names the two Mii outfit slots.
void composeMenuBottom(const MenuState& state,MenuBottomLayout& layout,const MiiStore* miis=nullptr,CourseNameFn courseName=nullptr,const MenuBottomMap* moved=nullptr);
// The widget under a touch (pixels), or -1; only tappable widgets count.
int hitMenuBottom(const MenuBottomLayout& layout,float x,float y);
}
