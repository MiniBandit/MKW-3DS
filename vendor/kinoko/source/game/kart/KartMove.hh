#pragma once

#include "game/kart/KartBoost.hh"
#include "game/kart/BattleRecovery.hh"
#include "game/kart/KartAction.hh"
#include "game/kart/KartBurnout.hh"
#include "game/kart/KartHalfPipe.hh"
#include "game/kart/KartObjectProxy.hh"
#include "game/kart/KartParam.hh"
#include "game/kart/KartReject.hh"
#include "game/kart/KartState.hh"

#include "game/field/CourseColMgr.hh"

#include <egg/core/BitFlag.hh>

namespace Kinoko::Kart {

/// @brief Responsible for reacting to player inputs and moving the kart.
/// @nosubgrouping
class KartMove : protected KartObjectProxy {
public:
    enum class ePadType {
        BoostPanel = 0,
        BoostRamp = 1,
        JumpPad = 2,
    };

    enum class DriftState {
        NotDrifting = 0,
        ChargingMt = 1,
        ChargedMt = 2,
        ChargingSmt = 2,
        ChargedSmt = 3,
    };

    typedef EGG::TBitFlag<u32, ePadType> PadType;

    KartMove();
    virtual ~KartMove();

    virtual void createSubsystems(const KartParam::Stats &stats);
    virtual void calcTurn();
    virtual void calcWheelie() {}
    virtual void setTurnParams();
    virtual void init(bool b1, bool b2);
    virtual void clear();

    /// @addr{0x8058974C}
    [[nodiscard]] virtual f32 leanRot() const {
        return 0.0f;
    }

    void setInitialPhysicsValues(const EGG::Vector3f &position, const EGG::Vector3f &angles);
    void resetDriftManual();

    void calc();
    void beginBattleProtection();
    void loseAllBalloons();
    void calcBattleRecovery();
    using BattleRefillHook = void (*)(void *, size_t);
    static void SetBattleRefillHook(BattleRefillHook hook, void *context);
    bool battleVisible() const { return m_battleRecovery.visible(); }
    u32 battleWipeSerial() const { return m_battleWipeSerial; }
    void calcRespawnStart();
    void calcInRespawn();
    void calcRespawnBoost();
    void calcTop();
    void calcAirtimeTop();
    void calcSpecialFloor();
    void calcDirs();
    void calcStickyRoad();
    void calcOffroad();
    void calcRisingWater();
    void calcBoost();
    void calcRampBoost();
    void calcDisableBackwardsAccel();
    void calcSsmt();
    bool calcPreDrift();
    void calcAutoDrift();
    void calcManualDrift();
    void startManualDrift();
    void clearDrift();
    void clearJumpPad();
    void clearRampBoost();
    void clearZipperBoost();
    void clearBoost();
    void clearSsmt();
    void clearOffroadInvincibility();
    void clearRejectRoad();
    void releaseMt();
    void controlOutsideDriftAngle();
    void calcRotation();
    void calcVehicleSpeed();
    void calcDeceleration();
    [[nodiscard]] f32 calcVehicleAcceleration() const;
    void calcAcceleration();
    [[nodiscard]] f32 calcWallCollisionSpeedFactor(f32 &f1);
    void calcWallCollisionStart(f32 param_2);
    void calcStandstillBoostRot();
    void calcDive();
    void calcSsmtStart();
    void calcHopPhysics();
    void calcRejectRoad();
    bool calcZipperCollision(f32 radius, f32 scale, EGG::Vector3f &pos, EGG::Vector3f &upLocal,
            const EGG::Vector3f &prevPos, Field::CollisionInfo *colInfo,
            Field::KCLTypeMask *maskOut, Field::KCLTypeMask flags) const;
    f32 calcSlerpRate(f32 scale, const EGG::Quatf &from, const EGG::Quatf &to) const;
    void applyForce(f32 force, const EGG::Vector3f &hitDir, bool stop);
    virtual void calcVehicleRotation(f32 turn);
    virtual void hop();
    virtual void onHop() {}
    virtual void onWallCollision() {}
    virtual void calcMtCharge();
    virtual void initOob();

    /// @stage 2
    /// @brief Returns the % speed boost from wheelies. For karts, this is always 0.
    /// @addr{0x8057C3C8}
    [[nodiscard]] virtual f32 getWheelieSoftSpeedLimitBonus() const {
        return 0.0f;
    }

