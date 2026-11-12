#pragma once

#include "game/kart/KartObjectProxy.hh"

namespace Kinoko::Kart {

enum class Action {
    None = -1,
    UNK_0 = 0,
    UNK_1 = 1,
    UNK_2 = 2,
    UNK_3 = 3,
    UNK_4 = 4,
    UNK_5 = 5,
    UNK_6 = 6,
    UNK_7 = 7,
    UNK_8 = 8,
    UNK_9 = 9,
    UNK_10 = 10,
    UNK_11 = 11,
    UNK_12 = 12,
    UNK_13 = 13, ///< Mega Mushroom crush.
    UNK_14 = 14,
    UNK_15 = 15,
    UNK_16 = 16,
    UNK_17 = 17, ///< Thunder Cloud strike.
    Max = 18,
};

class KartAction : KartObjectProxy {
public:
    enum class eFlags {
        Landing = 0,
        PowDodged = 1, ///< Retail +0xcc bit 1: the POW spin was cut to one rotation.
        LargeFlip = 2,
        Rotating = 3,
        LandingFromFlip = 5,
    };
    typedef EGG::TBitFlag<u32, eFlags> Flags;

    KartAction();
    ~KartAction();

    void init();
    void calc();
    void calcVehicleSpeed();
    /// @param force Retail r6: Star and Bullet contact pass it so the action reaches a Mega kart.
    /// @param attacker Retail r8: the racer responsible, 0xc for none (item-loss attribution).
    bool start(Action action, bool force = false, u8 attacker = NO_ATTACKER);
    void startRotation(size_t idx);
    [[nodiscard]] Action currentAction() const { return m_currentAction; }
    [[nodiscard]] u32 frame() const { return m_frame; }
    [[nodiscard]] bool powDodged() const { return m_flags.onBit(eFlags::PowDodged); }

    /// POW: the manager marks its targets at every stomp (retail KartAction +0xf8, 0x80563174)
    /// and opens a window at the third (0x805631a4) that the kart reads in KartMove::calc.
    void setPowTarget(bool target) { m_powTarget = target; }
    [[nodiscard]] bool powPending() const { return s_powWindowActive && m_powTarget; }
    bool dodgePow();
    static void OpenPowWindow();
    static void CalcPowWindow();
    static void ResetPowWindow();
    /// Incremented by every accepted start; the race item controller reads it with
    /// lastStarted() to attribute actions started inside the kart pass.
    [[nodiscard]] u32 startCount() const { return m_startCount; }
    // Presentation event emitted by every startRotation, including later
    // rotations inside an action. Matches retail's CreateSpinEffect call.
    [[nodiscard]] u32 spinStartCount() const { return m_spinStartCount; }
    [[nodiscard]] u32 crashStartCount() const { return m_crashStartCount; }
    [[nodiscard]] u32 spinRotations() const { return m_rotationCount; }
    [[nodiscard]] bool spinLeft() const { return m_rotationDirection > 0.f; }
    [[nodiscard]] Action lastStarted() const { return m_lastStarted; }
    [[nodiscard]] u8 lastAttacker() const { return m_attacker; }

    static constexpr u8 NO_ATTACKER = 0xc;

    void setHitDepth(const EGG::Vector3f &hitDepth) {
        m_hitDepth = hitDepth;
    }

    void setTranslation(const EGG::Vector3f &v) {
        m_translation = v;
    }

    [[nodiscard]] const Flags &flags() const {
        return m_flags;
    }

private:
    /// @brief Parameters specific to an action ID.
    struct ActionParams {
        f32 startSpeedMult;
        f32 calcSpeedMult;
        s16 priority;
    };

    struct RotationParams {
        f32 initialAngleIncrement;
        f32 minAngleIncrement;
        f32 minMultiplier;
        f32 initialMultiplierDecrement;
        f32 slowdownThreshold;
        f32 finalAngle;
    };

    // The player index sent into StartActionFunc is assumed to be cosmetic
    typedef void (KartAction::*StartActionFunc)();
    typedef bool (KartAction::*CalcActionFunc)();
    typedef void (KartAction::*EndActionFunc)(bool arg);

    void calcSideFromHitDepth();
    void calcSideFromHitDepthAndTranslation();

    void end();

    bool calcCurrentAction();
    void calcEndAction(bool endArg);
    bool calcRotation();
    void calcUp();
    void calcLanding();
    void startLaunch(f32 extVelScalar, f32 extVelKart, f32 extVelBike, f32 numRotations,
            u32 param6);
    void activateCrush(u16 timer);

    void applyStartSpeed();
    void setRotation(size_t idx);

    /* ================================ *
     *     START FUNCTIONS
     * ================================ */

    void startStub();
    void startAction0();
    void startAction1();
    void startAction6();
    void startPowAction();
    void startAction2();
    void startAction3();
    void startAction4();
    void startAction5();
    void startLargeFlipAction();
    void startAction9();
    void startLongPressAction();
    void startMegaCrushAction();
    void startShortPressAction();
    void startSpinShrinkAction();

    /* ================================ *
     *     CALC FUNCTIONS
     * ================================ */

    bool calcStub();
    bool calcAction1();
    bool calcPowAction();
    bool calcLaunchAction();
    bool calcAction4();
    bool calcLargeFlipAction();
    bool calcPressAction();

    /* ================================ *
     *     END FUNCTIONS
     * ================================ */

    void endStub(bool arg);
    void endAction1(bool arg);
    void endLaunchAction(bool arg);
    void endPressAction(bool arg);

    void loseItems(bool strong, bool trailOnly = true);

    EGG::Vector3f m_side;
    Action m_currentAction;
    f32 m_rotationDirection;
    f32 m_targetRot;
    EGG::Vector3f m_hitDepth;
    EGG::Vector3f m_rotAxis;
    EGG::Vector3f m_translation;

    f32 m_velPitch;
    f32 m_pitch;
    f32 m_deltaPitch;
    f32 m_flipPhase;
    s32 m_groundStartLaunchTimer;

    StartActionFunc m_onStart;
    CalcActionFunc m_onCalc;
    EndActionFunc m_onEnd;

    EGG::Quatf m_rotation;
    const ActionParams *m_actionParams;
    u32 m_frame;
    u32 m_crushTimer;
    u32 m_rotationCount = 0; ///< Retail +0x20: rotations of the current spin.
    bool m_powTarget = false;
    Flags m_flags;
    f32 m_currentAngle;
    f32 m_angleIncrement;
    f32 m_multiplier;
    f32 m_multiplierDecrement;
    f32 m_finalAngle;
    const RotationParams *m_rotationParams;
    EGG::Vector3f m_up;
    u16 m_framesFlipping;
    s16 m_priority;
    u32 m_startCount = 0;
    u32 m_spinStartCount = 0;
    u32 m_crashStartCount = 0;
    Action m_lastStarted = Action::None;
    u8 m_attacker = NO_ATTACKER;

    static constexpr size_t MAX_ACTION = static_cast<size_t>(Action::Max);

    static const std::array<ActionParams, MAX_ACTION> s_actionParams;
    static const std::array<RotationParams, 5> s_rotationParams;
    static bool s_powWindowActive; ///< Retail 0x80510118.
    static s16 s_powWindow;        ///< Retail 0x8051011a.

    static const std::array<StartActionFunc, MAX_ACTION> s_onStart;
    static const std::array<CalcActionFunc, MAX_ACTION> s_onCalc;
    static const std::array<EndActionFunc, MAX_ACTION> s_onEnd;
};

} // namespace Kinoko::Kart
