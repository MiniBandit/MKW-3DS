#pragma once

#include "game/field/obj/ObjectCollidable.hh"
#include <mkw/object_animation_sound.hpp>

namespace Kinoko::Field {

class ObjectSanbo final : public ObjectCollidable {
public:
    ObjectSanbo(const System::MapdataGeoObj &params);
    ~ObjectSanbo() override;

    void init() override;
    void calc() override;
    Kart::Reaction onCollision(Kart::KartObject *kart, Kart::Reaction onKart,
            Kart::Reaction onObject, EGG::Vector3f &depth) override;
    s16 onItemCollision(const EGG::Vector3f &position, const EGG::Vector3f &velocity,
            s16 onItem, s16 onObject, EGG::Vector3f &depth) override;
    void onGlobalItemCollision(s16 onItem, s16 onObject) override;
    bool scattered() const { return m_recoveryTimer > 0; }
    bool reviving() const { return m_reviveTimer > 0; }
    const EGG::Matrix34f &partTransform(unsigned i) const { return m_parts[i].matrix; }
    bool flinching() const { return m_flinchTimer > 0; }
    f32 animationFrame() const { return m_animationFrame; }

    /// @addr{0x8077BD38}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

private:
    struct Part {
        EGG::Matrix34f initial, matrix;
        EGG::Vector3f position, velocity, angles, angularVelocity;
    };
    std::array<Part,4> m_parts;
    s32 m_recoveryTimer = 0, m_reviveTimer = 0;
    mkw::ObjectAnimationSound m_animationSound;
    void scatter(const EGG::Vector3f &depth, const EGG::Vector3f &velocity);
    void flinch(s16 reaction);
    s32 m_flinchTimer = 0;
    f32 m_animationFrame = 0.0f;
    void calcMove();
    void checkSphere();

    bool m_standstill;   ///< Whether or not the pokey is currently walking
    u32 m_stillDuration; ///< Frames before the pokey will start to walk
    f32 m_yVel;          ///< Falling speed
    EGG::Vector3f m_up;
    EGG::Vector3f m_tangent;
};

} // namespace Kinoko::Field
