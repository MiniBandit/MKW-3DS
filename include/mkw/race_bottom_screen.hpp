#pragma once
#include <array>
#include <cstdint>
#include <memory>
namespace mkw {
class Renderer;
class RaceGrid;
class RaceItems;
// The race bottom screen: retail Mario Kart Wii's minimap (CtrlRace2DMap, PAL
// 0x807EA27C) moved to the lower LCD, beside the standings (Wii result-board
// rows: position, face, held item) and the race time (Wii CtrlRaceTime).
//
// The map is Map2DRenderer's capture of the course's map_model.brres
// (tools/cook_race_map.py), one texel per pixel, with the racers' map icons
// (CtrlRace2DMapCharacter) and the start line, as retail draws them.
//
// Presentation only: the view is filled read-only from the race each
// presented frame. Memory is fixed at load: VRAM for the bottom target, one
// course capture and the UI atlas; 2 x QuadCapacity quads of heap.
struct BottomRacer {
    float x=0,z=0;            // world position
    uint8_t character=0;      // retail character id (0..23)
    uint8_t rank=12;          // current position, 1..12
    uint8_t item=20;          // held item icon (ItemKind; 20 none)
    bool local=false;
    // A Time Trial ghost that has finished: its map icon hides
    // (CtrlRace2DMapCharacter_calcSelf PAL 0x807EB250).
    bool ghost=false,finished=false;
    // The map icon's spin (CtrlRace2DMapCharacter_calcSelf PAL 0x807EB250): Kinoko status
    // InAction (32), and KartAction's start count so an action shorter than a presented
    // frame still starts it.
    bool inAction=false;
    uint32_t actionStarts=0;
};
struct BottomView {
    unsigned racers=0;
    std::array<BottomRacer,12> racer{};
    float headingX=0,headingZ=-1; // the local kart's smoothed forward direction (world XZ)
    bool timerVisible=false;      // CtrlRaceTime: shown once the countdown runs (also the map's HasStarted)
    uint32_t milliseconds=0;      // official race time (the player's own once finished)
    bool standingsVisible=false;  // Mario Kart 7: from the start of the race
    bool timeTrial=false;         // Time Trial: the other icons are drawn larger
    bool replay=false;            // a watched ghost replay: no map and no time (GhostReplayRacePage)
    uint32_t tick=0;              // simulation step (animations advance at 60 Hz)
    float exitSlide=-1;           // the RaceHUD page's exit slide (RaceResultsFlow::hudSlide; -1 before)
    float fade=0;                 // black over the screen while leaving the race (1 opaque)
    bool mirror=false;            // Mirror mode: the map flips left to right with the race view
};
// Fills view from the running race, read-only: racer positions, ranks and
// characters, each racer's item window picture, the local kart's heading and
// the official time (the player's own once finished).
// leaveFade is the top screen's fade when leaving after the race (RaceResults::leaveFade).
void fillBottomView(BottomView& view,const RaceGrid& grid,const RaceItems& items,bool introActive,uint32_t tick,float leaveFade);
class RaceBottomScreen {
public:
    // Worst case by construction: fill 1, map 1, start line 1, map icons 12
    // faces + the local racer's searchlight and two shadows 3, column 2,
    // standings 12 x 4, timer 10, leave fade 1 = 79.
    static constexpr unsigned QuadCapacity=1+1+1+12+3+2+12*4+10+1;
    RaceBottomScreen();
    ~RaceBottomScreen();
    // Race load, render thread idle, after Renderer::reserveBloom() for a race
    // with bloom: creates the bottom target, then loads the course's minimap
    // capture and the UI atlas into VRAM. A missing texture only drops what it
    // draws; false when the bottom screen cannot exist (it is then not drawn).
    // Reloading the same course is free.
    bool load(Renderer& renderer,unsigned course);
    // Frees everything (waits for the GPU) and blanks the lower LCD.
    void unload(Renderer& renderer);
    bool active() const;
    // Records the frame's bottom screen (after Renderer::beginBottom()).
    void draw(Renderer& renderer,const BottomView& view);
    // runtime.log line: what is resident.
    void describe(char* text,unsigned size) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