    /// @addr{0x8058758C}
    virtual bool canWheelie() const {
        return false;
    }

    /// @addr{0x8057DA18}
    virtual bool canHop() const {
        const auto &status = KartObjectProxy::status();

        if (status.offAnyBit(eStatus::HopStart, eStatus::TouchingGround)) {
            return false;
        }

        if (status.onBit(eStatus::InAction)) {
            return false;
        }

        return true;
    }

    /// @addr{0x8057EA94}
    /// @details This doesn't bytematch the base game. The base game's implementation uses the >
    /// comparison. In practice, > is only ever used once, whereas >= is used in every other
    /// scenario, so we simplify by writing this function for the more common scenario.
    bool canStartDrift() const {
        return m_speed >= MINIMUM_DRIFT_THRESOLD * m_baseSpeed;
    }

    void tryStartBoostPanel();
    void tryStartBoostRamp();
    void tryStartJumpPad();
    void tryEndJumpPad();
    void cancelJumpPad();

    void activateBoost(KartBoost::Type type, s16 frames);
    void applyStartBoost(s16 frames);
    void activateMushroom();
    void activateZipperBoost();
    void setOffroadInvincibility(s16 timer);
    void calcOffroadInvincibility();
    void calcMushroomBoost();
    void calcZipperBoost();
    void calcSlipstream();
    void landTrick();
    void activateCrush(u16 timer);
    void calcCrushed();
    void calcScale();
    void calcMega();
    void calcPow();
    void calcStar();

    /// @name Item power-ups
    /// Retail PlayerSub10 owns the Star and Mega timers and the Lightning/Thunder Cloud
    /// shrink; the race item controller calls these and reads the timers back.
    /// @{
    bool activateStar();
    void activateMega();
    void endMega(bool immediate);
    /// The Thunder Cloud's carry status (retail bit 93, set by activateTc).
    void setThunderCloud(bool active);
    /// @}

    /// @addr{0x80580768}
    void activateShrink() {
        applyLightningEffect(300, true, false);
    }

    bool applyLightningEffect(u16 timer, bool spinShrink, bool cloudStrike);
    void calcShock();
    void deactivateShock(bool resetSpeed);

    void enterCannon();
    void calcCannon();
    void calcRotCannon(const EGG::Vector3f &forward);
    void exitCannon();

    void triggerRespawn();

    /// @beginSetters
    void setSpeed(f32 val) {
        m_speed = val;
    }

    void setSmoothedUp(const EGG::Vector3f &v) {
        m_smoothedUp = v;
    }

    void setUp(const EGG::Vector3f &v) {
        m_up = v;
    }

    void setDir(const EGG::Vector3f &v) {
        m_dir = v;
    }

    void setVel1Dir(const EGG::Vector3f &v) {
        m_vel1Dir = v;
    }

    void setFloorCollisionCount(u16 count) {
        m_floorCollisionCount = count;
    }

    void setKCLWheelSpeedFactor(f32 val) {
        m_kclWheelSpeedFactor = val;
    }

    void setKCLWheelRotFactor(f32 val) {
        m_kclWheelRotFactor = val;
    }

    /// @addr{0x8057B9AC}
    // AIStuckHandler Recover entry/exit (USA80735298/8073578c).
    // Keep wall escape acceleration while the target moves back along the path.
    void setCpuRecovery(bool active) {
        m_flags.changeBit(active, eFlags::CpuRecovery);
        status().changeBit(active, eStatus::CpuRecovery);
    }

    // Retail AISpeedRace writes KartMove+0x1c, a CPU-only speed. The original
    // PlayerSub10_updateAcceleration uses it instead of the stat base speed
    // (+0x14) as the speed-limit base while a CPU touches the ground; airborne
    // CPUs, speed ratios, drift and slipstream thresholds keep the stat speed.
    void setCpuBaseSpeed(f32 speed) {
        m_cpuSpeed = speed;
        m_cpu = true;
    }
    [[nodiscard]] f32 baseSpeed() const { return m_cpu ? m_cpuSpeed : m_baseSpeed; }
    [[nodiscard]] f32 statBaseSpeed() const { return m_baseSpeed; } ///< Retail KartMove +0x14.
    void setBulletRouteFlight(bool flight,const EGG::Vector3f &up){m_bulletRouteFlight=flight;m_bulletRouteUp=up;}
    void setBulletGroundContact(bool contact,const EGG::Vector3f& normal=EGG::Vector3f::ey);
    [[nodiscard]] bool bulletActive() const { return m_bulletActive; }

