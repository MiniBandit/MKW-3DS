#pragma once
#include <mkw/menu_ui.hpp>
#include <array>
#include <vector>
namespace mkw {
// Moves a page's controls (the authored Wii buttons, with their art and focus
// animation) from the top screen to the lower one, so they are drawn once, on
// the screen that takes touch. Every quad of the page's composition carries the
// control it belongs to (UiQuad::slot); this picks those, scales them to fit
// the lower screen and flags them bottom. The page's other art (title bar,
// preview, character, background) stays on top.
struct MenuBottomMap {
    struct SlotRect {bool present=false;float x0=0,y0=0,x1=0,y1=0;};
    bool valid=false;
    UiPage page=UiPage::Strap;
    std::array<SlotRect,32> rects{}; // lower-screen rectangle of each control, by slot
    SlotRect back;                   // the Back button (a copy of the page's own art)
};
// True for pages whose controls move.
bool movesControls(UiPage page);
// Fills quads' control art for the lower screen (quad.bottom=true) and the map
// of where each control now is. The placement comes from the page's settled
// layout (cached), so it does not move while the page animates in or out.
void relocateControls(const MenuState& state,std::vector<UiQuad>& quads,MenuBottomMap& map);
}
