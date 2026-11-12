#include "internal.h"
#include <string.h>
#include <c3d/uniforms.h>

C3D_FVec C3D_FVUnif[2][C3D_FVUNIF_COUNT];
C3D_IVec C3D_IVUnif[2][C3D_IVUNIF_COUNT];
u16      C3D_BoolUnifs[2];

bool C3D_FVUnifDirty[2][C3D_FVUNIF_COUNT] __attribute__((aligned(4)));
bool C3D_FVUnifAnyDirty[2];
bool C3D_FastState = true;
void (*C3D_UniformHook)(const void* program) = NULL;
bool C3D_TexCacheKeep = true;
u32 C3D_TexCacheClears, C3D_TexCacheKept;
bool C3D_IVUnifDirty[2][C3D_IVUNIF_COUNT];
bool C3D_BoolUnifsDirty[2];

static struct
{
	bool dirty;
	int count;
	float24Uniform_s* data;
} C3Di_ShaderFVecData[2];

static bool C3Di_FVUnifEverDirty[2][C3D_FVUNIF_COUNT];
static bool C3Di_IVUnifEverDirty[2][C3D_IVUNIF_COUNT];

// Last float uniform values uploaded to each register. A dirty uniform whose
// value is unchanged is not uploaded again.
static C3D_FVec C3Di_FVUnifShadow[2][C3D_FVUNIF_COUNT];
static bool C3Di_FVUnifShadowValid[2][C3D_FVUNIF_COUNT];

void C3Di_InvalidateUniformShadows(void)
{
	memset(C3Di_FVUnifShadowValid, 0, sizeof(C3Di_FVUnifShadowValid));
}

// The float uniform pass below in one walk (mkw-3ds): each dirty entry is
// compared with its shadow once, and a changed one is copied to it as the
// run that uploads it is found. The same runs, words and shadow state.
static void C3Di_UpdateFloatUniforms(GPU_SHADER_TYPE type, int offset)
{
	bool* dirty = C3D_FVUnifDirty[type];
	bool* valid = C3Di_FVUnifShadowValid[type];
	bool* ever = C3Di_FVUnifEverDirty[type];
	const C3Di_Word* values = (const C3Di_Word*)C3D_FVUnif[type];
	C3Di_Word* shadow = (C3Di_Word*)C3Di_FVUnifShadow[type];
	int i = 0;
	while (i < C3D_FVUNIF_COUNT)
	{
		if (!(i & 3))
		{
			u32 word;
			memcpy(&word, __builtin_assume_aligned(&dirty[i], 4), sizeof(word));
			if (!word)
			{
				i += 4;
				continue;
			}
		}
		if (!dirty[i])
		{
			i ++;
			continue;
		}
		const C3Di_Word* v = values + i*4;
		C3Di_Word* s = shadow + i*4;
		dirty[i] = false;
		ever[i] = true;
		if (valid[i] && v[0] == s[0] && v[1] == s[1] && v[2] == s[2] && v[3] == s[3])
		{
			i ++;
			continue;
		}
		s[0] = v[0]; s[1] = v[1]; s[2] = v[2]; s[3] = v[3];
		valid[i] = true;
		int j = i + 1;
		while (j < C3D_FVUNIF_COUNT && dirty[j])
		{
			const C3Di_Word* vj = values + j*4;
			C3Di_Word* sj = shadow + j*4;
			if (valid[j] && vj[0] == sj[0] && vj[1] == sj[1] && vj[2] == sj[2] && vj[3] == sj[3])
				break;
			sj[0] = vj[0]; sj[1] = vj[1]; sj[2] = vj[2]; sj[3] = vj[3];
			valid[j] = true;
			dirty[j] = false;
			ever[j] = true;
			j ++;
		}
		GPUCMD_AddWrite(GPUREG_VSH_FLOATUNIFORM_CONFIG+offset, 0x80000000|i);
		GPUCMD_AddWrites(GPUREG_VSH_FLOATUNIFORM_DATA+offset, (u32*)&C3D_FVUnif[type][i], (j-i)*4);
		i = j;
	}
}

