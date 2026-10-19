#pragma once
#include <array>
#include <cstdint>
namespace mkw {
using NativeMii=std::array<std::uint8_t,0x5c>;
using LicenseMiiId=std::array<std::uint8_t,8>;
// Keep the native appearance intact: Wii's smaller feature set cannot
// represent every face offered by the 3DS selector.
struct LicenseIdentity {
 NativeMii nativeMii{};
 std::array<std::uint16_t,10> name{};
};
// Legacy ID uses the Wii 2006 epoch / four-second ticks; native data uses
// 2010 / two-second ticks. The original native appearance remains separate.
LicenseMiiId licenseMiiId(const NativeMii&);
bool loadNativeMii(const char* directory,const LicenseMiiId&,NativeMii&);
bool writeNativeMii(const char* directory,const NativeMii&);
}
