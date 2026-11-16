#include "internal.h"
#include <c3d/texunit.h>

void C3D_TexUnit2UseCoord1(bool enable)
{
	C3D_Context* ctx = C3Di_GetContext();

	if (!(ctx->flags & C3DiF_Active))
		return;

	// GPUREG_TEXUNIT_CONFIG bit 13: texture unit 2 samples texcoord1, not texcoord2.
	u32 config = enable ? (ctx->texConfig | BIT(13)) : (ctx->texConfig &~ BIT(13));
	if (config != ctx->texConfig)
	{
		ctx->texConfig = config;
		ctx->flags |= C3DiF_TexStatus;
	}
}
