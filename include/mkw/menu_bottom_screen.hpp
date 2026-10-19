#pragma once
#include <mkw/menu_bottom.hpp>
#include <memory>
namespace mkw {
class Renderer;
// Draws the menus' lower screen from a MenuBottomLayout (menu_bottom.hpp): flat
// panels and the built-in text, no textures, so it costs no memory beyond the
// 320 x 240 bottom target. Create it before the menu loop (renderer idle,
// outside a frame) and release it before a race loads its own bottom screen.
class MenuBottomScreen {
public:
    // False when the bottom target cannot be made (the lower screen stays black).
    bool enable(Renderer& renderer);
    // Waits for the GPU, frees the target and blanks the lower LCD.
    void disable(Renderer& renderer);
    bool active() const {return on_;}
    // Records the lower screen for this frame (inside the frame, after the top
    // screen): calls Renderer::beginBottom() itself.
    void draw(Renderer& renderer,const MenuBottomLayout& layout);
    MenuBottomScreen();
    ~MenuBottomScreen();
private:
    struct Icons;
    std::unique_ptr<Icons> icons_;
    bool on_=false;
};
}
