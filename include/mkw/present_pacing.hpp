#pragma once
// Race frame presentation (pipelined frames, src/render_thread.cpp).
//
// citro3d swapped a frame's buffers when its GPU work finished and GSP shows a
// swapped buffer at the screen's next VBlank. A frame that finished just after
// a VBlank was still waiting to be shown when the next frame, lighter or started
// earlier, finished before that VBlank and replaced it: the screen showed the
// older picture for two refreshes, then jumped two ticks ahead (a hitch with no
// late loop frame and no slow GPU frame). Its display transfer also wrote into
// the buffer on screen.
//
// The fix (C3D_FramePresentDefer, c3d/pipeline.h): a pipelined frame's GPU
// queue holds only its drawing; its screen transfers and swap run when the
// next frame is submitted, after the previous present has been shown. The GPU
// never waits for the screen. Off-file: no-present-pacing.txt. DIAG builds
// count every present, replaced present and VBlank (linear_lock.cpp wraps
// gfxScreenSwapBuffers and citro3d's VBlank0 callback) for the race profile
// (runtime_profile.hpp).
#include <3ds.h>
#include <atomic>
#include <cstdint>

namespace mkw::presentPacing {
inline bool on=true;
// After a second of frames shown one refresh late, drop one present to get
// back (C3D_FramePresentCatchUp). Off by default: each drop showed as a
// repeat and a skip, 0.5-5 a minute on the console. On-file:
// present-catchup.txt.
inline bool catchUp=false;
// Bins of the top present's latency (main-loop VBlank to the VBlank it shows
// at: 1..4, then 5 or more) and of its time after the last VBlank (1 ms bins).
constexpr unsigned LatencyBins=5,PhaseBins=18;
struct Counters {
    std::atomic<uint32_t> vblanks{0},topPresents{0},topReplaced{0},bottomPresents{0},bottomReplaced{0};
    // VBlanks at which the top screen kept its picture: no top present since
    // the VBlank before (GSP event thread only: lastPresents).
    std::atomic<uint32_t> repeats{0};uint32_t lastPresents=0;
    std::atomic<uint64_t> vblankTick{0};
    // The main loop's VBlank count for the frame whose top present comes next
    // (set by the render thread before that frame's GPU work; 0: unknown).
    std::atomic<uint32_t> frameStart{0};
    std::atomic<uint32_t> latency[LatencyBins]{},phase[PhaseBins]{};
};
inline Counters counters;
}
