#pragma once
#include <3ds/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Pipelined frames (local citro3d patch). A frame begun with
// C3D_FRAME_PIPELINE does not wait for the previous frame: it builds into the
// other half of the command buffer while the GPU may still run that frame.
// Its command lists, memory fills and transfers are held back; C3D_FrameEnd
// waits for the previous frame, runs the pipeline hook, then queues them.
//
// Until FrameEnd the GPU may still read anything the previous frame used: a
// pipelined frame writes only buffers of its own (double-buffered), or calls
// C3D_FrameWaitPrevious first.
#define C3D_FRAME_PIPELINE BIT(2)

// Runs once per pipelined frame, after the previous frame has finished and
// before any of this frame's commands reach the GPU (outside the frame).
void C3D_FramePipelineHook(void (* hook)(void*), void* param);

// In a pipelined frame: wait until the previous frame has finished.
void C3D_FrameWaitPrevious(void);

// True between C3D_FrameBegin(C3D_FRAME_PIPELINE) and C3D_FrameEnd.
bool C3D_FramePipelined(void);

// On: a pipelined frame's FrameEnd queues only its drawing. Its screen
// transfers and swap run at the next wait for the GPU (normally the next
// pipelined frame's FrameEnd), once the previous present has been shown:
// every frame reaches the screen, one refresh after the next, however its
// GPU time falls against the VBlank. A frame already deferred is still shown at the next wait.
void C3D_FramePresentDefer(bool on);

// Deferred presents: the VBlank count (C3D_FrameCounter(0)) at which the
// frame being built began, set before its FrameEnd. With catch-up on, after a
// second of frames shown one refresh later than that allows (each waiting
// for the one before), one frame's present is dropped to get back.
void C3D_FramePresentStart(u32 vblank);
void C3D_FramePresentCatchUp(bool on);
// Presents dropped by catch-up since boot.
u32 C3D_FramePresentDrops(void);

#ifdef __cplusplus
}
#endif
