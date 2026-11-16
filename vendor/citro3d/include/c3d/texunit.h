#pragma once
#include <3ds/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Local citro3d addition: texture unit 2 reads texture coordinate 1 instead
// of 2 (GPUREG_TEXUNIT_CONFIG bit 13), so units 1 and 2 can sample at the same
// coordinates. Stays set until called again with false.
void C3D_TexUnit2UseCoord1(bool enable);

#ifdef __cplusplus
}
#endif
