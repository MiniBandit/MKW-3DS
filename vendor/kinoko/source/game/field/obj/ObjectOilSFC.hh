#pragma once

#include "game/field/obj/ObjectCollidable.hh"

namespace Kinoko::Field {

class ObjectOilSFC final : public ObjectCollidable {
public:
    ObjectOilSFC(const System::MapdataGeoObj &params);
    ~ObjectOilSFC() override;

    Kart::Reaction onCollision(Kart::KartObject *kartObj, Kart::Reaction reactionOnKart,
            Kart::Reaction reactionOnObj, EGG::Vector3f &hitDepth) override;

    /// @addr{0x806DDA80} Empty in retail (no kart object-hit sound).
    void onObjectCollision(Kart::KartObject *) override {}
};

} // namespace Kinoko::Field
