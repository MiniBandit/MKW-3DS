#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace mkw {
class Renderer;
// The Grand Prix award ceremony, from the files the builder converts
// (tools/cook_ceremony.py, models/ceremony.json):
//  - a podium finish: the winning-run stage (course 55) with the podium stands,
//    the top three drivers standing on them in their award models (demowin) and
//    the cup's trophy, filmed by the award camera move (SCN0 demowin03);
//  - otherwise: the losing stage (course 56) with the player's driver (demooth),
//    filmed by the losing camera (SCN0 demolos01).
struct CeremonyDriver {
    uint8_t character=0; // retail character id (Kinoko order)
    bool bike=false;     // Daisy, Peach and Rosalina wear their bike outfits
};
struct CeremonySetup {
    bool podium=false;               // the player finished first to third
    unsigned cup=0;                  // 0-7: Mushroom ... Lightning
    std::array<CeremonyDriver,3> top{}; // first, second, third (podium)
    CeremonyDriver player{};         // the player's driver (losing scene)
    unsigned playerPlace=0;          // 0 = first
};

class CeremonyScene {
public:
    CeremonyScene();
    ~CeremonyScene();
    // Loads the stage, stands, trophy, drivers and camera. False when a file is missing.
    bool load(const CeremonySetup& setup);
    void release();
    // One 60 Hz tick; false once the scene has played out.
    bool step();
    // Whether the scene may be skipped yet (A/B/START).
    bool skippable() const;
    void draw(Renderer& renderer);
    unsigned music() const; // the stream to play: GP award or award lose
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Award model code for a retail character (as Demo/Award.szs names them).
const char* ceremonyCharacterCode(unsigned character,bool bike);
}
