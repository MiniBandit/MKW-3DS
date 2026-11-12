#pragma once

#include "game/field/obj/ObjectCollidable.hh"
#include <mkw/object_animation_sound.hpp>

namespace Kinoko::Field {

/// @brief The base class for penguins on N64 Sherbet Land.
/// @details In the base game, the cave penguins derive from this class, but they do not override
/// any physics-dependent virtual functions. For Kinoko, we simply construct these penguins as a
/// base class instance. The cave penguins walk along a rail, always facing perpendicular to the
/// tangent line.
class ObjectPenguin : public ObjectCollidable {
public:
    void onGlobalItemCollision(s16, s16 reactionOnObj) override {
        if (reactionOnObj == 3 || reactionOnObj == 5) spinOut();
    }
    ObjectPenguin(const System::MapdataGeoObj &params);
    ~ObjectPenguin() override;

    void init() override;

    /// @addr{0x80775764}
    void calc() override;
    Kart::Reaction onCollision(Kart::KartObject *kart, Kart::Reaction reactionOnKart,
            Kart::Reaction reactionOnObj, EGG::Vector3f &hitDepth) override;
    s16 onItemCollision(const EGG::Vector3f &position, const EGG::Vector3f &velocity,
            s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &hitDepth) override;
    [[nodiscard]] bool spinning() const { return m_state == State::Spin; }
    [[nodiscard]] s32 presentationState() const {
        return static_cast<s32>(spinning() ? m_beforeSpin : m_state);
    }

    /// @addr{0x80777324}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

    /// @addr{0x807757A0}
    virtual void calcWalk() {
        m_railInterpolator->calc();
        calcPos();
        calcRot();
    }

    virtual void calcRot();
    [[nodiscard]] f32 effectScale() const { return id() == ObjectId::PenguinL ? 2.5f : 1.0f; }
    virtual void spinOut();
    virtual void calcSpin();
    void applySpin();

    /// @addr{0x80775C2C}
    virtual void calcPos() {
        setPos(m_railInterpolator->curPos());
    }

    virtual void enterWalk();

    /// 0 walk, 1 dive, 2 slider, 3 slider (slowing), 4 stand up.
    [[nodiscard]] s32 state() const {
        return static_cast<s32>(m_state);
    }
    [[nodiscard]] f32 animationFrame() const { return m_soundFrame; }

protected:
    mkw::ObjectAnimationSound m_animationSound;
    f32 m_soundFrame = 0;
    enum class State {
        Walk = 0,
        Dive = 1,
        Slider = 2,
        SliderSlow = 3,
        StandUp = 4,
        Spin = 5,
    };

    State m_state;
    State m_beforeSpin = State::Walk;
    u32 m_spinTicks = 0;
    f32 m_spinSpeed = 60.0f, m_spinAngle = 0.0f;
    EGG::Matrix34f m_spinPose = EGG::Matrix34f::ident;
    EGG::Vector3f m_basis;
};

/// @brief Represents the penguins on N64 Sherbet Land that slide on their stomach.
class ObjectPenguinL final : public ObjectPenguin {
public:
    explicit ObjectPenguinL(const System::MapdataGeoObj &params) : ObjectPenguin(params) {}
    /// @addr{0x80777284}: large penguins retain their authored heading.
    void calcRot() override {}
};

class ObjectPenguinS final : public ObjectPenguin {
public:
    ObjectPenguinS(const System::MapdataGeoObj &params);
    ~ObjectPenguinS() override;

    void init() override;
    void calc() override;

    void loadAnims() override;

    /// @addr{0x8077637C}
    void calcWalk() override {
        calcRail();
        calcPos();
        calcRot();
    }

    /// @addr{0x80776AF0}
    void enterWalk() override {
        m_state = State::Walk;
        m_soundFrame = 0;
        m_walkRate = 1.0f;
    }

    /// Frames left in the one-shot dive or stand-up animation.
    [[nodiscard]] s32 anmTimer() const {
        return m_anmTimer;
    }

private:
    void calcSpin() override;

    /// @addr{0x807763D0}
    void calcDive() {
        calcWalk();
    }

    void calcSlider();
    void calcStandUp();
    void calcRail();

    s32 m_anmTimer;
    f32 m_walkRate = 1.0f;
};

} // namespace Kinoko::Field
