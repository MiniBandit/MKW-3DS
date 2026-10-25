#pragma once
#include <array>
#include <memory>

namespace mkw {
class Renderer;
struct Texture;
// The course banner during a race's course preview: the mode or cup (with the
// engine class or race number) above the course name, in the menu's own
// rendered Mario Kart Wii text (textures/ui_text_*.t3x), sliding in at the lower
// left of the top screen and out as the preview ends.
class IntroBanner {
public:
    IntroBanner();~IntroBanner();
    // Up to four words for the first line, then the course name; each must be a
    // menu string (UiSprites). Unknown strings are left out. Call between races.
    bool load(const std::array<const char*,4>& first,const char* course);
    void release();
    // One 60 Hz tick: whether the preview is still showing.
    void step(bool previewActive);
    void draw(Renderer& renderer) const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
