#pragma once
#include <mkw/race_teams.hpp>
#include <mkw/license_identity.hpp>
#include <array>
#include <cstdint>
#include <memory>
namespace mkw {
class Renderer;
class Texture;
class RaceGrid;
class RaceItems;
// The race bottom screen (docs/BOTTOM_SCREEN.md), laid out as Mario Kart 7's:
// the standings down the left (Wii result-board rows: position, face, held
// item), a top-down map of the course on the right that follows the player at
// a fixed orientation (tap the touch screen for the whole course; Battle
// always shows the whole arena and ignores the toggle), every
// racer's face at its live position, items on the course, and the race time
// (Wii CtrlRaceTime) at the top right, as Mario Kart DS shows it.
//
// Presentation only: the view is filled read-only from the race each
// presented frame. Memory is fixed at load: VRAM for the bottom target, one
// course map, its overview, UI atlas and optional object atlas; 2 x QuadCapacity quads of heap.
struct BottomRacer {
    float x=0,z=0;            // world position
    float hitAngle=0;         // native map portrait damage animation
    uint8_t character=0;      // retail character id (0..23)
    uint8_t rank=12;          // current position, 1..12
    uint8_t item=20;          // held item icon (ItemKind; 20 none)
    uint16_t score=0;
    bool local=false;RaceTeam team=RaceTeam::None;
};
struct BottomItem {
    float x=0,z=0;
    uint8_t marker=0;         // BottomMarker
    float radius=0;           // blast radius (world units) for blasts
};
enum class BottomMarker : uint8_t {Banana,Bobomb,GreenShell,RedShell,SpinyShell,BlastRed,BlastBlue,FakeBox};
struct BottomMapObject {
    float x[4]{},z[4]{},height=0; // projected model-local top silhouette
    uint16_t art=0;
};
struct BottomView {
    static constexpr unsigned MaxItems=24,MaxMapObjects=32;
    unsigned mapObjects=0,mapCourse=~0u;
    std::array<BottomMapObject,MaxMapObjects> mapObject{};
    unsigned racers=0;
    std::array<BottomRacer,12> racer{};
    unsigned items=0;
    std::array<BottomItem,MaxItems> item{};
    float headingX=0,headingZ=-1; // the local kart's forward direction (world XZ)
    bool mirror=false;
    bool battle=false;
    bool timeTrial=false;         // no standings; full-width map and centred timer
    bool timerVisible=false;      // CtrlRaceTime: shown once the countdown runs
    uint32_t milliseconds=0;      // official race time (the player's own once finished)
    bool standingsVisible=false;  // Mario Kart 7: from the start of the race
    uint32_t tick=0;              // simulation step (animations advance at 60 Hz)
    float fade=0;                 // black over the screen while leaving the race (1 opaque)
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
    // standings 12 x 6 (up to three score digits in battle), timer 10,
    // leave fade 1, plus at most 32 map objects = 276.
    static constexpr unsigned QuadCapacity=BottomView::MaxMapObjects+1+1+24+BottomView::MaxItems*4+12*3+1+2+12*6+10+1;
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
    bool setMiiPortrait(Renderer&,const NativeMii&);
    const Texture* miiPortrait() const; // resident through the result screen
    // Mario Kart 7's touch: detailed <-> whole course. Battle ignores it.
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