    void setKartSpeedLimit() {
        constexpr f32 LIMIT = 120.0f;
        m_hardSpeedLimit = LIMIT;
        if (m_bulletActive) {
            status().resetBit(eStatus::Bullet);
        }
        m_bulletActive = false;
        m_bulletRouteFlight = m_bulletGroundContact = false;
    }

    // RMCU retail .text 0x69104 loads 145.0 from section 5 + 0x2c18.
    void setBulletSpeedLimit() {
        m_hardSpeedLimit = 145.0f;
        m_bulletActive = true;
        status().setBit(eStatus::Bullet);
    }

    [[nodiscard]] s16 starTimer() const {
        return m_starTimer;
    }
    [[nodiscard]] s16 megaTimer() const {
        return m_megaTimer;
    }
    [[nodiscard]] s16 powDodgeTimer() const {
        return m_powDodgeTimer;
    }
    [[nodiscard]] u16 shockTimer() const {
        return m_shockTimer;
    }

    /// @addr{0x80581720}
    void setScale(const EGG::Vector3f &v) {
        m_scale = v;
    }

    void setPadType(PadType type) {
        m_padType = type;
    }
    /// @endSetters

    bool boostActive(KartBoost::Type type) const { return m_boost.active(type); }
    s16 boostRemaining(KartBoost::Type type) const { return m_boost.remaining(type); }
    u16 rampBoostRemaining() const { return m_rampBoost; }
    bool takeFloorInkClear() { const bool pending=m_floorInkClear; m_floorInkClear=false; return pending; }
    /// @brief Read only, for presentation (audio).
    bool ssmtCharged() const { return m_flags.onBit(eFlags::SsmtCharged); }
    // Presentation-only equivalent of the StartUpEffects duration callback.
    // Kart::Link GP wall counter, USA 8058b028. Half-pipe airborne and
    // the first three zipper-boost ticks are exempt from the wall penalty.
    bool gpWallPenaltyAllowed() const {
        return status().offBit(eStatus::OverZipper) &&
                (status().offBit(eStatus::ZipperBoost) || m_zipperBoostTimer >= 3);
    }
    u32 miniTurboSerial() const { return m_miniTurboSerial; }
    u32 offroadFrameSerial() const { return m_offroadFrameSerial; }
    u32 objectDamageSerial() const { return m_objectDamageSerial; }
    void recordObjectDamage() { ++m_objectDamageSerial; }
    u32 startEffectSerial() const { return m_startEffectSerial; }
    s16 startEffectDuration() const { return m_startEffectDuration; }

