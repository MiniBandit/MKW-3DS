#pragma once

#include "game/kart/CollisionGroup.hh"
#include "game/kart/KartAction.hh"
#include "game/kart/KartObjectProxy.hh"
#include "game/kart/KartPullPath.hh"

#include "game/field/CourseColMgr.hh"

#include <egg/core/BitFlag.hh>
#include <egg/math/Matrix.hh>
#include <egg/math/Quat.hh>

namespace Kinoko::Kart {

class KartObject;

enum class Reaction {
    None = 0,
    UNK_3 = 3,
    UNK_4 = 4,
    UNK_5 = 5,
    UNK_7 = 7,
    WallAllSpeed = 8,
    SpinAllSpeed = 9,
    SpinSomeSpeed = 10,
    FireSpin = 11,
    ClipThroughSomeSpeed = 12,
    SmallLaunch = 13,
    KnockbackSomeSpeedLoseItem = 14,
    LaunchSpinLoseItem = 15,
    KnockbackBumpLoseItem = 16,
    LongCrushLoseItem = 17,
    SmallBump = 18,
    BigBump = 19,
    SpinShrink = 20,
    HighLaunchLoseItem = 21,
    SpinHitSomeSpeed = 22,
    WeakWall = 23,
    Wall = 24,
    LaunchSpin = 25,
    WallSpark = 26,
    RubberWall = 27,
    Wall2 = 28,
    UntrickableJumpPad = 29,
    ShortCrushLoseItem = 30,
    CrushRespawn = 31,
    ExplosionLoseItem = 32,
};

/// @brief Manages body+wheel collision and its influence on position/velocity/etc.
/// @nosubgrouping
class KartCollide : KartObjectProxy {
public:
    enum class eSurfaceFlags {
        Wall = 0,
        SolidOOB = 1,
        ObjectWall = 2,
        ObjectWall3 = 3,
        BoostRamp = 4,
        Offroad = 6, ///< @unused
        GroundBoostPanelOrRamp = 7,
        Trickable = 11,
        NotTrickable = 12,
        StopHalfPipeState = 16,
    };
    typedef EGG::TBitFlag<u32, eSurfaceFlags> SurfaceFlags;

    KartCollide();
    ~KartCollide();

    void init();
    void resetHitboxes();

    void calcHitboxes();

    void findCollision();
    void FUN_80572F4C();
    void FUN_805B72B8(f32 param_1, f32 param_2, bool lockXZ, bool addExtVelY);
    void calcBodyCollision(f32 totalScale, f32 sinkDepth, const EGG::Quatf &rot,
            const EGG::Vector3f &scale);
    void calcFloorEffect();
    void calcTriggers(Field::KCLTypeMask *mask, const EGG::Vector3f &pos, bool twoPoint);
    void handleTriggers(Field::KCLTypeMask *mask);
    void calcFallBoundary(Field::KCLTypeMask *mask, bool shortBoundary);
    void calcBeforeRespawn();
    void activateOob(bool detachCamera, Field::KCLTypeMask *mask, bool somethingCPU,
            bool somethingBullet);

    /// Port hook for presentation only; the simulation never reads it. Retail
    /// calls Kart::Link::SetCamera (0x8, the race camera's eye holds) or
    /// StopCameraMovAndRotation (0x18, eye and target hold; fall boundary KCL
    /// variants 1-3) just before activating out-of-bounds (PAL 0x80590EF8,
    /// 0x80590F18). The request of the activation that set BeforeRespawn.
    [[nodiscard]] u16 oobCameraRequest() const {
        return m_oobCameraRequest;
    }
    /// Presentation snapshot of the fall boundary's pocha height selector.
    /// The collision director's closest entry changes when another kart runs.
    [[nodiscard]] s8 oobWaterHeightSlot() const { return m_oobWaterHeightSlot; }
    [[nodiscard]] s8 oobLavaHeightSlot() const { return m_oobLavaHeightSlot; }
    /// Presentation uses the same post-decrement countdown as retail OOB wipe.
    [[nodiscard]] s16 respawnTimer() const { return m_respawnTimer; }
    /// QA and AI recovery callers state the camera request of an activation.
    void setOobCameraPending(u16 request) {
        m_oobCameraPending = request;
    }
    void calcWheelCollision(u16 wheelIdx, CollisionGroup *hitboxGroup, const EGG::Vector3f &colVel,
            const EGG::Vector3f &center, f32 radius);
    void calcSideCollision(CollisionData &collisionData, Hitbox &hitbox,
            Field::CollisionInfo *colInfo);
    void calcBoundingRadius();
    void calcObjectCollision();
    void calcPoleTimer();