void C3D_UpdateUniforms(GPU_SHADER_TYPE type)
{
	int offset = type == GPU_GEOMETRY_SHADER ? (GPUREG_GSH_BOOLUNIFORM-GPUREG_VSH_BOOLUNIFORM) : 0;
	int i = 0;

	// Update FVec uniforms that come from shader constants
	if (C3Di_ShaderFVecData[type].dirty)
	{
		while (i < C3Di_ShaderFVecData[type].count)
		{
			float24Uniform_s* u = &C3Di_ShaderFVecData[type].data[i++];
			GPUCMD_AddIncrementalWrites(GPUREG_VSH_FLOATUNIFORM_CONFIG+offset, (u32*)u, 4);
			C3D_FVUnifDirty[type][u->id] = false;
			C3Di_FVUnifShadowValid[type][u->id] = false;
		}
		C3Di_ShaderFVecData[type].dirty = false;
		i = 0;
	}

	// Update FVec uniforms. A dirty uniform whose value is already in its
	// register (shadow) is not uploaded again. The loop clears every dirty
	// entry, so with none set it is skipped.
	const bool* dirty = C3D_FVUnifDirty[type];
	if (C3D_FastState)
	{
		if (C3D_FVUnifAnyDirty[type])
			C3Di_UpdateFloatUniforms(type, offset);
		i = C3D_FVUNIF_COUNT;
	}
	C3D_FVUnifAnyDirty[type] = false;
	while (i < C3D_FVUNIF_COUNT)
	{
		// Skip four clean entries at a time (the array is 4-byte aligned)
		if (!(i & 3))
		{
			u32 word;
			memcpy(&word, __builtin_assume_aligned(&dirty[i], 4), sizeof(word));
			if (!word)
			{
				i += 4;
				continue;
			}
		}
		if (!dirty[i])
		{
			i ++;
			continue;
		}
		if (C3Di_FVUnifShadowValid[type][i] && C3Di_SameWords(&C3D_FVUnif[type][i], &C3Di_FVUnifShadow[type][i], sizeof(C3D_FVec)))
		{
			C3D_FVUnifDirty[type][i] = false;
			C3Di_FVUnifEverDirty[type][i] = true;
			i ++;
			continue;
		}

		// Find the number of consecutive dirty, changed uniforms
		int j;
		for (j = i + 1; j < C3D_FVUNIF_COUNT && dirty[j]
			&& !(C3Di_FVUnifShadowValid[type][j] && C3Di_SameWords(&C3D_FVUnif[type][j], &C3Di_FVUnifShadow[type][j], sizeof(C3D_FVec))); j ++);

		// Upload the uniforms
		GPUCMD_AddWrite(GPUREG_VSH_FLOATUNIFORM_CONFIG+offset, 0x80000000|i);
		GPUCMD_AddWrites(GPUREG_VSH_FLOATUNIFORM_DATA+offset, (u32*)&C3D_FVUnif[type][i], (j-i)*4);

		// Clear the dirty flag
		int k;
		for (k = i; k < j; k ++)
		{
			C3D_FVUnifDirty[type][k] = false;
			C3Di_FVUnifEverDirty[type][k] = true;
			C3Di_FVUnifShadow[type][k] = C3D_FVUnif[type][k];
			C3Di_FVUnifShadowValid[type][k] = true;
		}

		// Advance
		i = j;
	}

	// Update IVec uniforms
	for (i = 0; i < C3D_IVUNIF_COUNT; i ++)
	{
		if (!C3D_IVUnifDirty[type][i]) continue;

		GPUCMD_AddWrite(GPUREG_VSH_INTUNIFORM_I0+offset+i, C3D_IVUnif[type][i]);
		C3D_IVUnifDirty[type][i] = false;
		C3Di_IVUnifEverDirty[type][i] = false;
	}

	// Update bool uniforms
	if (C3D_BoolUnifsDirty[type])
	{
		GPUCMD_AddWrite(GPUREG_VSH_BOOLUNIFORM+offset, 0x7FFF0000 | C3D_BoolUnifs[type]);
		C3D_BoolUnifsDirty[type] = false;
	}
}

void C3Di_DirtyUniforms(GPU_SHADER_TYPE type)
{
	int i;
	memset(C3Di_FVUnifShadowValid[type], 0, sizeof(C3Di_FVUnifShadowValid[type]));
	C3D_BoolUnifsDirty[type] = true;
	if (C3Di_ShaderFVecData[type].count)
		C3Di_ShaderFVecData[type].dirty = true;
	for (i = 0; i < C3D_FVUNIF_COUNT; i ++)
		C3D_FVUnifDirty[type][i] = C3D_FVUnifDirty[type][i] || C3Di_FVUnifEverDirty[type][i];
	C3D_FVUnifAnyDirty[type] = true;
	for (i = 0; i < C3D_IVUNIF_COUNT; i ++)
		C3D_IVUnifDirty[type][i] = C3D_IVUnifDirty[type][i] || C3Di_IVUnifEverDirty[type][i];
}

void C3Di_LoadShaderUniforms(shaderInstance_s* si)
{
	GPU_SHADER_TYPE type = si->dvle->type;
	if (si->boolUniformMask)
	{
		C3D_BoolUnifs[type] &= ~si->boolUniformMask;
		C3D_BoolUnifs[type] |= si->boolUniforms;
	}

	if (type == GPU_GEOMETRY_SHADER)
		C3D_BoolUnifs[type] &= ~BIT(15);
	C3D_BoolUnifsDirty[type] = true;

	if (si->intUniformMask)
	{
		int i;
		for (i = 0; i < 4; i ++)
		{
			if (si->intUniformMask & BIT(i))
			{
				C3D_IVUnif[type][i] = si->intUniforms[i];
				C3D_IVUnifDirty[type][i] = true;
			}
		}
	}
	C3Di_ShaderFVecData[type].dirty = true;
	C3Di_ShaderFVecData[type].count = si->numFloat24Uniforms;
	C3Di_ShaderFVecData[type].data = si->float24Uniforms;
}

void C3Di_ClearShaderUniforms(GPU_SHADER_TYPE type)
{
	C3Di_ShaderFVecData[type].dirty = false;
	C3Di_ShaderFVecData[type].count = 0;
	C3Di_ShaderFVecData[type].data = NULL;
}
