#pragma once

#include "game/field/obj/ObjectCollidable.hh"

namespace Kinoko::Field {

class ObjectWoodboxWSub;

class ObjectWoodboxW final : public ObjectCollidable {
public:
    ObjectWoodboxW(const System::MapdataGeoObj &params);

    /// Port: ObjectDirector::reinitForRace. init() starts from the current
    /// pose, which the course preview has moved.
    void prepareReinit() override {
        resetPoseToParams();
    }
    ~ObjectWoodboxW() override;

    void init() override;
    void calc() override;

    /// @addr{0x8077ECDC}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

    /// @addr{0x8077ECD0}
    void createCollision() override {}

private:
    owning_span<ObjectWoodboxWSub *> m_boxes;
    s32 m_framesUntilSpawn;
    u32 m_nextBoxIdx;
};

} // namespace Kinoko::Field
