#pragma once
#include <c3d/attribs.h>
#include <c3d/buffers.h>
#include <c3d/proctex.h>
#include <c3d/light.h>
#include <c3d/framebuffer.h>
#include <c3d/texenv.h>
#include <c3d/fog.h>

#include <string.h>
#include <c3d/uniforms.h>

#define C3D_UNUSED __attribute__((unused))

typedef struct
{
	u32 fragOpMode;
	u32 fragOpShadow;
	u32 zScale, zOffset;
	GPU_CULLMODE cullMode;
	bool zBuffer, earlyDepth;
	GPU_EARLYDEPTHFUNC earlyDepthFunc;
	u32 earlyDepthRef;

	u32 alphaTest;
	u32 stencilMode, stencilOp;
	u32 depthTest;

	u32 blendClr;
	u32 alphaBlend;
	GPU_LOGICOP clrLogicOp;
} C3D_Effect;

typedef struct
{
	gxCmdQueue_s gxQueue;
	u32* cmdBuf;
	size_t cmdBufSize;
	float cmdBufUsage;

	u32 flags;
	shaderProgram_s* program;

	C3D_AttrInfo attrInfo;
	C3D_BufInfo bufInfo;
	C3D_Effect effect;
	C3D_LightEnv* lightEnv;

	u32 texConfig;
	u32 texShadow;
	C3D_Tex* tex[3];
	C3D_TexEnv texEnv[6];

	u32 texEnvBuf, texEnvBufClr;
	u32 fogClr;
	C3D_FogLut* fogLut;

	u16 gasAttn, gasAccMax;
	u32 gasLightXY, gasLightZ, gasLightZColor;
	u32 gasDeltaZ : 24;
	u32 gasFlags : 8;
	C3D_GasLut* gasLut;

	C3D_ProcTex* procTex;
	C3D_ProcTexLut* procTexLut[3];
	C3D_ProcTexColorLut* procTexColorLut;

	C3D_FrameBuf fb;
	u32 viewport[5];
	u32 scissor[3];

	u16 fixedAttribDirty, fixedAttribEverDirty;
	C3D_FVec fixedAttribs[12];
} C3D_Context;

enum
{
	C3DiF_Active = BIT(0),
	C3DiF_DrawUsed = BIT(1),
	C3DiF_AttrInfo = BIT(2),
	C3DiF_BufInfo = BIT(3),
	C3DiF_Effect = BIT(4),
	C3DiF_FrameBuf = BIT(5),
	C3DiF_Viewport = BIT(6),
	C3DiF_Scissor = BIT(7),
	C3DiF_Program = BIT(8),
	C3DiF_TexEnvBuf = BIT(9),
	C3DiF_LightEnv = BIT(10),
	C3DiF_VshCode = BIT(11),
	C3DiF_GshCode = BIT(12),
	C3DiF_TexStatus = BIT(14),
	C3DiF_ProcTex = BIT(15),
	C3DiF_ProcTexColorLut = BIT(16),
	C3DiF_FogLut = BIT(17),
	C3DiF_Gas = BIT(18),
	C3DiF_GasLut = BIT(19),

#define C3DiF_ProcTexLut(n) BIT(20+(n))
	C3DiF_ProcTexLutAll = 7 << 20,
#define C3DiF_Tex(n) BIT(23+(n))
	C3DiF_TexAll = 7 << 23,
#define C3DiF_TexEnv(n) BIT(26+(n))
	C3DiF_TexEnvAll = 0x3F << 26,
};

enum
{
	C3DiG_BeginAcc    = BIT(0),
	C3DiG_AccStage    = BIT(1),
	C3DiG_SetAccMax   = BIT(2),
	C3DiG_RenderStage = BIT(3),
};

// memcmp(a, b, bytes) == 0 for word-aligned state whose size is a multiple
// of four, without the library call (mkw-3ds: the shadows below are compared
// on every draw). The same bytes are compared, padding included.
typedef u32 __attribute__((may_alias)) C3Di_Word;
static inline bool C3Di_SameWords(const void* a, const void* b, unsigned bytes)
{
	if (!C3D_FastState)
		return memcmp(a, b, bytes) == 0;
	const C3Di_Word* x = (const C3Di_Word*)a;
	const C3Di_Word* y = (const C3Di_Word*)b;
	unsigned i;
	for (i = 0; i < bytes/4; i ++)
		if (x[i] != y[i])
			return false;
	return true;
}

