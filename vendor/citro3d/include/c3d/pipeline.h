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

#ifdef __cplusplus
}
#endif
