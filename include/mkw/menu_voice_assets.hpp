#pragma once
#include <cstdint>
namespace mkw {
// Original USA REL tables 808ace50/808acf10: [retail character][event].
// Inclusive sound ranges; event 1 falls back to 0 when it contains VO_DUMMY.
struct MenuVoiceRange {std::uint16_t first,last;};
inline constexpr MenuVoiceRange MenuVoiceRanges[24][2]={
 {{923,925},{1067,1069}},
 {{953,955},{1088,1090}},
 {{974,976},{1097,1099}},
 {{977,979},{914,914}},
 {{956,958},{1091,1093}},
 {{965,967},{914,914}},
 {{947,949},{1082,1084}},
 {{926,928},{1070,1072}},
 {{962,964},{914,914}},
 {{980,982},{914,914}},
 {{929,931},{1073,1075}},
 {{971,973},{1094,1096}},
 {{950,952},{1085,1087}},
 {{959,961},{914,914}},
 {{968,970},{914,914}},
 {{935,937},{1079,1081}},
 {{932,934},{914,914}},
 {{938,940},{914,914}},
 {{944,946},{914,914}},
 {{983,985},{914,914}},
 {{941,943},{914,914}},
 {{989,991},{914,914}},
 {{986,988},{914,914}},
 {{992,994},{914,914}},
};
// Authored sel_gut CHR0 durations, in MenuState driver storage order.
inline constexpr unsigned MenuSelectAnimationFrames[24]={60,60,61,75,60,66,60,60,60,60,61,61,51,60,60,61,60,65,71,78,61,71,60,61};
}
