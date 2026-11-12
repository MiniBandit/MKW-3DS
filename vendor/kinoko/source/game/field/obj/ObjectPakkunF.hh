#pragma once

#include "game/field/obj/ObjectCollidable.hh"
#include <mkw/object_animation_sound.hpp>

namespace Kinoko::Field {

/// @brief Represents the piranhas on GCN Mario Circuit.
/// @details This object does not include the pipe that is normally found underneath the piranha.
class ObjectPakkunF final : public ObjectCollidable {
public:
    ObjectPakkunF(const System::MapdataGeoObj &params);
    ~ObjectPakkunF() override;

    void init() override;
    void calc() override;
    Kart::Reaction onCollision(Kart::KartObject *kart, Kart::Reaction reactionOnKart,
            Kart::Reaction reactionOnObj, EGG::Vector3f &hitDepth) override;
    s16 onItemCollision(const EGG::Vector3f &position, const EGG::Vector3f &velocity,
            s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &hitDepth) override;
    void onGlobalItemCollision(s16, s16) override { enterDamage(); }
    [[nodiscard]] bool damaged() const { return m_state == State::Damage; }
    [[nodiscard]] u32 damageFrame() const { return m_damageFrame; }

    /// @addr{0x807754F4}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

    void loadAnims() override;
    void calcCollisionTransform() override;

    /// @addr{0x80775458}
    [[nodiscard]] f32 getCollisionRadius() const override {
        return 1000.0f;
    }

    [[nodiscard]] bool attacking() const {
        return m_state == State::Attack;
    }

    [[nodiscard]] u32 attackFrame() const {
        return m_currAttackFrame;
    }
    [[nodiscard]] u32 waitAnimationFrame() const { return m_waitAnimationFrame; }
    [[nodiscard]] f32 turnAngle() const { return m_turnAngle; }

private:
    enum class State {
        Wait = 0,
        Attack = 1,
        Damage = 2,
    };

    void calcWait();
    void calcAttack();
    void enterAttack(s32 player = -1);
    void enterDamage();
    void calcDamage();
    u32 m_damageFrame = 0;

    State m_state;
    mkw::ObjectAnimationSound m_animationSound;
    u32 m_waitAnimationFrame = 0;
    s32 m_waitFrames;      ///< How long until the piranha starts chomping
    s32 m_attackFrames;    ///< How long until the piranha stops chomping
    u32 m_currAttackFrame; /// How long the piranhas has been attacking for
    bool m_scheduled = false;
    EGG::Vector3f m_initialForward;
    f32 m_targetAngle = 0.0f, m_turnAngle = 0.0f;
    s32 m_targetCooldown = 0;
    static s32 s_recentPlayer;

    /// Total time the piranha will be idle for. Not in the base game, but this prevents
    /// having to dereference m_mapObj multiple times.
    const u32 m_waitDuration;
};

} // namespace Kinoko::Field
