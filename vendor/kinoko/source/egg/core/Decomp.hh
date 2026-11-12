#pragma once

#include <Common.hh>

namespace Kinoko::EGG::Decomp {

[[nodiscard]] s32 GetExpandSize(const u8 *src);
s32 DecodeSZS(const u8 *src, u8 *dst);
/// mkw-3ds: the same bytes as DecodeSZSReference (retail's loop), with whole
/// groups of literals, run copies in 8-byte steps where the source is 8 or
/// more bytes back, and one bounds check per run. Used by DecodeSZS unless
/// fastDecode is false (main.cpp: no-fast-yaz0.txt).
s32 DecodeSZSReference(const u8 *src, u8 *dst);
inline bool fastDecode = true;

} // namespace Kinoko::EGG::Decomp