    /// @brief Port: the points where retail kart code calls into Audio::KartActor.
    /// Recorded for the race audio only; nothing in the simulation reads them.
    enum class SoundCue : u32 {
        Boost = 1u << 0,           ///< activateBoost: KartActor::PlayBoostSound (USA 0x80702208).
        BoostRamp = 1u << 1,       ///< tryStartBoostRamp: PlayRampSound (USA 0x807023a4).
        JumpPad = 1u << 2,         ///< tryStartJumpPad: USA 0x80702540 (false).
        JumpPadMushroom = 1u << 3, ///< tryStartJumpPad variant 4: USA 0x80702540 (true).
        Hop = 1u << 4,             ///< hop: SE_VCL_MINI_JUMP (USA 0x805773a8).
        Cannon = 1u << 5,          ///< enterCannon: PlayCannonSound (USA 0x8057e474).
        Landing = 1u << 6,         ///< Retail status bit 95: a hard or zipper landing.
        Horn = 1u << 7,            ///< checkKartCollision bump: PlayHornSound on this racer.
        Bumped = 1u << 8,          ///< checkKartCollision contact on this racer (Effects).
        BushWall = 1u << 9,        ///< calcWallCollisionStart: PlayBushCollisionSound.
        // Kart::Link::PlayCharacterSound (character voices):
        BoostVoice = 1u << 10, ///< A boost activated (6), or an MT released (USA 0x8057c964).
        Trick = 1u << 11,      ///< KartJump::start: Kart::Trick::Start (0xb).
        Respawn = 1u << 12,    ///< triggerRespawn: Kart::Movement::Respawn (0x1b).
        StartFail = 1u << 13,  ///< KartBurnout::start (USA 0x80582a48: 7, SE_VCL_SPIN/START_FAIL).
        Wheelie = 1u << 14,    ///< KartMoveBike::startWheelie (USA 0x80860f60).
        PowerHit = 1u << 15,   ///< This racer's Star/Mega/Bullet contact landed (5).
        StarHit = 1u << 16,    ///< Another racer's Star/Mega/Bullet contact landed here (0x110).
        Bump = 1u << 17,       ///< A lateral kart bump hit this racer (USA 0x80861104).
        Fall = 1u << 18,       ///< activateOob: the fall voice (USA 0x8056c634).
        // Kart-side item effects (Audio::KartActor::StartSoundLimited):
        MegaUse = 1u << 19,  ///< activateMega: SE_ITM_BIG_KINOKO_USE (USA 0x8057a344).
        MegaEnd = 1u << 20,  ///< The Mega ends: SE_ITM_BIG_KINOKO_RET (0x8057a4e4, 0x8057a5e4).
        Grow = 1u << 21,     ///< A Lightning shrink ends: SE_ITM_THNDR_BIG (0x8057a198, 0x8057a3b0).
        Press = 1u << 22,    ///< activateCrush: SE_VCL_DMG_PRESS (0x8057a704).
        PressEnd = 1u << 23, ///< A crush ends: SE_VCL_DMG_PRESS_REV (0x8057a7a4, 0x80579b1c).
        Spin = 1u << 24,     ///< KartAction rotation start: SE_VCL_SPIN (Damage 0x80563ba8).
        BlowSide = 1u << 25, ///< Large flip by action 8: SE_CC_POIHANA_KART_BLOW (0x80564e20).
        SoundTrigger = 1u << 26, ///< KartActor::ApplyKCLSoundTrigger: the course sound zone.
    };
    void cueSound(SoundCue cue) {
        m_soundCues.cues |= static_cast<u32>(cue);
    }
    void cueBumpedBy(u8 playerIdx, const EGG::Vector3f &contact) {
        cueSound(SoundCue::Bumped);
        m_soundCues.bumpedBy |= static_cast<u16>(1u << playerIdx);
        m_soundCues.contact = contact;
    }
    void cueBump(f32 magnitude) {
        cueSound(SoundCue::Bump);
        m_soundCues.bump = magnitude;
    }
    void cuePowerObjectContact(bool wall) {
        m_soundCues.powerObjectContact |= wall ? 2 : 1;
    }
    void cueObjectContact(const EGG::Vector3f &contact) {
        m_soundCues.objectHit = true;
        m_soundCues.objectContact = contact;
    }
    // Poihana's first throw contact calls KartEffect before starting damage.
    // Share the existing per-step observer without changing the reaction.
    void cuePoihanaThrow() { m_soundCues.poihanaThrow = true; }
    void cueFall(u8 voice) {
        cueSound(SoundCue::Fall);
        m_soundCues.fallVoice = voice;
    }
    void cueSoundTrigger(u8 zone) {
        cueSound(SoundCue::SoundTrigger);
        m_soundCues.soundZone = zone;
    }
    struct SoundCues {
        u32 cues = 0;
        u16 bumpedBy = 0; ///< Racers whose contact cued Bumped.
        EGG::Vector3f contact = EGG::Vector3f::zero;
        u8 powerObjectContact = 0; ///< Original normal/wall object audio callbacks.
        bool objectHit = false; ///< Presentation observer; no physics/audio cue changes.
        EGG::Vector3f objectContact = EGG::Vector3f::zero;
        f32 bump = 0.0f;  ///< The last bump's signed magnitude.
        u8 fallVoice = 0; ///< The fall voice type (0x16-0x19).
        u8 soundZone = 0; ///< The last sound trigger zone (KCL type 0x18 variant).
        bool poihanaThrow = false; ///< USA80739588 -> KartEffect806a325c.
    };
    /// @brief The cues since the last call.
    SoundCues takeSoundCues() {
        const SoundCues cues = m_soundCues;
        m_soundCues = SoundCues{};
        return cues;
    }
    const SoundCues &peekSoundCues() const { return m_soundCues; }

