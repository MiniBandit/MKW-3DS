#pragma once
#include "mkw/math.hpp"
#include <array>
#include <cstdint>

namespace mkw {
// Retail kart action IDs (RMCU KartAction, section 5 + 0x2088 parameter table). The item
// system asks the kart to start these; the kart decides (Star, Bullet, Mega, respawn and
// cannon gates, then strictly higher priority) exactly as the original start function does.
namespace KartAction {
enum : int8_t {
    None = -1,
    Spin = 0,          ///< Banana, dropped Blue/Bob-omb contact after the blast phase.
    SpinSlow = 1,      ///< Spin from a zipper launch (actions 2-6 become this).
    Launch = 2,        ///< Green/Red Shell, Fake Item Box.
    StarHit = 3,       ///< Star contact.
    Knockback = 4,
    LaunchSpin = 5,
    BulletHit = 6,     ///< Bullet Bill contact.
    Explosion = 7,     ///< Bob-omb and Blue Shell blast.
    HighLaunch = 8,
    FireSpin = 9,
    Lightning = 10,
    Pow = 11,
    LongPress = 12,
    MegaCrush = 13,    ///< Flattened by a Mega Mushroom racer.
    ShortPress = 14,
    SpinShrink = 15,   ///< Shrinking object.
    CrushRespawn = 16,
    CloudStrike = 17,  ///< Thunder Cloud.
    Count = 18,
};
}

/// RMCU 0x8056325c priorities: a new action needs strictly higher priority.
inline constexpr int kartActionPriority(int action) {
    constexpr int values[] = {1, 2, 4, 4, 4, 4, 4, 6, 6, 3, 3, 5, 3, 3, 3, 3, 3, 3};
    return action >= 0 && action < KartAction::Count ? values[action] : 0;
}

/// Item loss requested by each action's start function: strong (RMCU fn_28867C) drops the
/// inventory, roulette and tethered items; weak (fn_288774) drops tethered items, only a single
/// trailing one unless the start passes flag 0 (action 5).
struct ItemKartLoss {
    bool strong = false, tethered = false, trailOnly = false;
    bool any() const { return strong || tethered || trailOnly; }
};
inline constexpr ItemKartLoss kartActionLoss(int action) {
    switch (action) {
    case KartAction::StarHit: case KartAction::Knockback: case KartAction::BulletHit:
    case KartAction::Explosion: case KartAction::HighLaunch: case KartAction::LongPress:
    case KartAction::MegaCrush: case KartAction::ShortPress: case KartAction::CrushRespawn:
        return {true, false, false};
    case KartAction::Pow: return {true, false, true};
    case KartAction::LaunchSpin: return {false, true, false};
    case KartAction::None: return {};
    default: return {false, false, true};
    }
}

/// Kart state the item system reads each step (native: Kinoko snapshot).
struct ItemRacer {
    Vec3 position{}, forward{0,0,1};
    float progress=0, speed=0, radius=65;
    bool grounded=true, unavailable=false, finished=false, controlLocked=false, inCannon=false, wallCollision=false;
    // RMCU ItemHolderPlayer_update 0x8078e91c item lock (+0xc flag 0x400): respawn, cannon,
    // action and the stopped/vanished bits; AfterCannon (102) holds an existing lock.
    bool itemLocked=false, afterCannon=false;
    Vec3 up{0,1,0},scale{1,1,1};
    // kartParam.bin itemUnk170/174/178/17c, exposed for held-item geometry.
    std::array<float,4> itemGeometry{100,80,-12,-15};
    float speedRatio=0,hop=0;
    /// KartObjectProxy::getBaseSpeed (0x805910B0, KartMove +0x14): caps the inherited speed
    /// of a backward throw (0x80795030).
    float baseSpeed=0;
    /// Status 41 / 42 (half-pipe ramp, over a zipper: a shell's launch starts its slope timer,
    /// 0x807B5178), 16 (sticky road), 15 (airtime over 20) and 83 (vanished), for the throw.
    bool zipper=false,stickyRoad=false,airtimeOver20=false,vanished=false;
    float stickX=0; ///< The pad's stick X (KPad current state, getInput +0x30).
    bool bike=false;
    Vec3 velocity{};
    bool haveVelocity=false;
    bool shellTargetUnavailable=false,shellAcquireUnavailable=false,shellTargetProtected=false;
    /// KartMove +0x74 (vel1Dir, the direction of travel) and +0x38 (smoothedUp), which the
    /// Blue Shell aims and orbits by (0x807B3FDC / 0x807B4430).
    Vec3 moveDir{0,0,1},smoothedUp{0,1,0};
    /// Retail fn_1_2AD3B4 (0x807aed08): status 47 (ZipperTrick) or 54 (TrickRot), which
    /// slows a pursuing Red's turn onto the racer's back axis.
    bool trickRotating=false;
    uint16_t itemPoint=0xffff;
    int8_t damageAction=-1; ///< Current kart action, -1 for none.
    /// Retail start() gates other than Star, Mega and Bullet: respawn and cannon family.
    bool damageProtected=false, zipperStick=false;
    /// Power-up state owned by the kart (RMCU KartMove +0x18a Star, +0x194 Mega timers,
    /// state bits 91 Bullet, 71 Shocked, 80 Crushed).
    uint16_t star=0, mega=0;
    bool bullet=false, shocked=false, crushed=false, inAction=false;
    /// The held items' wheelie lift (fn_1_281AA8 0x80791E5C): status 29 (Wheelie), the last
    /// wheel's position (getWheelPos 0x8059031C, index getWheelCount - 1), and RaceManager
    /// +0x20 == 60, the frame fn_1_285210 (0x807955C4) records that wheel's resting place.
    bool wheelie=false, wheelieReferenceFrame=false;
    Vec3 rearWheel{};
};

/// One course object an item sphere touches: the item's GeoHitTableItem response (0 none,
/// 1 bounce, 2 destroy) after the object's own callbacks, and the push-out (hit depth) that
/// moves the item off it.
struct ItemObjectHit { Vec3 depth{}; int16_t response=0; };

/// The kart side of the item system. Natively this is the Kinoko kart (KartAction, KartMove);
/// host tests substitute item_kart_model.hpp.
class ItemKarts {
public:
    virtual ~ItemKarts() = default;
    /// True when item contacts run inside the kart pass (RMCU 0x805905e4), false when the
    /// item controller resolves them itself at the start of its step.
    virtual bool contactsInKartPass() const { return false; }
    /// KartAction::start with retail r6 (force through Mega) and r8 (attacker, 12 = none).
    /// Returns the action started (a zipper launch becomes a spin) or KartAction::None.
    virtual int startAction(unsigned player, int action, bool force, unsigned attacker,
            Vec3 hitDepth) = 0;
    virtual bool activateStar(unsigned player) = 0;
    virtual void activateMega(unsigned player) = 0;
    virtual void activateMushroom(unsigned player) = 0;
    /// PlayerSub10_applyLightningEffect; false when the racer was immune.
    virtual bool lightning(unsigned player, uint16_t frames, bool spinShrink, bool cloud) = 0;
    virtual void setThunderCloud(unsigned player, bool active) = 0;
    /// The Bullet's kart status (RaceGrid drives it natively through setBulletSpeedLimit).
    virtual void setBullet(unsigned /*player*/, bool /*active*/) {}
    /// POW stomps (RMCU 0x807d9b10): the stomp's targets (KartAction +0xf8), the lift of a
    /// target that has not been airborne for more than 20 frames at stomps 1 and 2 (external
    /// velocity +6 / +10), and the hit window opened by stomp 3 that the kart reads itself.
    virtual void setPowTarget(unsigned player, bool target) = 0;
    virtual void powHop(unsigned player, float lift) = 0;
    virtual void openPowWindow() = 0;
    /// Item loss requested by the kart since the last call.
    virtual ItemKartLoss takeLoss(unsigned player) = 0;
    /// Host models advance their own kart state once per step and overlay it on the racers the
    /// test supplies; the native kart is authoritative and needs neither.
    virtual void bind(const ItemRacer * /*racers*/, unsigned /*count*/) {}
    virtual void resetKarts() {}
    virtual void advance() {}
    virtual void overlay(unsigned /*player*/, ItemRacer & /*racer*/) const {}
    /// Fine contact stage of RMCU ItemObj_checkPlayerCollision: the item's sphere against the
    /// racer's object collision hull.
    virtual bool touches(unsigned player, Vec3 center, float radius) const = 0;
    /// Item vs course objects (PAL fn_1_31AA08 0x8082ADBC: ObjectDirector's item search):
    /// retail object type, owner (12 none), the item sphere and velocity, held (tethered).
    /// The objects react through their own callbacks; returns the hits written. Host models
    /// have no course objects.
    virtual unsigned objectContacts(unsigned /*type*/, unsigned /*owner*/, Vec3 /*position*/,
            Vec3 /*velocity*/, float /*radius*/, bool /*held*/, ItemObjectHit * /*hits*/,
            unsigned /*max*/) { return 0; }
};
}
