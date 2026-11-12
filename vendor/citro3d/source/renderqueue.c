#include "internal.h"
#include <c3d/base.h>
#include <c3d/renderqueue.h>
#include <c3d/pipeline.h>
#include <stdlib.h>

static C3D_RenderTarget *firstTarget, *lastTarget;
static C3D_RenderTarget *linkedTarget[3];

static TickCounter gpuTime, cpuTime;

static bool inFrame, inSafeTransfer, measureGpuTime;

// Pipelined frames (c3d/pipeline.h). The frame's command buffer: the whole
// buffer, or one half in a pipelined frame. lastHalf is the half the last
// submitted frame used (-1: the whole buffer).
static u32* frameCmdBuf;
static u32 frameCmdBufSize;
static int lastHalf = -1, frameHalf = -1;
static bool pipelined, pipelineHookDone;
static void (* pipelineHook)(void*);
static void* pipelineHookData;
// GX operations a pipelined frame holds back until the previous frame has
// finished. The GX queue itself holds 32 entries per frame.
enum { StageList, StageFill, StageDisplay, StageCopy };
typedef struct { u8 type; u16 c0, c1; u32 a[6]; } StagedOp;
static StagedOp staged[32];
static unsigned stagedCount;
// libctru marks a queue idle before invoking its callback. The renderer may
// run on a different core: fence the callback's final swap/state writes too.
// Only the submitting thread accesses queueCallbackPending; the callback
// publishes its completion through this one-shot event.
static LightEvent queueCallbackDone;
static bool queueCallbackPending;
static bool needSwapTop, needSwapBot, isTopStereo;
static float framerate = 60.0f;
static float framerateCounter[2] = { 60.0f, 60.0f };
static u32 frameCounter[2];
static void (* frameEndCb)(void*);
static void* frameEndCbData;

static void C3Di_RenderTargetDestroy(C3D_RenderTarget* target);

static bool framerateLimit(int id)
{
	framerateCounter[id] -= framerate;
	if (framerateCounter[id] <= 0.0f)
	{
		framerateCounter[id] += 60.0f;
		return true;
	}
	return false;
}

static void onVBlank0(C3D_UNUSED void* unused)
{
	if (framerateLimit(0))
		frameCounter[0]++;
}

static void onVBlank1(C3D_UNUSED void* unused)
{
	if (framerateLimit(1))
		frameCounter[1]++;
}

static void onQueueFinish(gxCmdQueue_s* queue)
{
	if (measureGpuTime)
	{
		osTickCounterUpdate(&gpuTime);
		measureGpuTime = false;
	}
	if (inSafeTransfer)
	{
		inSafeTransfer = false;
		if (inFrame)
		{
			gxCmdQueueStop(queue);
			gxCmdQueueClear(queue);
		}
	}
	else
	{
		if (needSwapTop)
		{
			gfxScreenSwapBuffers(GFX_TOP, isTopStereo);
			needSwapTop = false;
		}
		if (needSwapBot)
		{
			gfxScreenSwapBuffers(GFX_BOTTOM, false);
			needSwapBot = false;
		}
	}
	LightEvent_Signal(&queueCallbackDone);
}

void C3D_FrameSync(void)
{
	u32 cur[2];
	u32 start[2] = { frameCounter[0], frameCounter[1] };
	do
	{
		gspWaitForAnyEvent();
		cur[0] = frameCounter[0];
		cur[1] = frameCounter[1];
	} while (cur[0]==start[0] || cur[1]==start[1]);
}

u32 C3D_FrameCounter(int id)
{
	return frameCounter[id];
}

static void C3Di_RunQueue(gxCmdQueue_s* queue)
{
	// Running an empty libctru queue leaves it active: a later Add can then
	// auto-start without passing this fence. Keep empty queues inactive.
	if (queue->lastEntry >= queue->numEntries) return;
	// Arm before Run, which may finish on the other core immediately.
	queueCallbackPending = true;
	gxCmdQueueRun(queue);
}