    /// @beginGetters
    [[nodiscard]] DriftState driftState() const {
        return m_driftState;
    }

    [[nodiscard]] u16 mtCharge() const {
        return m_mtCharge;
    }

    [[nodiscard]] f32 kclSpeedFactor() const {
        return m_kclSpeedFactor;
    }

    [[nodiscard]] f32 kclRotFactor() const {
        return m_kclRotFactor;
    }

    /// @brief Factors in vehicle speed to retrieve our hop direction and magnitude.
    /// @addr{0x8057EFF8}
    /// @return 0.0f if we are too slow to drift, otherwise the hop direction.
    [[nodiscard]] s32 getAppliedHopStickX() const {
        return canStartDrift() ? m_hopStickX : 0;
    }

    [[nodiscard]] f32 softSpeedLimit() const {
        return m_softSpeedLimit;
    }

    [[nodiscard]] f32 speed() const {
        return m_speed;
    }

    [[nodiscard]] f32 acceleration() const {
        return m_acceleration;
    }

    [[nodiscard]] const EGG::Vector3f &scale() const {
        return m_scale;
    }

    [[nodiscard]] f32 hardSpeedLimit() const {
        return m_hardSpeedLimit;
    }

    [[nodiscard]] const EGG::Vector3f &smoothedUp() const {
        return m_smoothedUp;
    }

    [[nodiscard]] const EGG::Vector3f &up() const {
        return m_up;
    }

    [[nodiscard]] f32 totalScale() const {
        return m_totalScale;
    }

    [[nodiscard]] f32 hitboxScale() const {
        return m_hitboxScale;
    }

    [[nodiscard]] const EGG::Vector3f &dir() const {
        return m_dir;
    }

    [[nodiscard]] const EGG::Vector3f &lastDir() const {
        return m_lastDir;
    }

    [[nodiscard]] f32 outsideDriftAngle() const {
        return m_outsideDriftAngle;
    }

    [[nodiscard]] const EGG::Vector3f &vel1Dir() const {
        return m_vel1Dir;
    }

    [[nodiscard]] const EGG::Vector3f &smoothedForward() const {
        return m_smoothedForward;
    }

    /// Retail slipstream state (KartMove +0xec/+0xf0, state bits 64 charging / 65 boost).
    [[nodiscard]] bool slipstreamCharging() const {
        return m_slipstreamCharging;
    }
    [[nodiscard]] bool slipstreamBoost() const {
        return m_slipstreamBoost;
    }
    [[nodiscard]] f32 slipstreamCharge() const {
        return m_slipstreamCharge;
    }

    [[nodiscard]] f32 speedRatioCapped() const {
        return m_speedRatioCapped;
    }

    [[nodiscard]] f32 speedRatio() const {
        return m_speedRatio;
    }

    [[nodiscard]] u16 floorCollisionCount() const {
        return m_floorCollisionCount;
    }

    [[nodiscard]] s32 hopStickX() const {
        return m_hopStickX;
    }

    [[nodiscard]] f32 hopPosY() const {
        return m_hopPosY;
    }

    [[nodiscard]] s16 respawnTimer() const {
        return m_respawnTimer;
    }

    [[nodiscard]] s16 respawnPostLandTimer() const {
        return m_respawnPostLandTimer;
    }

    [[nodiscard]] PadType &padType() {
        return m_padType;
    }

    [[nodiscard]] KartJump *jump() const {
        return m_jump;
    }

    [[nodiscard]] KartHalfPipe *halfPipe() const {
        return m_halfPipe;
    }

    [[nodiscard]] KartScale *kartScale() const {
        return m_kartScale;
    }

    [[nodiscard]] KartBurnout &burnout() {
        return m_burnout;
    }
    /// @endGetters

protected:
    enum class eFlags {
        Respawned = 0,   ///< Set when Lakitu lets go of the player, cleared when landing.
        DriftReset = 1,  ///< Set when a wall bonk should cancel your drift.
        SsmtCharged = 2, ///< Set after holding a stand-still mini-turbo for 75 frames.
        CpuRecovery = 3, ///< Retail +0x254 bit3: bypass wall speed loss during AI recovery.
        LaunchBoost = 4,
        SsmtLeeway = 5,       ///< If set, activates SSMT when not pressing A or B.
        TrickableSurface = 6, ///< Set when driving on a trickable surface.
        WallBounce = 8,       ///< Set when our speed loss from wall collision is > 30.0f.
    };
    typedef EGG::TBitFlag<u16, eFlags> Flags;