    void processWheel(CollisionData &collisionData, Hitbox &hitbox, Field::CollisionInfo *colInfo,
            Field::KCLTypeMask *maskOut);
    void processBody(CollisionData &collisionData, Hitbox &hitbox, Field::CollisionInfo *colInfo,
            Field::KCLTypeMask *maskOut);
    void processMovingWater(CollisionData &collisionData, Field::KCLTypeMask *maskOut);
    [[nodiscard]] bool processWall(CollisionData &collisionData, Field::KCLTypeMask *maskOut);
    void processFloor(CollisionData &collisionData, Hitbox &hitbox, Field::CollisionInfo *colInfo,
            Field::KCLTypeMask *maskOut, bool wheel);
    void processCannon(Field::KCLTypeMask *maskOut);

    void applySomeFloorMoment(f32 down, f32 rate, CollisionGroup *hitboxGroup,
            const EGG::Vector3f &forward, const EGG::Vector3f &nextDir, const EGG::Vector3f &speed,
            bool b1, bool b2, bool b3);

    [[nodiscard]] bool FUN_805B6A9C(CollisionData &collisionData, const Hitbox &hitbox,
            EGG::BoundBox3f &minMax, EGG::Vector3f &relPos, s32 &count,
            const Field::KCLTypeMask &maskOut, const Field::CollisionInfo &colInfo);
    void applyBodyCollision(CollisionData &collisionData, const EGG::Vector3f &movement,
            const EGG::Vector3f &posRel, s32 count);

    void startFloorMomentRate();
    void calcFloorMomentRate();

    /// Item objects live outside the time-trial engine. RMCU KartSub pass one runs
    /// PlayerSub18_checkItemCollision right after the object collision; a race item controller
    /// registers this hook to resolve its contacts for the racer at that point.
    using ItemCollisionHook = void (*)(void *context, size_t playerIdx);
    static void SetItemCollisionHook(ItemCollisionHook hook, void *context) {
        s_itemCollisionHook = hook;
        s_itemCollisionContext = context;
    }
    void calcItemCollision();

    /// RMCU fn_1_609F4 (0x8056bf58) first hands the attacker's Thunder Cloud to the victim
    /// (0x8079156c) whenever the victim's bump timer has run out; a race item controller
    /// registers this hook.
    using KartBumpHook = void (*)(void *context, size_t attacker, size_t victim);
    static void SetKartBumpHook(KartBumpHook hook, void *context) {
        s_kartBumpHook = hook;
        s_kartBumpContext = context;
    }

    /// Racer-to-racer collision (absent from Kinoko's time-trial scope).
    void calcKartCollision();
    void calcKartBumpTimer();
    void resetKartCollision() {
        m_kartPairMask = 0;
        m_kartBumped = false;
    }

    /// Object collision functions
    Action handleReactNone(size_t idx);
    Action handleReactWallAllSpeed(size_t idx);
    Action handleReactSpinAllSpeed(size_t idx);
    Action handleReactSpinSomeSpeed(size_t idx);
    Action handleReactFireSpin(size_t idx);
    Action handleReactSmallLaunch(size_t idx);
    Action handleReactKnockbackSomeSpeedLoseItem(size_t idx);
    Action handleReactLaunchSpinLoseItem(size_t idx);
    Action handleReactKnockbackBumpLoseItem(size_t idx);
    Action handleReactLongCrushLoseItem(size_t idx);
    Action handleReactSmallBump(size_t idx);
    Action handleReactSpinShrink(size_t idx);
    Action handleReactHighLaunchLoseItem(size_t idx);
    Action handleReactWeakWall(size_t idx);
    Action handleReactOffroad(size_t idx);
    Action handleReactLaunchSpin(size_t idx);
    Action handleReactWallSpark(size_t idx);
    Action handleReactRubberWall(size_t idx);
    Action handleReactUntrickableJumpPad(size_t idx);
    Action handleReactShortCrushLoseItem(size_t idx);
    Action handleReactCrushRespawn(size_t idx);
    Action handleReactExplosionLoseItem(size_t idx);

    /// @beginSetters
    /// @addr{0x805B78D0}
    void setFloorColInfo(CollisionData &collisionData, const EGG::Vector3f &relPos,
            const EGG::Vector3f &vel, const EGG::Vector3f &floorNrm) {
        collisionData.relPos = relPos;
        collisionData.vel = vel;
        collisionData.floorNrm = floorNrm;
        collisionData.bFloor = true;
    }