static bool C3Di_WaitAndClearQueue(s64 timeout)
{
	gxCmdQueue_s* queue = &C3Di_GetContext()->gxQueue;
	if (!gxCmdQueueWait(queue, timeout))
		return false;
	if (queueCallbackPending)
	{
		if (timeout == 0)
		{
			if (!LightEvent_TryWait(&queueCallbackDone)) return false;
		}
		else if (timeout < 0) LightEvent_Wait(&queueCallbackDone);
		else if (LightEvent_WaitTimeout(&queueCallbackDone, timeout)) return false;
		queueCallbackPending = false;
	}
	gxCmdQueueStop(queue);
	gxCmdQueueClear(queue);
	return true;
}

void C3Di_RenderQueueEnableVBlank(void)
{
	gspSetEventCallback(GSPGPU_EVENT_VBlank0, onVBlank0, NULL, false);
	gspSetEventCallback(GSPGPU_EVENT_VBlank1, onVBlank1, NULL, false);
}

void C3Di_RenderQueueDisableVBlank(void)
{
	gspSetEventCallback(GSPGPU_EVENT_VBlank0, NULL, NULL, false);
	gspSetEventCallback(GSPGPU_EVENT_VBlank1, NULL, NULL, false);
}

void C3Di_RenderQueueInit(void)
{
	C3D_Context* ctx = C3Di_GetContext();

	LightEvent_Init(&queueCallbackDone, RESET_ONESHOT);
	queueCallbackPending = false;
	C3Di_RenderQueueEnableVBlank();

	GX_BindQueue(&ctx->gxQueue);
	gxCmdQueueSetCallback(&ctx->gxQueue, onQueueFinish, NULL);
	C3Di_RunQueue(&ctx->gxQueue);
}

void C3Di_RenderQueueExit(void)
{
	int i;
	C3D_RenderTarget *a, *next;

	C3Di_WaitAndClearQueue(-1);
	gxCmdQueueSetCallback(&C3Di_GetContext()->gxQueue, NULL, NULL);
	GX_BindQueue(NULL);

	C3Di_RenderQueueDisableVBlank();

	for (i = 0; i < 3; i ++)
		linkedTarget[i] = NULL;

	for (a = firstTarget; a; a = next)
	{
		next = a->next;
		C3Di_RenderTargetDestroy(a);
	}
}

void C3Di_RenderQueueWaitDone(void)
{
	C3Di_WaitAndClearQueue(-1);
}

float C3D_FrameRate(float fps)
{
	float old = framerate;
	if (fps > 0.0f && fps <= 60.0f)
	{
		framerate = fps;
		framerateCounter[0] = fps;
		framerateCounter[1] = fps;
	}
	return old;
}

static void C3Di_Replay(const StagedOp* op)
{
	const u32* a = op->a;
	switch (op->type)
	{
		case StageList:
			GX_ProcessCommandList((u32*)a[0], a[1], (u8)op->c0);
			break;
		case StageFill:
			GX_MemoryFill((u32*)a[0], a[1], (u32*)a[2], op->c0, (u32*)a[3], a[4], (u32*)a[5], op->c1);
			break;
		case StageDisplay:
			GX_DisplayTransfer((u32*)a[0], a[1], (u32*)a[2], a[3], a[4]);
			break;
		case StageCopy:
			GX_TextureCopy((u32*)a[0], a[1], (u32*)a[2], a[3], a[4], a[5]);
			break;
	}
}

// Wait for the previous frame, run the hook once, then queue the held-back
// operations (stopped: they run at the next C3Di_RunQueue).
static void C3Di_PipelineSubmit(void)
{
	unsigned i;
	C3Di_WaitAndClearQueue(-1);
	if (!pipelineHookDone)
	{
		pipelineHookDone = true;
		if (pipelineHook)
			pipelineHook(pipelineHookData);
	}
	for (i = 0; i < stagedCount; i ++)
		C3Di_Replay(&staged[i]);
	stagedCount = 0;
}

