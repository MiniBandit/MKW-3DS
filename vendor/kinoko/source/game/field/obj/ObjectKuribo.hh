#pragma once

#include "game/field/StateManager.hh"
#include "game/field/obj/ObjectCollidable.hh"
#include <mkw/object_animation_sound.hpp>

namespace Kinoko::Field {

class ObjectKuribo : public ObjectCollidable, public StateManager {
public:
    ObjectKuribo(const System::MapdataGeoObj &params);
    ~ObjectKuribo() override;

    void init() override;
    void calc() override;

    /// @addr{0x806DD2C8}
    [[nodiscard]] u32 loadFlags() const override {
        return 3;
    }

    void loadAnims() override;

    [[nodiscard]] f32 animTimer() const {
        return m_damagePhase == DamagePhase::Alive ? m_animTimer : m_hitAnimFrame;
    }

    [[nodiscard]] bool visible() const { return m_visible; }
    Kart::Reaction onCollision(Kart::KartObject *kart, Kart::Reaction reactionOnKart,
            Kart::Reaction reactionOnObj, EGG::Vector3f &hitDepth) override;
    s16 onItemCollision(const EGG::Vector3f &position, const EGG::Vector3f &velocity,
            s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &hitDepth) override;
    void onGlobalItemCollision(s16 reactionOnItem, s16 reactionOnObj) override;

private:
    enum class DamagePhase { Alive, Squash, Hidden, Growing };
    DamagePhase m_damagePhase = DamagePhase::Alive;
    EGG::Vector3f m_initialScale = EGG::Vector3f(1.0f, 1.0f, 1.0f);
    u32 m_damageFrame = 0, m_flatFrame = 0;
    s32 m_recoveryDelay = 0;
    f32 m_damageHeight = 1.0f, m_damageWidth = 1.0f, m_hitAnimFrame = 0.0f;
    bool m_visible = true;
    void beginSquash();
    void dropMushroom(const EGG::Vector3f &velocity, u8 owner);
    void calcDamage();
    void enterSquashState() { beginSquash(); }
    void enterHiddenState();
    void calcDamageState() { calcAnim(); }
    void enterStateStub();
    void calcStateReroute();
    void calcStateWalk();

    void calcAnim();
    void calcRot();
    void checkSphereFull();

    f32 m_speedStep;
    f32 m_animStep;
    EGG::Vector3f m_origin;
    f32 m_maxAnimTimer;
    u32 m_frameCount;
    f32 m_currSpeed;
    EGG::Vector3f m_rot;
    EGG::Vector3f m_floorNrm;
    f32 m_animTimer;
    mkw::ObjectAnimationSound m_animationSound;

    static constexpr std::array<StateManagerEntry, 4> STATE_ENTRIES = {{
            {StateEntry<ObjectKuribo, &ObjectKuribo::enterStateStub,
                    &ObjectKuribo::calcStateReroute>(0)},
            {StateEntry<ObjectKuribo, &ObjectKuribo::enterStateStub, &ObjectKuribo::calcStateWalk>(
                    1)},
            {StateEntry<ObjectKuribo, &ObjectKuribo::enterSquashState, &ObjectKuribo::calcDamageState>(
                    2)},
            {StateEntry<ObjectKuribo, &ObjectKuribo::enterHiddenState, &ObjectKuribo::calcDamageState>(
                    3)},
    }};
};

} // namespace Kinoko::Field
