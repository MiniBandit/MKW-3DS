#pragma once
#include "mkw/renderer.hpp"
#include <array>
namespace mkw {
// Retail lens flare (StaticR module ~0x80545864..0x8054a5dc, USA): 21 screen
// elements strung from the course's lensFX object (ObjFlow id 3) towards and
// past the screen centre, faded in while the source is on screen and not
// covered, faded out otherwise. Drawn after the scene (and bloom), before the HUD.
class LensFlare {
public:
    // Finds the course's first single-player lensFX (KMP GOBJ) and loads the
    // four textures once (tools/cook_lens_flare.py). Call after the Kinoko course loads.
    void reset();
    void clear(){hasSource_=false;fade_=0;}
    // Once per drawn race frame, after the scene and before bloom (the depth
    // probe reads the scene's target); physicsSteps advances the fade (60 Hz, as retail).
    void update(Renderer& renderer,unsigned physicsSteps);
    // After bloom, before the HUD.
    void draw(Renderer& renderer) const;
    bool active() const {return hasSource_;}
    int fade() const {return fade_;}
private:
    std::array<Texture,4> textures_;
    bool loaded_=false,hasSource_=false,haveScreen_=false;
    Vec3 source_{};
    float x_=0,y_=0;
    int fade_=0;
    unsigned lastSteps_=0;
};
}