static void C3Di_PipelineFlush(void)
{
	// Submit what this frame holds so far and continue in the same buffer.
	C3D_Context* ctx = C3Di_GetContext();
	C3Di_PipelineSubmit();
	C3Di_RunQueue(&ctx->gxQueue);
	C3Di_WaitAndClearQueue(-1);
}

static void C3Di_Stage(const StagedOp* op)
{
	if (stagedCount == sizeof(staged)/sizeof(staged[0]))
	{
		// A full queue's worth: what is held so far runs now.
		C3Di_PipelineFlush();
	}
	staged[stagedCount++] = *op;
}

void C3D_FramePipelineHook(void (* hook)(void*), void* param)
{
	pipelineHook = hook;
	pipelineHookData = param;
}

void C3D_FrameWaitPrevious(void)
{
	// The queue holds only the previous frame: this frame's operations are held back.
	if (inFrame && pipelined)
		C3Di_WaitAndClearQueue(-1);
}

bool C3D_FramePipelined(void)
{
	return inFrame && pipelined;
}

void C3Di_MemoryFill(u32* buf0a, u32 buf0v, u32* buf0e, u16 control0, u32* buf1a, u32 buf1v, u32* buf1e, u16 control1)
{
	if (inFrame && pipelined)
	{
		StagedOp op = { StageFill, control0, control1, { (u32)buf0a, buf0v, (u32)buf0e, (u32)buf1a, buf1v, (u32)buf1e } };
		C3Di_Stage(&op);
	} else
		GX_MemoryFill(buf0a, buf0v, buf0e, control0, buf1a, buf1v, buf1e, control1);
}

bool C3D_FrameBegin(u8 flags)
{
	C3D_Context* ctx = C3Di_GetContext();
	if (inFrame) return false;

	if (flags & C3D_FRAME_SYNCDRAW)
		C3D_FrameSync();
	if (flags & C3D_FRAME_PIPELINE)
	{
		// The GPU may still run the previous frame from its half: build into
		// the other one. A frame that used the whole buffer finishes first.
		if (lastHalf < 0 && !C3Di_WaitAndClearQueue((flags & C3D_FRAME_NONBLOCK) ? 0 : -1))
			return false;
		frameHalf = lastHalf == 0 ? 1 : 0;
		frameCmdBufSize = ctx->cmdBufSize / 2;
		frameCmdBuf = ctx->cmdBuf + frameHalf * frameCmdBufSize;
		pipelined = true;
		pipelineHookDone = false;
		stagedCount = 0;
	} else
	{
		if (!C3Di_WaitAndClearQueue((flags & C3D_FRAME_NONBLOCK) ? 0 : -1))
			return false;
		frameHalf = -1;
		frameCmdBuf = ctx->cmdBuf;
		frameCmdBufSize = ctx->cmdBufSize;
		pipelined = false;
	}

	inFrame = true;
	C3Di_TexCacheDirty = true;
	osTickCounterStart(&cpuTime);
	GPUCMD_SetBuffer(frameCmdBuf, frameCmdBufSize, 0);
	return true;
}

bool C3D_FrameDrawOn(C3D_RenderTarget* target)
{
	if (!inFrame) return false;

	target->used = true;
	C3Di_TexCacheDirty = true;
	C3D_SetFrameBuf(&target->frameBuf);
	C3D_SetViewport(0, 0, target->frameBuf.width, target->frameBuf.height);
	return true;
}

void C3D_FrameSplit(u8 flags)
{
	u32 *cmdBuf, cmdBufSize;
	if (!inFrame) return;
	C3Di_TexCacheDirty = true;
	if (!C3Di_SplitFrame(&cmdBuf, &cmdBufSize))
		return;
	if (pipelined)
	{
		// Usage of this frame's half, not of the whole buffer.
		C3Di_GetContext()->cmdBufUsage = (float)(cmdBuf + cmdBufSize - frameCmdBuf) / frameCmdBufSize;
		StagedOp op = { StageList, flags, 0, { (u32)cmdBuf, cmdBufSize*4 } };
		C3Di_Stage(&op);
	} else
		GX_ProcessCommandList(cmdBuf, cmdBufSize*4, flags);
}

