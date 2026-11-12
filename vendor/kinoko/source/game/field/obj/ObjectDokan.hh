#pragma once

#include "game/field/obj/ObjectCollidable.hh"
#include <mkw/object_animation_sound.hpp>

namespace Kinoko::Field {

class ObjectDokan final : public ObjectCollidable {
public:
    ObjectDokan(const System::MapdataGeoObj &params);
    ~ObjectDokan() override;

    void init() override;
    void calc() override;
    [[nodiscard]] f32 animationFrame() const { return m_animationFrame; }

    /// @addr{0x80778FE4}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

    void calcCollisionTransform() override;

    Kart::Reaction onCollision(Kart::KartObject *kartObj, Kart::Reaction reactionOnKart,
            Kart::Reaction reactionOnObj, EGG::Vector3f &hitDepth) override;

    s16 onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
            s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) override;
    void onGlobalItemCollision(s16 reactionOnItem, s16 reactionOnObj) override;

private:
    void launch(s16 reactionOnObj);
    void calcFloor();

    bool m_b0;
    EGG::Vector3f m_velocity;
    f32 m_animationFrame = -1.0f;
    mkw::ObjectAnimationSound m_animationSound;
};

} // namespace Kinoko::Field
