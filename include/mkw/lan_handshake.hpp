#pragma once

namespace mkw {
class Renderer;
// LAN play: two hands slide together and shake once both consoles have
// agreed on a race (about 2.5 s on the upper screen, then the race loads).
void playLanHandshake(Renderer& renderer);
}
