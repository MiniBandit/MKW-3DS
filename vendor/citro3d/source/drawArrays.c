#include "internal.h"

void C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size)
{
	C3Di_UpdateContext();

	// mkw-3ds: one block of the same commands (see C3D_DrawElements).
	if (gpuCmdBuf && gpuCmdBufOffset + 22 <= gpuCmdBufSize)
	{
		u32* p = gpuCmdBuf + gpuCmdBufOffset;
#define C3DI_CMD(reg, mask, val) do { p[0] = (val); p[1] = GPUCMD_HEADER(0, (mask), (reg)); p += 2; } while (0)
		C3DI_CMD(GPUREG_PRIMITIVE_CONFIG, 2, primitive);
		C3DI_CMD(GPUREG_RESTART_PRIMITIVE, 0xF, 1);
		C3DI_CMD(GPUREG_INDEXBUFFER_CONFIG, 0xF, 0x80000000);
		C3DI_CMD(GPUREG_NUMVERTICES, 0xF, size);
		C3DI_CMD(GPUREG_VERTEX_OFFSET, 0xF, first);
		C3DI_CMD(GPUREG_GEOSTAGE_CONFIG2, 1, 1);
		C3DI_CMD(GPUREG_START_DRAW_FUNC0, 1, 0);
		C3DI_CMD(GPUREG_DRAWARRAYS, 0xF, 1);
		C3DI_CMD(GPUREG_START_DRAW_FUNC0, 1, 1);
		C3DI_CMD(GPUREG_GEOSTAGE_CONFIG2, 1, 0);
		C3DI_CMD(GPUREG_VTX_FUNC, 0xF, 1);
#undef C3DI_CMD
		gpuCmdBufOffset += 22;
		C3Di_GetContext()->flags |= C3DiF_DrawUsed;
		return;
	}

	// Set primitive type
	GPUCMD_AddMaskedWrite(GPUREG_PRIMITIVE_CONFIG, 2, primitive);
	// Start a new primitive (breaks off a triangle strip/fan)
	GPUCMD_AddWrite(GPUREG_RESTART_PRIMITIVE, 1);
	// The index buffer is not used, but this command is still required
	GPUCMD_AddWrite(GPUREG_INDEXBUFFER_CONFIG, 0x80000000);
	// Number of vertices
	GPUCMD_AddWrite(GPUREG_NUMVERTICES, size);
	// First vertex
	GPUCMD_AddWrite(GPUREG_VERTEX_OFFSET, first);
	// Enable array drawing mode
	GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 1, 1);
	// Enable drawing mode
	GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 0);
	// Trigger array drawing
	GPUCMD_AddWrite(GPUREG_DRAWARRAYS, 1);
	// Go back to configuration mode
	GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 1);
	// Disable array drawing mode
	GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 1, 0);
	// Clear the post-vertex cache
	GPUCMD_AddWrite(GPUREG_VTX_FUNC, 1);

	C3Di_GetContext()->flags |= C3DiF_DrawUsed;
}