// Local continuation patch. Reserve 64 KiB for a complete context upload,
// one draw and list termination. The application uses a 1 MiB command buffer.
// This runs between draws, before UpdateContext emits any pending state.
void C3Di_EnsureCommandSpace(void)
{
	C3D_Context* ctx = C3Di_GetContext();
	if (!inFrame || !gpuCmdBufOffset || gpuCmdBufOffset + 0x4000 <= gpuCmdBufSize)
		return;
	// Draw buffers can also have been updated by the CPU during this frame.
	extern u32 __ctru_linear_heap;
	extern u32 __ctru_linear_heap_size;
	GSPGPU_FlushDataCache((void*)__ctru_linear_heap, __ctru_linear_heap_size);
	C3D_FrameSplit(GX_CMDLIST_FLUSH);
	if (pipelined)
		C3Di_PipelineFlush();
	else
	{
		C3Di_RunQueue(&ctx->gxQueue);
		C3Di_WaitAndClearQueue(-1);
	}
	// PICA state survives a command-list boundary. Dirty state stays pending.
	GPUCMD_SetBuffer(frameCmdBuf, frameCmdBufSize, 0);
}

void C3D_FrameEnd(u8 flags)
{
	C3D_Context* ctx = C3Di_GetContext();
	if (!inFrame) return;

	if (frameEndCb)
		frameEndCb(frameEndCbData);

	C3D_FrameSplit(flags);
	GPUCMD_SetBuffer(NULL, 0, 0);
	osTickCounterUpdate(&cpuTime);
	inFrame = false;

	// Flush the entire linear memory if the user did not explicitly mandate to flush the command list
	if (!(flags & GX_CMDLIST_FLUSH))
	{
		extern u32 __ctru_linear_heap;
		extern u32 __ctru_linear_heap_size;
		GSPGPU_FlushDataCache((void*)__ctru_linear_heap, __ctru_linear_heap_size);
	}

	// A pipelined frame reaches the queue once the previous one has finished
	// and been swapped: its transfers below then target the free framebuffers.
	if (pipelined)
	{
		C3Di_PipelineSubmit();
		pipelined = false;
	}
	lastHalf = frameHalf;

	int i;
	C3D_RenderTarget* target;
	isTopStereo = false;
	for (i = 2; i >= 0; i --)
	{
		target = linkedTarget[i];
		if (!target || !target->used)
			continue;
		target->used = false;
		C3D_FrameBufTransfer(&target->frameBuf, target->screen, target->side, target->transferFlags);
		if (target->screen == GFX_TOP)
		{
			needSwapTop = true;
			if (target->side == GFX_RIGHT)
				isTopStereo = true;
		}
		else if (target->screen == GFX_BOTTOM)
			needSwapBot = true;
	}

	measureGpuTime = true;
	osTickCounterStart(&gpuTime);
	C3Di_RunQueue(&ctx->gxQueue);
}

void C3D_FrameEndHook(void (* hook)(void*), void* param)
{
	frameEndCb = hook;
	frameEndCbData = param;
}

float C3D_GetDrawingTime(void)
{
	return osTickCounterRead(&gpuTime);
}

float C3D_GetProcessingTime(void)
{
	return osTickCounterRead(&cpuTime);
}

static C3D_RenderTarget* C3Di_RenderTargetNew(void)
{
	C3D_RenderTarget* target = (C3D_RenderTarget*)malloc(sizeof(C3D_RenderTarget));
	if (!target) return NULL;
	memset(target, 0, sizeof(C3D_RenderTarget));
	return target;
}

