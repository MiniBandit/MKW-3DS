#pragma once
#include <egg/core/SceneManager.hh>
namespace mkw {
class RaceGrid;
bool runControlChecks(Kinoko::EGG::SceneManager& scenes);
// QA runtime.log lines that show why a racer stops progressing, written after each race step:
// player 0's route-driver changes, fall, respawn point and landing, and for any racer whose race
// completion has not risen by 1% of a lap in 20 s (1200 steps, the CPU stuck handler's no-progress
// limit) a "QA stall" line every 20 s with its position, checkpoint, respawn point, completion and
// AI route point and target.
class QaRaceWatch {
public:
    void step(unsigned stepIndex,const RaceGrid& grid,void (*log)(const char*));
private:
    static constexpr unsigned Capacity=12;
    float best_[Capacity]{};unsigned since_[Capacity]{};
    bool racing_=false,route_=false,fall_=false,respawn_=false,landing_=false;
};
}