    /// @brief The direction the player is currently driving in.
    enum class DrivingDirection {
        Forwards = 0,
        Braking = 1,
        WaitingForBackwards = 2, ///< Holding reverse but waiting on a 15 frame delay.
        Backwards = 3,
    };

    struct JumpPadProperties {
        f32 minSpeed;
        f32 maxSpeed;
        f32 velY;
    };

    /// @brief Houses parameters that vary between the drift type (inward bike, outward bike, kart).
    struct DriftingParameters {
        f32 hopVelY;
        f32 stabilizationFactor;
        f32 _8;
        f32 boostRotFactor;
    };

    f32 m_engineClassRate = 1.0f; ///< Retail Movement +0x10; selected before kart creation.
    f32 m_baseSpeed;      ///< The speed associated with the current character/vehicle stats.
    f32 m_softSpeedLimit; ///< Base speed + boosts + wheelies, restricted to the hard speed limit.
    f32 m_cpuSpeed = 0.0f; ///< Retail +0x1c: AISpeedRace result (CPU ground speed-limit base).
    bool m_cpu = false;    ///< Set once the CPU speed has been written (KartObjectProxy::isCpu).
    f32 m_speed;          ///< Current speed, restricted to the soft speed limit.
    f32 m_lastSpeed;      ///< Last frame's speed, cached to calculate angular velocity.
    f32 m_processedSpeed; ///< Offset 0x28. It's only ever just a copy of @ref m_speed.
    f32 m_megaScale = 1.0f; ///< Retail KartMove + 0x17c collision scale.
    bool m_megaEnding = false; ///< Retail +0x196: the scale-down animation has started.
    BattleRecovery m_battleRecovery;
    u32 m_battleWipeSerial=0;
    static BattleRefillHook s_battleRefillHook;
    static void *s_battleRefillContext;
    s16 m_starTimer = 0;       ///< Retail +0x18a.
    s16 m_megaTimer = 0;       ///< Retail +0x194.
    s16 m_powDodgeTimer = 0;   ///< Retail +0x152: frames a trick still dodges a POW hit.
    bool m_bulletRouteFlight = false;
    EGG::Vector3f m_bulletRouteUp = EGG::Vector3f::ey;
    bool m_bulletGroundContact = false;
    bool m_bulletActive = false; ///< Native integration of retail Bullet state bit 91.
    f32 m_hardSpeedLimit; ///< Absolute speed cap: kart 120, retail Bullet Bill 145.
    f32 m_acceleration;   ///< Captures the acceleration from player input and boosts.
    f32 m_speedDragMultiplier;  ///< After 5 frames of airtime, this causes speed to slowly decay.
    EGG::Vector3f m_smoothedUp; ///< A smoothed up vector, mostly used after significant airtime.
    EGG::Vector3f m_up;         ///< Vector perpendicular to the floor, pointing upwards.
    EGG::Vector3f m_landingDir;
    EGG::Vector3f m_dir;
    EGG::Vector3f m_lastDir; ///< @ref m_speed from the previous frame but with signed magnitude.
    EGG::Vector3f m_vel1Dir;
    EGG::Vector3f m_smoothedForward;
    EGG::Vector3f m_dirDiff;
    bool m_hasLandingDir;
    f32 m_outsideDriftAngle; ///< The facing angle of an outward-drifting vehicle.
    f32 m_landingAngle;
    EGG::Vector3f m_outsideDriftLastDir; ///< Used to compute the next @ref m_outsideDriftAngle.
    f32 m_speedRatioCapped;              ///< @ref m_speedRatio but capped at 1.0f.
    f32 m_speedRatio;     ///< The ratio between current speed and the player's base speed stat.
    f32 m_kclSpeedFactor; ///< Float between 0-1 that scales the player's speed on offroad.
    f32 m_kclRotFactor;   ///< Float between 0-1 that scales the player's turning radius on offroad.
    f32 m_kclWheelSpeedFactor; ///< The slowest speed multiplier of each wheel's floor collision.
    f32 m_kclWheelRotFactor;   ///< The slowest rotation multiplier of each wheel's floor collision.
    u16 m_floorCollisionCount; ///< The number of tires colliding with the floor.
    s32 m_hopStickX;        ///< A ternary for the direction of our hop, 0 if still neutral hopping.
    s32 m_hopFrame;         ///< A timer that can prevent subsequent hops until reset.
    EGG::Vector3f m_hopUp;  ///< The up vector when hopping.
    EGG::Vector3f m_hopDir; ///< Used for outward drift. Tracks the forward vector of our rotation.
    f32 m_divingRot;        ///< Induces x-axis angular velocity based on up/down stick input.
    f32 m_standStillBoostRot;
    DriftState m_driftState;
    u16 m_mtCharge;          ///< A value between 0 and 270 representing current MT charge.
    u16 m_smtCharge;         ///< A value between 0 and 300 representing current SMT charge.
    f32 m_outsideDriftBonus; ///< Added to angular velocity when outside drifting.
    KartBoost m_boost;
    bool m_floorInkClear=false;
    s16 m_zipperBoostTimer;
    s16 m_zipperBoostMax;
    KartReject m_reject;
    s16 m_offroadInvincibility;  ///< How many frames until the player is affected by offroad.
    s16 m_ssmtCharge;            ///< Increments every frame up to 75 when charging stand-still MT.
    s16 m_ssmtLeewayTimer;       ///< Frames to forgive letting go of A before clearing SSMT charge.
    s16 m_ssmtDisableAccelTimer; ///< Counter that tracks delay before starting to reverse.
    f32 m_realTurn; ///< The "true" turn magnitude. Equal to @ref m_weightedTurn unless drifting.
    f32 m_weightedTurn;    ///< Magnitude+direction of stick input, factoring in the kart's stats.
    EGG::Vector3f m_scale; ///< Normally the unit vector, but may vary due to crush animations.
    f32 m_totalScale;      ///< @unused Always 1.0f
    f32 m_hitboxScale;
    f32 m_shockSpeedMultiplier;
    u16 m_mushroomBoostTimer; ///< Number of frames until the mushroom boost runs out.
    f32 m_invScale;
    u16 m_shockTimer;
    u16 m_crushTimer; ///< Number of frames until player will be uncrushed.
    u32 m_nonZipperAirtime;
    f32 m_jumpPadMinSpeed; ///< Snaps the player to a minimum speed when first touching a jump pad.
    f32 m_jumpPadMaxSpeed;
    f32 m_jumpPadBoostMultiplier;
    f32 m_jumpPadSoftSpeedLimit;
    const JumpPadProperties *m_jumpPadProperties;
    u16 m_rampBoost;
    f32 m_autoDriftAngle;
    s16 m_autoDriftStartFrameCounter;
    f32 m_cannonEntryOfsLength;
    EGG::Vector3f m_cannonEntryPos;
    EGG::Vector3f m_cannonEntryOfs;
    EGG::Vector3f m_cannonOrthog;
    EGG::Vector3f m_cannonProgress;
    f32 m_hopVelY;    ///< Relative velocity due to a hop. Starts at 10 and decreases with gravity.
    f32 m_hopPosY;    ///< Relative position as the result of a hop. Starts at 0.
    f32 m_hopGravity; ///< Always main gravity (-1.3f).
    s16 m_timeInRespawn;        ///< The number of frames elapsed after position snap from respawn.
    s16 m_respawnPreLandTimer;  ///< Counts down from 4 when pressing A before landing from respawn.
    s16 m_respawnPostLandTimer; ///< Counts up to 4 if not accelerating after respawn landing.
    s16 m_respawnTimer;
    s16 m_bumpTimer;                     ///< Set when a @ref Reaction::SmallBump collision occurs.
    DrivingDirection m_drivingDirection; ///< Current state of driver's direction.
    s16 m_backwardsAllowCounter;         ///< Tracks the 15f delay before reversing.
    PadType m_padType;
    Flags m_flags;
    SoundCues m_soundCues; ///< Port: sound cues (audio only).
    // Lifetime event counter: generic init/respawn must not erase race statistics.
    u32 m_miniTurboSerial = 0;
    u32 m_offroadFrameSerial = 0;
    u32 m_objectDamageSerial = 0;
    u32 m_startEffectSerial = 0;
    s16 m_startEffectDuration = 0;
    KartJump *m_jump;
    KartHalfPipe *m_halfPipe;                   ///< Pertains to zipper physics.
    KartScale *m_kartScale;                     ///< Manages scaling due to TF stompers and MH cars.
    KartBurnout m_burnout;                      ///< Manages the state of start boost burnout.
    const DriftingParameters *m_driftingParams; ///< Drift-type-specific parameters.
    f32 m_rawTurn; ///< Float in range [-1, 1]. Represents stick magnitude + direction.
    s16 m_slipstreamTimer = 0;     ///< Retail +0xec: drafting / boost frames.
    f32 m_slipstreamCharge = 1.0f; ///< Retail +0xf0: 1.0, up to 1.1 drafting, 1.4 boosting.
                                   ///< Retail reads it nowhere else (no physical effect).
    bool m_slipstreamCharging = false;
    bool m_slipstreamBoost = false;