static void C3Di_RenderTargetFinishInit(C3D_RenderTarget* target)
{
	target->prev = lastTarget;
	target->next = NULL;
	if (lastTarget)
		lastTarget->next = target;
	if (!firstTarget)
		firstTarget = target;
	lastTarget = target;
}

C3D_RenderTarget* C3D_RenderTargetCreate(int width, int height, GPU_COLORBUF colorFmt, C3D_DEPTHTYPE depthFmt)
{
	GPU_DEPTHBUF depthFmtReal = GPU_RB_DEPTH16;
	void* depthBuf = NULL;
	void* colorBuf = vramAlloc(C3D_CalcColorBufSize(width,height,colorFmt));
	if (!colorBuf) goto _fail0;
	if (C3D_DEPTHTYPE_OK(depthFmt))
	{
		depthFmtReal = C3D_DEPTHTYPE_VAL(depthFmt);
		size_t depthSize = C3D_CalcDepthBufSize(width,height,depthFmtReal);
		vramAllocPos vramBank = addrGetVRAMBank(colorBuf);
		depthBuf = vramAllocAt(depthSize, vramBank ^ VRAM_ALLOC_ANY); // Attempt opposite bank first...
		if (!depthBuf) depthBuf = vramAllocAt(depthSize, vramBank); // ... if that fails, attempt same bank
		if (!depthBuf) goto _fail1;
	}

	C3D_RenderTarget* target = C3Di_RenderTargetNew();
	if (!target) goto _fail2;

	C3D_FrameBuf* fb = &target->frameBuf;
	C3D_FrameBufAttrib(fb, width, height, false);
	C3D_FrameBufColor(fb, colorBuf, colorFmt);
	target->ownsColor = true;
	if (depthBuf)
	{
		C3D_FrameBufDepth(fb, depthBuf, depthFmtReal);
		target->ownsDepth = true;
	}
	C3Di_RenderTargetFinishInit(target);
	return target;

_fail2:
	if (depthBuf) vramFree(depthBuf);
_fail1:
	vramFree(colorBuf);
_fail0:
	return NULL;
}

C3D_RenderTarget* C3D_RenderTargetCreateFromTex(C3D_Tex* tex, GPU_TEXFACE face, int level, C3D_DEPTHTYPE depthFmt)
{
	if (!addrIsVRAM(tex->data)) return NULL; // Render targets must be in VRAM
	C3D_RenderTarget* target = C3Di_RenderTargetNew();
	if (!target) return NULL;

	C3D_FrameBuf* fb = &target->frameBuf;
	C3D_FrameBufTex(fb, tex, face, level);

	if (C3D_DEPTHTYPE_OK(depthFmt))
	{
		GPU_DEPTHBUF depthFmtReal = C3D_DEPTHTYPE_VAL(depthFmt);
		size_t depthSize = C3D_CalcDepthBufSize(fb->width,fb->height,depthFmtReal);
		vramAllocPos vramBank = addrGetVRAMBank(tex->data);
		void* depthBuf = vramAllocAt(depthSize, vramBank ^ VRAM_ALLOC_ANY); // Attempt opposite bank first...
		if (!depthBuf) depthBuf = vramAllocAt(depthSize, vramBank); // ... if that fails, attempt same bank
		if (!depthBuf)
		{
			free(target);
			return NULL;
		}

		C3D_FrameBufDepth(fb, depthBuf, depthFmtReal);
		target->ownsDepth = true;
	}

	C3Di_RenderTargetFinishInit(target);
	return target;
}

void C3Di_RenderTargetDestroy(C3D_RenderTarget* target)
{
	if (target->ownsColor)
		vramFree(target->frameBuf.colorBuf);
	if (target->ownsDepth)
		vramFree(target->frameBuf.depthBuf);

	C3D_RenderTarget** prevNext = target->prev ? &target->prev->next : &firstTarget;
	C3D_RenderTarget** nextPrev = target->next ? &target->next->prev : &lastTarget;
	*prevNext = target->next;
	*nextPrev = target->prev;
	free(target);
}

