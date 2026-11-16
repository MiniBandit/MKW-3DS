#pragma once
// Which console the game behaves as. Every "New 3DS or not" choice goes
// through isNew3ds(), so one switch gives every Old 3DS code path.
//
// MKW_OLD3DS_TEST (DIAG variant o3dstest, tools/diag/compile.py): an Old 3DS
// test build for a New 3DS. With its RSF (tools/diag/package/diag-o3ds-test.rsf:
// SystemModeExt Legacy = the Old 3DS 80 MB mode, 268 MHz, no L2, no core 2)
// the game runs as on an Old 3DS: every Old 3DS path, no speedup, Old 3DS
// buttons only (no ZL, ZR or C-stick), no stereo 3D (as the game does there).
#include <3ds.h>
namespace mkw {
#ifdef MKW_OLD3DS_TEST
inline constexpr bool old3dsTestBuild=true;
inline bool isNew3ds(){return false;}
#else
inline constexpr bool old3dsTestBuild=false;
inline bool isNew3ds(){static const bool n=[]{bool v=false;APT_CheckNew3DS(&v);return v;}();return n;}
#endif
// Buttons an Old 3DS has: the test build drops the New 3DS-only ones.
inline u32 consoleKeys(u32 keys){
    if constexpr(old3dsTestBuild)return keys&~u32(KEY_ZL|KEY_ZR|KEY_CSTICK_RIGHT|KEY_CSTICK_LEFT|KEY_CSTICK_UP|KEY_CSTICK_DOWN);
    return keys;
}
}
