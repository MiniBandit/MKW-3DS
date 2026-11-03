#pragma once
#include <mkw/license_identity.hpp>
#include <vector>
namespace mkw {
// Main thread, between frames after GPU/asset work is drained. Builds only
// the selected 3DS identity and normal expression, then releases all CFL data.
// Transparent top-down straight RGBA. Never writes the system Mii database.
bool composeNativeMiiPortrait(const NativeMii&,unsigned,std::vector<std::uint8_t>&);
// Staff's authored photo is a billboard too. Bake its front-facing native
// head with the exact portrait material/depth/light path, without the body.
bool composeNativeMiiPhotoHead(const NativeMii&,unsigned,bool blink,std::vector<std::uint8_t>&);
}
