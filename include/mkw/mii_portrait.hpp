#pragma once
#include <mkw/mii_head.hpp>
namespace mkw {
// Compose once when an identity changes, outside gameplay/render frames.
// Original MakeIcon camera, part order, materials and light; CPU coverage is
// an adaptation, not GX EFB bit-exact rasterization. Output is straight RGBA.
bool composeMiiPortrait(const MiiHead&,unsigned size,std::vector<std::uint8_t>&);
}
