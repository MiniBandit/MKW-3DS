#pragma once
#include <mkw/renderer.hpp>
#include <memory>

namespace mkw {
// LAN play connection screen on the upper LCD: a terminal listing what the
// connection is doing (libctru's 8x8 console font), and once both consoles
// agree, a white handshake over it (Font Awesome Free 6, CC BY 4.0).
class LanTerminal {
public:
    explicit LanTerminal(Renderer& renderer);
    ~LanTerminal();
    void add(const char* line,bool bright=false);
    // Draws one frame. connectedSeconds < 0: still connecting; otherwise the
    // time since the handshake, which brings the handshake in and shakes it.
    void frame(float connectedSeconds=-1.f);
private:
    void rebuild();
    Renderer& renderer_;
    std::unique_ptr<struct LanTerminalState> state_;
};
}