// libctru's GPUCMD_Add (one command of at most 256 parameters: the first
// parameter, the header with the count, the rest, a zero to an even length)
// written in place (mkw-3ds: state is written on almost every draw). Larger
// commands, a full buffer or !C3D_FastState take GPUCMD_Add itself.
static inline void C3Di_CmdAdd(u32 header, const u32* param, u32 count)
{
	if (!C3D_FastState || !param || count - 1 >= 256 || !gpuCmdBuf || gpuCmdBufOffset + count + 2 > gpuCmdBufSize)
	{
		GPUCMD_Add(header, param, count);
		return;
	}
	u32* p = gpuCmdBuf + gpuCmdBufOffset;
	u32 i;
	p[0] = param[0];
	p[1] = header | ((count - 1) << 20);
	for (i = 1; i < count; i ++)
		p[1 + i] = param[i];
	gpuCmdBufOffset += count + 1;
	if ((count - 1) & 1)
		gpuCmdBuf[gpuCmdBufOffset ++] = 0;
}
static inline void C3Di_CmdAddOne(u32 header, u32 param)
{
	if (!C3D_FastState || !gpuCmdBuf || gpuCmdBufOffset + 2 > gpuCmdBufSize)
	{
		GPUCMD_Add(header, &param, 1);
		return;
	}
	u32* p = gpuCmdBuf + gpuCmdBufOffset;
	p[0] = param;
	p[1] = header;
	gpuCmdBufOffset += 2;
}
#undef GPUCMD_AddMaskedWrite
#undef GPUCMD_AddWrite
#undef GPUCMD_AddMaskedWrites
#undef GPUCMD_AddWrites
#undef GPUCMD_AddMaskedIncrementalWrites
#undef GPUCMD_AddIncrementalWrites
#define GPUCMD_AddMaskedWrite(reg, mask, val) C3Di_CmdAddOne(GPUCMD_HEADER(0, (mask), (reg)), (val))
#define GPUCMD_AddWrite(reg, val) GPUCMD_AddMaskedWrite((reg), 0xF, (val))
#define GPUCMD_AddMaskedWrites(reg, mask, vals, num) C3Di_CmdAdd(GPUCMD_HEADER(0, (mask), (reg)), (vals), (num))
#define GPUCMD_AddWrites(reg, vals, num) GPUCMD_AddMaskedWrites((reg), 0xF, (vals), (num))
#define GPUCMD_AddMaskedIncrementalWrites(reg, mask, vals, num) C3Di_CmdAdd(GPUCMD_HEADER(1, (mask), (reg)), (vals), (num))
#define GPUCMD_AddIncrementalWrites(reg, vals, num) GPUCMD_AddMaskedIncrementalWrites((reg), 0xF, (vals), (num))

static inline C3D_Context* C3Di_GetContext(void)
{
	extern C3D_Context __C3D_Context;
	return &__C3D_Context;
}

static inline bool typeIsCube(GPU_TEXTURE_MODE_PARAM type)
{
	return type == GPU_TEX_CUBE_MAP || type == GPU_TEX_SHADOW_CUBE;
}

static inline bool C3Di_TexIs2D(C3D_Tex* tex)
{
	return !typeIsCube(C3D_TexGetType(tex));
}

static inline bool addrIsVRAM(const void* addr)
{
	u32 vaddr = (u32)addr;
	return vaddr >= OS_VRAM_VADDR && vaddr < OS_VRAM_VADDR + OS_VRAM_SIZE;
}

static inline vramAllocPos addrGetVRAMBank(const void* addr)
{
	u32 vaddr = (u32)addr;
	return vaddr < OS_VRAM_VADDR + OS_VRAM_SIZE/2 ? VRAM_ALLOC_A : VRAM_ALLOC_B;
}

void C3Di_UpdateContext(void);
void C3Di_AttrInfoBind(C3D_AttrInfo* info);
void C3Di_BufInfoBind(C3D_BufInfo* info);
void C3Di_FrameBufBind(C3D_FrameBuf* fb);
void C3Di_TexEnvBind(int id, C3D_TexEnv* env);
bool C3Di_SetTex(int unit, C3D_Tex* tex);
extern bool C3Di_TexCacheDirty;
void C3Di_EffectBind(C3D_Effect* effect);
void C3Di_GasUpdate(C3D_Context* ctx);

void C3Di_LightMtlBlend(C3D_Light* light);

void C3Di_DirtyUniforms(GPU_SHADER_TYPE type);
void C3Di_InvalidateUniformShadows(void);
void C3Di_InvalidateTexShadows(void);
void C3Di_InvalidateStateShadows(void);
void C3Di_LoadShaderUniforms(shaderInstance_s* si);
void C3Di_ClearShaderUniforms(GPU_SHADER_TYPE type);

bool C3Di_SplitFrame(u32** pBuf, u32* pSize);

void C3Di_RenderQueueInit(void);
void C3Di_RenderQueueExit(void);
void C3Di_RenderQueueWaitDone(void);
void C3Di_EnsureCommandSpace(void);
// GX_MemoryFill, held back until FrameEnd in a pipelined frame (c3d/pipeline.h).
void C3Di_MemoryFill(u32* buf0a, u32 buf0v, u32* buf0e, u16 control0, u32* buf1a, u32 buf1v, u32* buf1e, u16 control1);
void C3Di_RenderQueueEnableVBlank(void);
void C3Di_RenderQueueDisableVBlank(void);
