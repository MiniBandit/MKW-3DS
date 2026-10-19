#pragma once
#include <array>
#include <cstdint>
#include <memory>
namespace mkw {
class Renderer;
class RaceGrid;
class RaceItems;
// The race bottom screen (docs/BOTTOM_SCREEN.md), laid out as Mario Kart 7's:
// the standings down the left (Wii result-board rows: position, face, held
// item), a top-down map of the course on the right that follows the player at
// a fixed orientation (tap the touch screen for the whole course), every
// racer's face at its live position, items on the course, and the race time
// (Wii CtrlRaceTime) at the top right, as Mario Kart DS shows it.
//
// Presentation only: the view is filled read-only from the race each
// presented frame. Memory is fixed at load: VRAM for the bottom target, one
// course map, its overview and the UI atlas; 2 x QuadCapacity quads of heap.
struct BottomRacer {
    float x=0,z=0;            // world position
    uint8_t character=0;      // retail character id (0..23)
    uint8_t rank=12;          // current position, 1..12
    uint8_t item=20;          // held item icon (ItemKind; 20 none)
    bool local=false;
};
struct BottomItem {
    float x=0,z=0;
    uint8_t marker=0;         // BottomMarker
    float radius=0;           // blast radius (world units) for blasts
};
enum class BottomMarker : uint8_t {Banana,Bobomb,GreenShell,RedShell,SpinyShell,BlastRed,BlastBlue,FakeBox};
struct BottomView {
    static constexpr unsigned MaxItems=24;
    unsigned racers=0;
    std::array<BottomRacer,12> racer{};
    unsigned items=0;
    std::array<BottomItem,MaxItems> item{};
    float headingX=0,headingZ=-1; // the local kart's forward direction (world XZ)
    bool timerVisible=false;      // CtrlRaceTime: shown once the countdown runs
    uint32_t milliseconds=0;      // official race time (the player's own once finished)
    bool standingsVisible=false;  // Mario Kart 7: from the start of the race
    uint32_t tick=0;              // simulation step (animations advance at 60 Hz)
    float fade=0;                 // black over the screen while leaving the race (1 opaque)
    // The course tour (the opening fly-over): the whole course is shown and the camera's
    // position and viewing direction move along it, as in Mario Kart DS.
    bool tour=false;
    float tourX=0,tourZ=0;        // camera position (world XZ)
    float tourHeadX=0,tourHeadZ=-1;
};
// Fills view from the running race, read-only: racer positions, ranks and
// characters, each racer's item window picture, the local kart's heading,
// items on the course, the official time (the player's own once finished).
// leaveFade is the top screen's fade when leaving after the race (RaceResults::leaveFade).
void fillBottomView(BottomView& view,const RaceGrid& grid,const RaceItems& items,bool introActive,uint32_t tick,float leaveFade);
class RaceBottomScreen {
public:
    // Worst case by construction: fill 1, map 1, start line 24, 24 items as
    // blast rings (4 each) 96, racers 12 x 3 + searchlight 1, column 2,
    // standings 12 x 4, timer 10, leave fade 1 = 220. The course tour's camera marker (4 quads)
    // shows only while the standings are hidden (the fly-over), so it fits the same budget.
    static constexpr unsigned QuadCapacity=1+1+24+BottomView::MaxItems*4+12*3+1+2+12*4+10+1;
    RaceBottomScreen();
    ~RaceBottomScreen();
    // Race load, render thread idle, after Renderer::reserveBloom() for a race
    // with bloom: creates the bottom target, then loads the course's map,
    // overview and the UI atlas into VRAM. A missing texture only drops what
    // it draws; false when the bottom screen cannot exist (it is then not
    // drawn). Reloading the same course is free.
    bool load(Renderer& renderer,unsigned course);
    // Frees everything (waits for the GPU) and blanks the lower LCD.
    void unload(Renderer& renderer);
    bool active() const;
    // Mario Kart 7's touch: the detailed view <-> the whole course.
    void toggleView();
    // Records the frame's bottom screen (after Renderer::beginBottom()).
    void draw(Renderer& renderer,const BottomView& view);
    // runtime.log line: what is resident.
    void describe(char* text,unsigned size) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