    static constexpr f32 MINIMUM_DRIFT_THRESOLD = 0.55f;
};

/// @brief Responsible for reacting to player inputs and moving the bike.
/// @details This derived class has specialized behavior for bikes, such as wheelies and leaning.
/// There are also additional member variables to track the bike's unique state.
/// @nosubgrouping
class KartMoveBike : public KartMove {
public:
    /// @brief Represents turning information which differs only between inside/outside drift.
    struct TurningParameters {
        f32 leanRotShallowFactor;
        f32 leanRotIncRace;
        f32 leanRotCapRace;
        f32 driftStickXFactor;
        f32 leanRotMaxDrift;
        f32 leanRotMinDrift;
        f32 leanRotIncCountdown;
        f32 leanRotCapCountdown;
        f32 leanRotIncSSMT;
        f32 leanRotCapSSMT;
        f32 leanRotDecayFactor;
        u16 maxWheelieFrames;
    };

    KartMoveBike();
    ~KartMoveBike();

    virtual void startWheelie();
    virtual void cancelWheelie();

    void createSubsystems(const KartParam::Stats &stats) override;
    void calcVehicleRotation(f32 /*turn*/) override;
    void calcWheelie() override;
    void onHop() override;
    void onWallCollision() override;
    void calcMtCharge() override;
    void initOob() override;
    void setTurnParams() override;
    void init(bool b1, bool b2) override;
    void clear() override;

