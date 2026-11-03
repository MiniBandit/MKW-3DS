#pragma once
#include <array>
#include <cstdint>
namespace mkw::led {
// Retail standard kart body: marked badge-side paint and upper-right paint.
inline constexpr std::array<std::array<uint32_t,2>,24> StandardKartColors{{
  {{0xff0000u,0x000e9cu}}, // Mario
  {{0x007d08u,0x000e9cu}}, // Luigi
  {{0xfe768cu,0xff7a89u}}, // Peach
  {{0x00ff00u,0x6bbe00u}}, // Yoshi
  {{0xff0000u,0x000e9cu}}, // Baby Mario
  {{0xfe768cu,0xff7a89u}}, // Baby Peach
  {{0xff0000u,0xffffffu}}, // Toad
  {{0x39ae00u,0x33aefcu}}, // Koopa Troopa
  {{0xa71183u,0xf1ba00u}}, // Wario
  {{0xfede04u,0x00004cu}}, // Waluigi
  {{0x7e5310u,0x311000u}}, // Donkey Kong
  {{0xf97b0au,0x1e3905u}}, // Bowser
  {{0xf1a310u,0xe76f00u}}, // Baby Daisy
  {{0x007d08u,0x000e9cu}}, // Baby Luigi
  {{0xe7094fu,0x8900d3u}}, // Birdo
  {{0xf1a310u,0xe76f00u}}, // Daisy
  {{0xff0000u,0x916100u}}, // Diddy Kong
  {{0x866f47u,0x423808u}}, // Dry Bones
  {{0x454442u,0xa08c52u}}, // Dry Bowser
  {{0x1867cbu,0xfcf8deu}}, // Funky Kong
  {{0xffffffu,0xffffffu}}, // King Boo
  {{0x0042b8u,0x0aae96u}}, // Rosalina
  {{0xff207cu,0xfcf8deu}}, // Toadette
  {{0x285c00u,0xf7d91bu}}, // Bowser Jr.
}};
}
