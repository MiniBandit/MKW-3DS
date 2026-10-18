#pragma once
#include <memory>
#include <citro3d.h>
#include <mkw/math.hpp>
namespace mkw {
class Renderer;
class Model;
class RaceItems;
class RaceGrid;
// Each opponent owns its deforming vertices. Textures remain shared by Model.
class RacerVisuals {
public:
    RacerVisuals();
    ~RacerVisuals();
    bool load();
    void reset();
    void resetAnimation();
    // Pause: retail never touches PlayerModel, so each racer's driver model, clip
    // clocks and vehicle animation are kept while its graphics are released and
    // put back on the racers load() rebuilds with the same model.
    void saveAnimation();
    void restoreAnimation();
    // Racers load() drew with their class's standard vehicle (port-only safety).
    unsigned fallbacks() const;
    // Driver models after the race step: kart state, this step's item events, ranks.
    // far: bit i = racer i is beyond its driver LOD radius (DriverSignals::far).
    void step(unsigned tick,const RaceItems& items,const RaceGrid& grid,uint32_t far);
    // Poses and culls every opponent for this frame, on the job worker where
    // possible. Call after Renderer::begin; race state must not change until draw.
    void prepare(const Renderer& renderer,const RaceItems& items,Vec3 viewer,unsigned tick);
    // Draws in racer order; prepares inline first when prepare() was not called.
    // Original vehicle shadow volumes resolve on the world before racers draw.
    void draw(Renderer& renderer,const RaceItems& items,Vec3 viewer,unsigned tick,
              const Model* playerShadow=nullptr,const C3D_Mtx* playerTransform=nullptr);
    // Time Trial ghosts, translucent and without shadows: after the player and the
    // water, before the effects, as one layer that leaves its depth. draw() leaves
    // them out.
    void drawGhosts(Renderer& renderer);
    unsigned count() const;
    // QA: racer 1..11's current driver clip (nullptr without that racer).
    const char* driverClip(unsigned racer) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
