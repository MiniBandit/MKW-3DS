#pragma once

#include "game/field/obj/ObjectCollidable.hh"

namespace Kinoko::Field {

/// @brief Holder class for one of various Thwomp "modes".
class ObjectDossunc final : public ObjectCollidable {
public:
    ObjectDossunc(const System::MapdataGeoObj &params);

    /// Port: ObjectDirector::reinitForRace. init() starts from the current
    /// pose, which the course preview has moved.
    void prepareReinit() override {
        resetPoseToParams();
    }
    ~ObjectDossunc() override;

    /// @addr{0x80764A38}
    void load() override;
};

} // namespace Kinoko::Field
