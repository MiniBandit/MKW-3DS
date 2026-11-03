#pragma once

#include "game/field/StateManager.hh"
#include "game/field/obj/ObjectCollidable.hh"
#include <mkw/object_animation_sound.hpp>

namespace Kinoko::Field {

class ObjectPoihanaBase : public ObjectCollidable, virtual public StateManager {
public:
    ObjectPoihanaBase(const System::MapdataGeoObj &params);
    ~ObjectPoihanaBase() override;

    void init() override;

    /// @addr{0x807472F4}
    void calc() override {
        StateManager::calc();
    }

    /// @addr{0x8074815C}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

protected:
    enum class WalkState {
        NeedTarget = 0,
        HasTarget = 1,
    };

    WalkState m_walkState;
    EGG::Vector3f m_curRot;
    EGG::Vector3f m_up;
    EGG::Matrix34f m_workMat;
    EGG::Vector3f m_accel;
    EGG::Vector3f m_extVel;
    EGG::Vector3f m_vel;
    EGG::Vector3f m_floorNrm;
    f32 m_heightOffset; ///< Accounts for cataquack leg height
    f32 m_radius;
    EGG::Vector3f m_targetPos;
    EGG::Vector3f m_dir;
    EGG::Vector3f m_initPos;
    EGG::Vector3f m_targetVel; ///< Derived from m_targetPos and current position
};

/// @brief Represents the cataquacks on GCN Peach Beach.
class ObjectPoihana final : public ObjectPoihanaBase {
public:
    ObjectPoihana(const System::MapdataGeoObj &params);
    ~ObjectPoihana() override;

    void init() override;

    /// @addr{0x80748B70}
    void calc() override;
    Kart::Reaction onCollision(Kart::KartObject *kart, Kart::Reaction reactionOnKart,
            Kart::Reaction reactionOnObj, EGG::Vector3f &hitDepth) override;
    s16 onItemCollision(const EGG::Vector3f &position, const EGG::Vector3f &velocity,
            s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &hitDepth) override;
    void onGlobalItemCollision(s16 reactionOnItem, s16 reactionOnObj) override;
    [[nodiscard]] const char *animationName() const { return m_animationName; }
    [[nodiscard]] f32 animationFrame() const { return m_animationFrame; }

private:
    mkw::ObjectAnimationSound m_animationSound;
    const char *m_animationName = "walk";
    f32 m_animationFrame = 0.0f, m_animationRate = 1.0f;
    EGG::Vector3f m_hitDirection, m_hitPosition, m_throwPosition;
    EGG::Vector3f m_toppleAxis = EGG::Vector3f::ez;
    EGG::Vector3f m_visualForward = EGG::Vector3f::ez;
    EGG::Vector3f m_visualUp = EGG::Vector3f::ey;
    f32 m_homeRadius = 4000.0f, m_airVelocity = 0.0f, m_airOffset = 0.0f;
    u32 m_actionFrame = 0, m_auxFrame = 0, m_toppleCount = 0;
    s32 m_target = -1, m_lastTarget = -1;
    u32 m_seekCursor = 0, m_actionPhase = 0;
    bool m_throwPending = false, m_hitPending = false, m_toppling = false;
    bool m_grounded = false, m_fixedMovement = false;

    void playAnimation(const char *name, f32 rate = 1.0f);
    void publishTransform();
    bool targetAvailable(unsigned player) const;
    s32 findTarget();
    EGG::Vector3f predictedTarget() const;
    void enterState0();
    void enterState1() {}
    void calcState0And1();
    void enterChase();
    void calcChase();
    void enterCatapult();
    void calcCatapult();
    void enterCatapultNoMove();
    void calcCatapultNoMove();
    void topple();
    void enterTopple();
    void enterToppleAgain();
    void calcTopple();
    void enterToppleNoBury();
    void calcToppleNoBury();
    void rotateTopple(f32 angle, EGG::Vector3f &up, EGG::Vector3f &forward);

    /// @addr{0x80747308}
    [[nodiscard]] EGG::Vector3f curPos() const {
        return m_workMat.base(3);
    }

    void calcCurRot(f32 t);
    void calcOrthonormalBasis();
    void calcCollision();
    void calcStep();
    void calcDir();
    void calcUp();

    /// @addr{0x80747324}
    [[nodiscard]] EGG::Vector3f collisionPos() const {
        return curPos() + m_floorNrm * m_heightOffset;
    }

    /// @addr{0x807476D8}
    void calcPosAndCollision() {
        m_extVel += m_accel;
        m_workMat.setBase(3, m_workMat.base(3) + (m_extVel + m_vel));
        calcCollision();
    }

    /// @addr{0x8074B0AC}
    [[nodiscard]] static EGG::Vector3f Project(const EGG::Vector3f &v0, const EGG::Vector3f &v1) {
        f32 scale = (v0.z * v1.z + (v0.x * v1.x + v0.y * v1.y)) /
                EGG::Mathf::fma(v1.z, v1.z, v1.x * v1.x + v1.y * v1.y);

        return v1 * scale;
    }

    static constexpr std::array<StateManagerEntry, 8> STATE_ENTRIES = {{
        StateEntry<ObjectPoihana, &ObjectPoihana::enterState0, &ObjectPoihana::calcState0And1>(0),
        StateEntry<ObjectPoihana, &ObjectPoihana::enterState1, &ObjectPoihana::calcState0And1>(1),
        StateEntry<ObjectPoihana, &ObjectPoihana::enterChase, &ObjectPoihana::calcChase>(2),
        StateEntry<ObjectPoihana, &ObjectPoihana::enterCatapult, &ObjectPoihana::calcCatapult>(3),
        StateEntry<ObjectPoihana, &ObjectPoihana::enterCatapultNoMove, &ObjectPoihana::calcCatapultNoMove>(4),
        StateEntry<ObjectPoihana, &ObjectPoihana::enterTopple, &ObjectPoihana::calcTopple>(5),
        StateEntry<ObjectPoihana, &ObjectPoihana::enterToppleAgain, &ObjectPoihana::calcTopple>(6),
        StateEntry<ObjectPoihana, &ObjectPoihana::enterToppleNoBury, &ObjectPoihana::calcToppleNoBury>(7),
    }};
};

} // namespace Kinoko::Field
