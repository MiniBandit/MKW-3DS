#pragma once
#include <mkw/balloon_presentation.hpp>
#include <memory>

namespace mkw {
class Renderer;
class RaceEffects;
struct RaceAudioFrame;
// Balloon-only model/state allocation; clear only after the GPU is idle.
class BalloonBattleRuntime {
public:
    BalloonBattleRuntime();
    ~BalloonBattleRuntime();
    bool reset(unsigned racers,const std::array<RaceTeam,12>& teams);
    void clear();
    void step(RaceEffects& effects);
    void fillAudio(RaceAudioFrame& frame);
    void lose(unsigned racer,bool audible=true);
    void steal(unsigned thief,unsigned victim);
    void replenish(unsigned racer);
    void draw(Renderer& renderer);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
