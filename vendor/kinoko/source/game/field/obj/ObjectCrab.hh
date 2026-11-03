#pragma once

#include "game/field/obj/ObjectCollidable.hh"
#include <mkw/object_animation_sound.hpp>

namespace Kinoko::Field {

class ObjectCrab final : public ObjectCollidable {
public:
    ObjectCrab(const System::MapdataGeoObj &params);
    ~ObjectCrab() override;

    void init() override;
    void calc() override;
    Kart::Reaction onCollision(Kart::KartObject *kart, Kart::Reaction onKart,
            Kart::Reaction onObject, EGG::Vector3f &depth) override;
    void processItemReactions(s16 type, bool direct, s16 &onItem, s16 &onObject) override;
    s16 onItemCollision(const EGG::Vector3f &position, const EGG::Vector3f &velocity,
            s16 onItem, s16 onObject, EGG::Vector3f &depth) override;
    void onGlobalItemCollision(s16 onItem, s16 onObject) override;
    [[nodiscard]] bool visible() const { return m_visible; }
    [[nodiscard]] u32 effectSerial(unsigned index) const { return m_effectSerial[index]; }
    [[nodiscard]] const EGG::Vector3f &effectPosition(unsigned index) const { return m_effectPos[index]; }

    /// 0 walking, 1 still, 2 deactivated, 3 resurfacing.
    [[nodiscard]] s32 state() const {
        return static_cast<s32>(m_state);
    }

    [[nodiscard]] bool backwards() const {
        return m_backwards;
    }

    // Presentation uses the object's simulation clock, including while culled.
    // Clip order matches course_object_kinds: walk_l, walk_r, wait.
    [[nodiscard]] u32 animationClip() const { return m_animationClip; }
    [[nodiscard]] f32 animationFrame() const { return m_animationFrame; }

    /// @addr{0x808864DC}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

private:
    enum class State {
        Walking = 0,
        Still = 1,
        Deactivated = 2,
        Resurfacing = 3,
    };

    enum class StatePhase {
        Start = 0,
        Middle = 1,
        End = 2,
    };

    enum class StateResult {
        Walking = 0,
        Middle = 1,
        BeginWalking = 2,
    };

    bool calcRail();
    void calcMotion();
    StateResult calcState();

    void calcCurRot(const EGG::Vector3f &rot);
    void calcTransMat(const EGG::Vector3f &rot);
    void startAnimation(u32 clip, f32 rate);
    void hit(const EGG::Vector3f &source);
    void startDamage();
    void calcDamage();
    void startResurfacing();
    void calcResurfacing();
    void emit(unsigned index, const EGG::Vector3f &position);

    bool m_visible = true, m_landed = false;
    bool m_hasStaticWater = false;
    f32 m_staticWaterHeight = 0.0f;
    s16 m_itemType = -1;
    u32 m_damageFrame = 0, m_hideFrame = 300;
    f32 m_surfaceOffset = 0.0f;
    EGG::Vector3f m_hitSource = EGG::Vector3f::zero;
    EGG::Vector3f m_hitVelocity = EGG::Vector3f::zero;
    EGG::Vector3f m_hitPivot = EGG::Vector3f::zero;
    EGG::Matrix34f m_hitBasis = EGG::Matrix34f::ident;
    std::array<u32, 3> m_effectSerial{};
    std::array<EGG::Vector3f, 3> m_effectPos{};

    u32 m_animationClip = 2;
    f32 m_animationFrame = 0.0f;
    f32 m_animationRate = 1.0f;
    mkw::ObjectAnimationSound m_animationSound;

    const f32 m_vel;
    u32 m_stillDuration;
    u32 m_stillFrame;
    bool m_still;
    const bool m_backwards;
    EGG::Vector3f m_curRot;
    State m_state;
    StatePhase m_statePhase;

    // In the base game, there is a m_timer member variable at offset 0xcc which ends up resulting
    // in the calc function being run only once during the intro timer.
    bool m_introCalc;

    static constexpr f32 YAW = F_PI / 2.0f;
    STATIC_ASSERT(YAW == 1.5707964f);
    static constexpr EGG::Vector3f INIT_ROT = EGG::Vector3f(0.0f, YAW, 0.0f);

    static constexpr u32 COUNT = 3;
};

} // namespace Kinoko::Field
