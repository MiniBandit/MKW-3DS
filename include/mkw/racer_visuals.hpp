#pragma once
#include <memory>
#include <citro3d.h>
#include <mkw/math.hpp>
#include <mkw/driver_collision_look.hpp>
namespace mkw {
class Renderer;
class Model;
class RaceItems;
// Each opponent owns its deforming vertices. Textures remain shared by Model.
class RacerVisuals {
public:
    RacerVisuals();
    ~RacerVisuals();
    bool load();
    void reset();
    void resetAnimation();
    // Set reuse (New 3DS; main.cpp no-set-reuse.txt): park() keeps the
    // opponents for the next race of the set instead of releasing them. The
    // next load() takes a parked racer for a slot with the same character,
    // vehicle and team (settings unchanged: texture quality, opponent detail),
    // bakes its colours for the new course and resets its state as a fresh
    // load would; it releases the parked racers no slot takes before loading
    // the others. reset() releases parked racers too. Their models and
    // textures stay where they were (LINEAR) while parked.
    void park();
    static inline bool setReuse=false;
    // Racers the last load() took from the parked ones.
    unsigned reused() const;
    // drawNext false (Old 3DS): no frame is drawn before the next step, so racers
    // not shown up close keep last tick's vehicle snapshot (VehicleAnimation::step).
    void step(unsigned tick,const RaceItems& items,bool drawNext=true,std::span<const DriverCollisionLook> collisions={});
    // Old 3DS: a racer whose animated driver was not drawn last frame is presented
    // once per drawn frame (on the tick before it, drawNext), that step standing for
    // the ticks since its last: controller clocks and wheel roll catch up.
    static inline bool thinHidden=false;
    // Poses and culls every opponent for this frame, on the job worker where
    // possible. Call after Renderer::begin; race state must not change until draw.
    // fullDetail: no LOD at all (intro, results, replays, after the finish);
    // otherwise RenderQuality::racerLod picks our rule or the Wii's driver LOD.
    void prepare(const Renderer& renderer,const RaceItems& items,Vec3 viewer,unsigned tick,bool fullDetail=false);
    // Draws in racer order; prepares inline first when prepare() was not called.
    // Original vehicle shadow volumes resolve on the world before racers draw.
    void draw(Renderer& renderer,const RaceItems& items,Vec3 viewer,unsigned tick,
              const Model* playerShadow=nullptr,const C3D_Mtx* playerTransform=nullptr);
    // Replayed ghosts (RaceConfig Ghost players) are left out of draw(): call
    // after the opaque world, player and its shadow. Translucent, no shadow volume.
    void drawGhosts(Renderer& renderer);
    unsigned count() const;
    // QA: opponents drawn with the Wii's low-detail driver in the last draw().
    unsigned lowDriverCount() const;
private:
    void drawRacer(Renderer& renderer,unsigned i);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
