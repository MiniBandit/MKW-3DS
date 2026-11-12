#include "internal.h"

void C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void* indices)
{
	C3D_Context* ctx = C3Di_GetContext();
	u32 pa = osConvertVirtToPhys(indices);
	u32 base = ctx->bufInfo.base_paddr;
	if (pa < base) return;

	C3Di_UpdateContext();

	// mkw-3ds: the same single-parameter commands GPUCMD_Add would emit, written
	// as one block when they fit (GPUCMD_Add itself traps on overflow).
	const u32 words = primitive == GPU_TRIANGLES ? 30 : 22;
	if (gpuCmdBuf && gpuCmdBufOffset + words <= gpuCmdBufSize)
	{
		u32* p = gpuCmdBuf + gpuCmdBufOffset;
#define C3DI_CMD(reg, mask, val) do { p[0] = (val); p[1] = GPUCMD_HEADER(0, (mask), (reg)); p += 2; } while (0)
		C3DI_CMD(GPUREG_PRIMITIVE_CONFIG, 2, primitive != GPU_TRIANGLES ? primitive : GPU_GEOMETRY_PRIM);
		C3DI_CMD(GPUREG_RESTART_PRIMITIVE, 0xF, 1);
		C3DI_CMD(GPUREG_INDEXBUFFER_CONFIG, 0xF, (pa - base) | (type << 31));
		C3DI_CMD(GPUREG_NUMVERTICES, 0xF, count);
		C3DI_CMD(GPUREG_VERTEX_OFFSET, 0xF, 0);
		if (primitive == GPU_TRIANGLES)
		{
			C3DI_CMD(GPUREG_GEOSTAGE_CONFIG, 2, 0x100);
			C3DI_CMD(GPUREG_GEOSTAGE_CONFIG2, 2, 0x100);
		}
		C3DI_CMD(GPUREG_START_DRAW_FUNC0, 1, 0);
		C3DI_CMD(GPUREG_DRAWELEMENTS, 0xF, 1);
		C3DI_CMD(GPUREG_START_DRAW_FUNC0, 1, 1);
		if (primitive == GPU_TRIANGLES)
		{
			C3DI_CMD(GPUREG_GEOSTAGE_CONFIG, 2, 0);
			C3DI_CMD(GPUREG_GEOSTAGE_CONFIG2, 2, 0);
		}
		C3DI_CMD(GPUREG_VTX_FUNC, 0xF, 1);
		C3DI_CMD(GPUREG_PRIMITIVE_CONFIG, 0x8, 0);
		C3DI_CMD(GPUREG_PRIMITIVE_CONFIG, 0x8, 0);
#undef C3DI_CMD
		gpuCmdBufOffset += words;
		C3Di_GetContext()->flags |= C3DiF_DrawUsed;
		return;
	}

	// Set primitive type
	GPUCMD_AddMaskedWrite(GPUREG_PRIMITIVE_CONFIG, 2, primitive != GPU_TRIANGLES ? primitive : GPU_GEOMETRY_PRIM);
	// Start a new primitive (breaks off a triangle strip/fan)
	GPUCMD_AddWrite(GPUREG_RESTART_PRIMITIVE, 1);
	// Configure the index buffer
	GPUCMD_AddWrite(GPUREG_INDEXBUFFER_CONFIG, (pa - base) | (type << 31));
	// Number of vertices
	GPUCMD_AddWrite(GPUREG_NUMVERTICES, count);
	// First vertex
	GPUCMD_AddWrite(GPUREG_VERTEX_OFFSET, 0);
	// Enable triangle element drawing mode if necessary
	if (primitive == GPU_TRIANGLES)
	{
		GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG, 2, 0x100);
		GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 2, 0x100);
	}
	// Enable drawing mode
	GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 0);
	// Trigger element drawing
	GPUCMD_AddWrite(GPUREG_DRAWELEMENTS, 1);
	// Go back to configuration mode
	GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 1);
	// Disable triangle element drawing mode if necessary
	if (primitive == GPU_TRIANGLES)
	{
		GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG, 2, 0);
		GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 2, 0);
	}
	// Clear the post-vertex cache
	GPUCMD_AddWrite(GPUREG_VTX_FUNC, 1);
	GPUCMD_AddMaskedWrite(GPUREG_PRIMITIVE_CONFIG, 0x8, 0);
	GPUCMD_AddMaskedWrite(GPUREG_PRIMITIVE_CONFIG, 0x8, 0);

	C3Di_GetContext()->flags |= C3DiF_DrawUsed;
}