void C3D_RenderTargetDelete(C3D_RenderTarget* target)
{
	if (inFrame)
		svcBreak(USERBREAK_PANIC); // Shouldn't happen.
	if (target->linked)
		C3D_RenderTargetDetachOutput(target);
	else
		C3Di_WaitAndClearQueue(-1);
	C3Di_RenderTargetDestroy(target);
}

void C3D_RenderTargetSetOutput(C3D_RenderTarget* target, gfxScreen_t screen, gfx3dSide_t side, u32 transferFlags)
{
	int id = 0;
	if (screen==GFX_BOTTOM) id = 2;
	else if (side==GFX_RIGHT) id = 1;
	if (linkedTarget[id])
	{
		linkedTarget[id]->linked = false;
		if (!inFrame)
			C3Di_WaitAndClearQueue(-1);
	}
	linkedTarget[id] = target;
	if (target)
	{
		target->linked = true;
		target->transferFlags = transferFlags;
		target->screen = screen;
		target->side = side;
	}
}

static void C3Di_SafeDisplayTransfer(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 flags)
{
	C3Di_WaitAndClearQueue(-1);
	inSafeTransfer = true;
	GX_DisplayTransfer(inadr, indim, outadr, outdim, flags);
	C3Di_RunQueue(&C3Di_GetContext()->gxQueue);
}

static void C3Di_SafeTextureCopy(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 size, u32 flags)
{
	C3Di_WaitAndClearQueue(-1);
	inSafeTransfer = true;
	GX_TextureCopy(inadr, indim, outadr, outdim, size, flags);
	C3Di_RunQueue(&C3Di_GetContext()->gxQueue);
}

static void C3Di_SafeMemoryFill(u32* buf0a, u32 buf0v, u32* buf0e, u16 control0, u32* buf1a, u32 buf1v, u32* buf1e, u16 control1)
{
	C3Di_WaitAndClearQueue(-1);
	inSafeTransfer = true;
	GX_MemoryFill(buf0a, buf0v, buf0e, control0, buf1a, buf1v, buf1e, control1);
	C3Di_RunQueue(&C3Di_GetContext()->gxQueue);
}

void C3D_SyncDisplayTransfer(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 flags)
{
	if (inFrame)
	{
		C3D_FrameSplit(0);
		if (pipelined)
		{
			StagedOp op = { StageDisplay, 0, 0, { (u32)inadr, indim, (u32)outadr, outdim, flags } };
			C3Di_Stage(&op);
		} else
			GX_DisplayTransfer(inadr, indim, outadr, outdim, flags);
	} else
	{
		C3Di_SafeDisplayTransfer(inadr, indim, outadr, outdim, flags);
		C3Di_WaitAndClearQueue(-1);
	}
}

void C3D_SyncTextureCopy(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 size, u32 flags)
{
	if (inFrame)
	{
		C3D_FrameSplit(0);
		if (pipelined)
		{
			StagedOp op = { StageCopy, 0, 0, { (u32)inadr, indim, (u32)outadr, outdim, size, flags } };
			C3Di_Stage(&op);
		} else
			GX_TextureCopy(inadr, indim, outadr, outdim, size, flags);
	} else
	{
		C3Di_SafeTextureCopy(inadr, indim, outadr, outdim, size, flags);
		C3Di_WaitAndClearQueue(-1);
	}
}

void C3D_SyncMemoryFill(u32* buf0a, u32 buf0v, u32* buf0e, u16 control0, u32* buf1a, u32 buf1v, u32* buf1e, u16 control1)
{
	if (inFrame)
	{
		C3D_FrameSplit(0);
		C3Di_MemoryFill(buf0a, buf0v, buf0e, control0, buf1a, buf1v, buf1e, control1);
	} else
	{
		C3Di_SafeMemoryFill(buf0a, buf0v, buf0e, control0, buf1a, buf1v, buf1e, control1);
		C3Di_WaitAndClearQueue(-1);
	}
}