    /// @stage 2
    /// @brief Returns what % to raise the speed cap when wheeling.
    /// @addr{0x80588324}
    [[nodiscard]] f32 getWheelieSoftSpeedLimitBonus() const override {
        constexpr f32 WHEELIE_SPEED_BONUS = 0.15f;
        return status().onBit(eStatus::Wheelie) ? WHEELIE_SPEED_BONUS : 0.0f;
    }

    /// @addr{0x80588860}
    [[nodiscard]] f32 wheelieRotFactor() const {
        constexpr f32 WHEELIE_ROTATION_FACTOR = 0.2f;

        return status().onBit(eStatus::Wheelie) ? WHEELIE_ROTATION_FACTOR : 1.0f;
    }

    void tryStartWheelie();

    /// @beginGetters
    /// @addr{0x805896BC}
    [[nodiscard]] f32 leanRot() const override {
        return m_leanRot;
    }

    /// @brief Checks if the kart is going fast enough to wheelie.
    /// @addr{0x80588FE0}
    [[nodiscard]] bool canWheelie() const override {
        constexpr f32 WHEELIE_THRESHOLD = 0.3f;

        return m_speedRatioCapped >= WHEELIE_THRESHOLD && m_speed >= 0.0f;
    }

    /// @endGetters

private:
    f32 m_leanRot;         ///< Z-axis rotation of the bike from leaning.
    f32 m_leanRotCap;      ///< The maximum leaning rotation.
    f32 m_leanRotInc;      ///< The incrementor for leaning rotation.
    f32 m_wheelieRot;      ///< X-axis rotation from wheeling.
    f32 m_maxWheelieRot;   ///< The maximum wheelie rotation.
    u32 m_wheelieFrames;   ///< Tracks wheelie duration and cancels the wheelie after 180 frames.
    s16 m_wheelieCooldown; ///< The number of frames before another wheelie can start.
    f32 m_wheelieRotDec;   ///< The wheelie rotation decrementor, used after a wheelie has ended.
    s16 m_autoHardStickXFrames;
    const TurningParameters *m_turningParams; ///< Inside/outside drifting bike turn info.
};

} // namespace Kinoko::Kart