    void setTangentOff(const EGG::Vector3f &v) {
        m_tangentOff = v;
    }
    void setMovement(const EGG::Vector3f &v) {
        m_movement = v;
    }
    /// @endSetters

    /// @beginGetters
    [[nodiscard]] KartPullPath &pullPath() {
        return m_pullPath;
    }

    [[nodiscard]] const KartPullPath &pullPath() const {
        return m_pullPath;
    }

    [[nodiscard]] f32 boundingRadius() const {
        return m_boundingRadius;
    }

    [[nodiscard]] f32 floorMomentRate() const {
        return m_floorMomentRate;
    }

    [[nodiscard]] const SurfaceFlags &surfaceFlags() const {
        return m_surfaceFlags;
    }

    [[nodiscard]] const EGG::Vector3f &tangentOff() const {
        return m_tangentOff;
    }

    [[nodiscard]] const EGG::Vector3f &movement() const {
        return m_movement;
    }

    [[nodiscard]] f32 sumHitboxBottomHeightSoftWall() const {
        return m_sumHitboxBottomHeightSoftWall;
    }

    [[nodiscard]] u16 numSoftWallCollisions() const {
        return m_numSoftWallCollisions;
    }

    [[nodiscard]] f32 sumHitboxBottomHeightFloorOnly() const {
        return m_sumHitboxBottomHeightFloorOnly;
    }

    [[nodiscard]] u16 numFloorOnlyCollisions() const {
        return m_numFloorOnlyCollisions;
    }

    [[nodiscard]] f32 colPerpendicularity() const {
        return m_colPerpendicularity;
    }

    /// The bump this racer received this frame (retail AI::handleBump input).
    [[nodiscard]] bool kartBumped() const {
        return m_kartBumped;
    }
    [[nodiscard]] const EGG::Vector3f &kartBumpForce() const {
        return m_kartBumpForce;
    }
    [[nodiscard]] u8 kartBumpAttacker() const {
        return m_kartBumpAttacker;
    }
    /// @endGetters

private:
    typedef Action (KartCollide::*ObjectCollisionHandler)(size_t idx);

    KartPullPath m_pullPath;
    f32 m_boundingRadius;
    f32 m_floorMomentRate;
    EGG::Vector3f m_totalReactionWallNrm;
    SurfaceFlags m_surfaceFlags;
    EGG::Vector3f m_tangentOff;
    EGG::Vector3f m_movement;
    s16 m_respawnTimer;
    s16 m_solidOobTimer;
    s16 m_shrinkTimer;
    f32 m_smoothedBack; // 0x50
    f32 m_sumHitboxBottomHeightSoftWall;
    u16 m_numSoftWallCollisions;
    f32 m_sumHitboxBottomHeightFloorOnly;
    u16 m_numFloorOnlyCollisions;
    s16 m_poleAngVelTimer;
    f32 m_poleYaw;
    f32 m_colPerpendicularity;
    s16 m_kartBumpTimer = 0;  ///< Retail +0x18: frames before this racer can be bumped again.
    u16 m_kartPairMask = 0;   ///< Retail KartState +0x84: racer pairs resolved this frame.
    bool m_kartBumped = false;
    u8 m_kartBumpAttacker = 0;
    EGG::Vector3f m_kartBumpForce;
    u16 m_oobCameraPending = 0; ///< Port hook: camera request for the next activation.
    u16 m_oobCameraRequest = 0; ///< Port hook: request of the current out-of-bounds.
    // The non-solid sink boundary stays non-solid throughout its rescue wait.
    bool m_sinkholeFall = false;
    s8 m_oobWaterHeightSlot = -1;
    s8 m_oobLavaHeightSlot = -1;
    /// mkw-3ds: applySomeFloorMoment's world-space inverse inertia tensor for the
    /// last (mainRot, invInertiaTensor) bit pattern; every wheel of one suspension
    /// pass shares those inputs, so the same product is reused bit for bit.
    EGG::Quatf m_floorMomentRot;
    EGG::Matrix34f m_floorMomentInertia;
    EGG::Matrix34f m_floorMomentTensor;
    bool m_floorMomentValid = false;

    bool checkKartCollision(KartObject *self, KartObject *other);
    void applyKartBump(f32 sign, KartObject *attacker, KartObject *victim,
            const EGG::Vector3f &dir, const EGG::Vector3f &normal) const;

    static std::array<ObjectCollisionHandler, 33> s_objectCollisionHandlers;
    static ItemCollisionHook s_itemCollisionHook;
    static KartBumpHook s_kartBumpHook;
    static void *s_kartBumpContext;
    static void *s_itemCollisionContext;
};

} // namespace Kinoko::Kart
