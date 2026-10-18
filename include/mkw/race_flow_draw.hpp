#pragma once
#include <mkw/race_flow_pose.hpp>
#include <array>
#include <cstdint>

namespace mkw {
class RaceResultsFlow;
// Screen-space quad of one NW4R material: corners TL, TR, BL, BR in top-screen
// pixels, texcoords with v=0 the top image row, rgba vertex colours with the
// pane alpha applied (the layout of Renderer::MaterialQuad).
struct FlowQuad {float x[4],y[4],u[4],v[4];uint32_t color[4];};
// One draw: lerp(color0, color1, texture) x vertex colour for `count` quads.
// texture indexes FlowResultTextures (-1: none); wrap modes are GX (0 clamp,
// 1 repeat, 2 mirror).
struct FlowBatch {int texture;uint8_t wrapS,wrapT;uint32_t color0,color1;const FlowQuad* quads;unsigned count;};
using FlowSink=void(*)(const FlowBatch&,void* context);
// A drawn pane's screen rectangle and depth (Section::Draw LEQUAL depth).
struct FlowOccluder {float x0,y0,x1,y1,z;};
// The course preview's title card (DemoPage_onInit, PAL 0x80855200): the
// course name and the cup line (text and FlowDemoTextures icon index).
struct FlowDemoCard {const char* course=nullptr;char cup[64]{};int8_t icon=-1;bool cupVisible=true;};

// The result boards and end-of-race buttons as material batches, independent
// of the 3DS renderer (src/race_flow_ui.cpp submits them; host tools can
// rasterize them for review). Rows draw pane by pane across all rows so each
// pane state costs one batch.
class FlowDrawer {
public:
 void reset();
 void step(const RaceResultsFlow& flow); // one 60 Hz tick
 void draw(const RaceResultsFlow& flow,FlowSink sink,void* context);
 // The static title card; batch textures index FlowDemoTextures' font (0) and
 // FlowDemoIconSlot for the card's icon.
 void drawDemo(const FlowDemoCard& card,FlowSink sink,void* context);
 static constexpr unsigned MaxQuads=192; // one pane across all rows (names: ~11 glyphs x 12)
private:
 struct Batch {std::array<FlowQuad,MaxQuads> quads;unsigned count=0;FlowQuad* add(){return count<MaxQuads?&quads[count++]:nullptr;}};
 struct Button {std::array<FlowPlay,4> plays;}; // Loop, Select, SelectIn, OK
 void flush(FlowSink sink,void* context,int texture,uint8_t wrapS,uint8_t wrapT,uint32_t c0,uint32_t c1);
 unsigned boardFrame=0;
 std::array<Button,2> buttons{};bool menuStarted=false;unsigned lastCursor=0;int chosen=-1;
 Batch batch;
 // Working state kept off the stack: the race loop runs on the main thread,
 // whose frame (main) already uses most of its 32 KB.
 std::array<FlowPose,2> poses{};FlowPose buttonPose{};
 std::array<FlowOccluder,FlowPose::MaxPanes> occluders{};
};
}
