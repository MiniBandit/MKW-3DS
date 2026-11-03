#include "KartCollide.hh"
#include "BattleEvents.hh"

#include <cstring>

#include "game/kart/KartBody.hh"
#include "game/kart/KartMove.hh"
#include "game/kart/KartObject.hh"
#include "game/kart/KartObjectManager.hh"
#include "game/kart/KartPhysics.hh"
#include "game/kart/KartState.hh"

#include "game/field/CollisionDirector.hh"
#include "game/field/ObjectCollisionKart.hh"
#include "game/field/ObjectDirector.hh"

#include <egg/math/BoundBox.hh>
#include <egg/math/Math.hh>

namespace Kinoko::Kart {

/// @addr{0x8056E56C}
KartCollide::KartCollide() {
    m_boundingRadius = 100.0f;
    m_surfaceFlags.makeAllZero();
}

/// @addr{0x80573FF0}
KartCollide::~KartCollide() = default;

/// @addr{0x8056E624}
void KartCollide::init() {
    m_pullPath.init();
    calcBoundingRadius();
    m_floorMomentRate = 0.8f;
    m_surfaceFlags.makeAllZero();
    m_respawnTimer = 0;
    m_oobWaterHeightSlot = -1;
    m_oobLavaHeightSlot = -1;
    m_sinkholeFall = false;
    m_solidOobTimer = 0;
    m_shrinkTimer = 0;
    m_smoothedBack = 0.0f;
    m_sumHitboxBottomHeightFloorOnly = 0.0f;
    m_sumHitboxBottomHeightSoftWall = 0.0f;
    m_numFloorOnlyCollisions = 0;
    m_numSoftWallCollisions = 0;
    m_poleAngVelTimer = 0;
    m_poleYaw = 0.0f;
    m_colPerpendicularity = 0.0f;
    m_kartBumpTimer = 0;
    m_kartPairMask = 0;
    m_kartBumped = false;
}

namespace {
/// Retail PlayerSub18_checkPlayerCollision state gate: respawn, cannon, crushed
/// (with state bit 81) and ghost racers never touch another racer.
bool KartCollisionExempt(const KartObject *kart) {
    const auto &status = kart->status();
    return status.onBit(eStatus::TriggerRespawn, eStatus::InRespawn, eStatus::AfterRespawn,
            eStatus::CannonStart, eStatus::InCannon, eStatus::AfterCannon, eStatus::BattleHidden);
}
} // namespace

KartCollide::ItemCollisionHook KartCollide::s_itemCollisionHook = nullptr;
void *KartCollide::s_itemCollisionContext = nullptr;
KartCollide::KartBumpHook KartCollide::s_kartBumpHook = nullptr;
void *KartCollide::s_kartBumpContext = nullptr;

/// @brief RMCU 0x805905e4: item contacts for this racer, after its object collision.
void KartCollide::calcItemCollision() {
    if (s_itemCollisionHook) {
        s_itemCollisionHook(s_itemCollisionContext, param()->playerIdx());
    }
}

/// @brief Retail KartSub pass-one racer loop (USA 0x805904D8 + 0xcc): every racer
/// near this one goes through PlayerSub18_checkPlayerCollision once per frame.
void KartCollide::calcKartCollision() {
    auto *objects = KartObjectManager::Instance();
    if (!objects) {
        return;
    }
    const size_t count = objects->count();
    const u8 selfIdx = param()->playerIdx();
    if (count < 2 || selfIdx >= count) {
        return;
    }
    KartObject *self = objects->object(selfIdx);
    for (size_t i = 0; i < count; ++i) {
        if (i != selfIdx) {
            checkKartCollision(self, objects->object(i));
        }
    }
}

/// @brief Retail fn_1_61004 (USA 0x8056C568), late in pass one.
void KartCollide::calcKartBumpTimer() {
    m_kartBumpTimer = std::max<s16>(m_kartBumpTimer - 1, 0);
}

/// @brief Retail PlayerSub18_checkPlayerCollision (USA 0x8056A9A0).
/// Every body sphere pair is tested (fn_1_5EB40, USA 0x8056A0A4); the push
/// box min+max gives the separation, split half to each racer through the
/// tangent offset and movement. Star, Mega and Bullet contact are item
/// reactions instead of a bump (applied by the race item code); otherwise both
/// racers receive the weight-scaled lateral bump of fn_1_609F4.
bool KartCollide::checkKartCollision(KartObject *self, KartObject *other) {
    const u8 selfIdx = param()->playerIdx();
    const u8 otherIdx = other->param()->playerIdx();
    if (selfIdx == otherIdx || KartCollisionExempt(self) || KartCollisionExempt(other) ||
            (m_kartPairMask & (1u << otherIdx))) {
        return false;
    }
    KartCollide *otherCollide = other->collide();
    m_kartPairMask |= static_cast<u16>(1u << otherIdx);
    otherCollide->m_kartPairMask |= static_cast<u16>(1u << selfIdx);

    CollisionGroup *mine = collisionGroup();
    CollisionGroup *theirs = other->collisionGroup();
    const f32 reach = mine->boundingRadius() + theirs->boundingRadius() + 200.0f;
    if ((other->pos() - pos()).squaredLength() > reach * reach) {
        return false;
    }

    // Both boxes start at zero, so min+max of a single contact is the contact.
    EGG::Vector3f pushMin = EGG::Vector3f::zero;
    EGG::Vector3f pushMax = EGG::Vector3f::zero;
    EGG::Vector3f contactMin = EGG::Vector3f::zero;
    EGG::Vector3f contactMax = EGG::Vector3f::zero;
    bool hit = false;
    for (u16 i = 0; i < mine->hitboxCount(); ++i) {
        const Hitbox &a = mine->hitbox(i);
        for (u16 j = 0; j < theirs->hitboxCount(); ++j) {
            const Hitbox &b = theirs->hitbox(j);
            EGG::Vector3f delta = b.worldPos() - a.worldPos();
            const f32 sumRadius = a.radius() + b.radius();
            const f32 distSq = delta.squaredLength();
            if (sumRadius * sumRadius - distSq <= 0.0f) {
                continue;
            }
            const f32 depth = sumRadius - EGG::Mathf::sqrt(distSq);
            delta.normalise();
            const EGG::Vector3f contact = a.worldPos() + delta * a.radius();
            contactMin = contactMin.minimize(contact);
            contactMax = contactMax.maximize(contact);
            const EGG::Vector3f push = delta * depth;
            pushMin = pushMin.minimize(push);
            pushMax = pushMax.maximize(push);
            hit = true;
        }
    }
    if (!hit) {
        return false;
    }

    // This port resolves each pair once, so notify both presenters. Otherwise
    // racer 0, which checks its pairs first, never receives its local effect.
    const EGG::Vector3f contact = contactMin + contactMax;
    self->move()->cueBumpedBy(otherIdx, contact);
    other->move()->cueBumpedBy(selfIdx, contact);

    EGG::Vector3f normal = pushMin + pushMax;
    const f32 length = normal.normalise();
    const EGG::Vector3f half = normal * (0.5f * length);
    m_tangentOff -= half;
    m_movement -= half;
    otherCollide->m_tangentOff += half;
    otherCollide->m_movement += half;

    // Power-up contact (RMCU 0x8056ab9c..0x8056b8d4): Star bit 63, Mega 79, Crushed 80,
    // Bullet 91. The racer without the power-up gets the action instead of a bump, whether or
    // not the action is accepted. Star and Bullet force through a Mega (start r6 = 1); Mega
    // crushes with action 13. The hit direction points away from the attacker.
    const auto &selfStatus = status();
    const auto &otherStatus = other->status();
    const bool selfBullet = selfStatus.onBit(eStatus::Bullet);
    const bool otherBullet = otherStatus.onBit(eStatus::Bullet);
    const bool selfStar = selfStatus.onBit(eStatus::Star);
    const bool otherStar = otherStatus.onBit(eStatus::Star);
    auto react = [&](KartObject *victim, KartObject *attacker, const EGG::Vector3f &away,
                         Action reaction, bool force) {
        KartAction *victimAction = victim->action();
        victimAction->setHitDepth(away);
        if (victimAction->start(reaction, force, attacker->param()->playerIdx())) {
            victim->move()->cueSound(KartMove::SoundCue::StarHit);   // USA 0x8056b0b8
            attacker->move()->cueSound(KartMove::SoundCue::PowerHit); // USA 0x8056b294
        }
    };
    bool bump = true;
    if (selfBullet == otherBullet) {
        if (selfStar != otherStar) {
            if (otherStar && !selfBullet && !selfStar) {
                react(self, other, -normal, Action::UNK_3, true);
                bump = false;
            } else if (selfStar && !otherBullet && !otherStar) {
                react(other, self, normal, Action::UNK_3, true);
                bump = false;
            }
        } else {
            const bool selfMega = selfStatus.onBit(eStatus::Mega);
            const bool otherMega = otherStatus.onBit(eStatus::Mega);
            if (otherMega && !selfMega && selfStatus.offBit(eStatus::Crushed)) {
                react(self, other, -normal, Action::UNK_13, false);
                bump = false;
            } else if (selfMega && !otherMega && otherStatus.offBit(eStatus::Crushed)) {
                react(other, self, normal, Action::UNK_13, false);
                bump = false;
            }
        }
    } else if (otherBullet && !selfStar) {
        react(self, other, -normal, Action::UNK_6, true);
        bump = false;
    } else if (selfBullet && !otherStar) {
        react(other, self, normal, Action::UNK_6, true);
        bump = false;
    }
    if (!bump) {
        return true;
    }

    // Lateral bump directions: each racer's right axis (KartObjectProxy::
    // bodyForward, first pose column) in the plane of its KartMove direction.
    EGG::Vector3f otherRight = other->bodyForward().perpInPlane(other->move()->dir(), true);
    EGG::Vector3f otherDir = otherRight * normal.dot(otherRight);
    otherDir.normalise();
    EGG::Vector3f selfRight = bodyForward().perpInPlane(move()->dir(), true);
    EGG::Vector3f selfDir = selfRight * otherDir.dot(selfRight);
    selfDir.normalise();
    if (!otherBullet) {
        applyKartBump(1.0f, self, other, otherDir, normal);
    }
    if (!selfBullet) {
        applyKartBump(-1.0f, other, self, selfDir, normal);
    }

    // Retail RMCU 8056bab4..8056be64: only mushroom state26 against a
    // non-mushroom kart can transfer balloons/coins. Star/Mega/Bullet above
    // are damage contacts. Use the actual hull contact, not a radius guess.
    auto transfer=[](KartObject* thief,KartObject* victim){
        const auto& source=thief->status();const auto& target=victim->status();
        if(source.offBit(eStatus::MushroomBoost)||target.onBit(eStatus::MushroomBoost,
                eStatus::BeforeRespawn,eStatus::TriggerRespawn,eStatus::BattleDamageProtection,
                eStatus::InRespawn,eStatus::AfterRespawn,eStatus::BattleVanish,eStatus::BattleHidden,
                eStatus::BattleEmpty,eStatus::InAction,eStatus::CannonStart,eStatus::InCannon,
                eStatus::Star,eStatus::Mega,eStatus::Bullet,static_cast<eStatus>(89)))return;
        // Retail starts protection before the race-mode callback, even when
        // its team/count gate subsequently declines the transfer.
        victim->move()->beginBattleProtection();
        BattleEvents::Emit(BattleEvents::Kind::Steal,victim->param()->playerIdx(),thief->param()->playerIdx());
    };
    transfer(self,other);transfer(other,self);

    // KartMove vtable +0x30: bikes leave a wheelie (KartMoveBike::cancelWheelie).
    if (self->isBike()) {
        static_cast<KartMoveBike *>(move())->cancelWheelie();
    }
    if (other->isBike()) {
        static_cast<KartMoveBike *>(other->move())->cancelWheelie();
    }
    move()->cueSound(KartMove::SoundCue::Horn); // USA 0x8056beac
    return true;
}

/// @brief Retail fn_1_609F4 (USA 0x8056BF58): the bump the attacker gives the victim.
/// The weight-class lever arm, bump deviation and roll axis come from this racer
/// (the one running the check) for both calls, as in the original.
void KartCollide::applyKartBump(f32 sign, KartObject *attacker, KartObject *victim,
        const EGG::Vector3f &dir, const EGG::Vector3f &normal) const {
    constexpr s16 BUMP_COOLDOWN = 10;

    KartCollide *victimCollide = victim->collide();
    if (victimCollide->m_kartBumpTimer >= 1) {
        return;
    }
    if (s_kartBumpHook) {
        s_kartBumpHook(s_kartBumpContext, attacker->param()->playerIdx(),
                victim->param()->playerIdx());
    }
    KartMove *victimMove = victim->move();
    const auto &victimStatus = victim->status();

    f32 attackerWeight = attacker->param()->stats().weight;
    f32 victimWeight = victim->param()->stats().weight;
    // Slipstream boost (state bit 65) bumps at least as heavy as the other racer.
    if (attacker->move()->slipstreamBoost() && attackerWeight < victimWeight) {
        attackerWeight = victimWeight;
    }
    if (victimMove->slipstreamBoost() && victimWeight < attackerWeight) {
        victimWeight = attackerWeight;
    }
    // KartBoost +0x118 bit 2: the mushroom / boost panel boost.
    if (attacker->move()->boostActive(KartBoost::Type::MushroomAndBoostPanel)) {
        attackerWeight *= 1.5f;
    }
    if (victimMove->boostActive(KartBoost::Type::MushroomAndBoostPanel)) {
        attackerWeight *= 0.5f;
    }
    const f32 ratio = attackerWeight / victimWeight;

    const EGG::Vector3f &victimUp = victimMove->smoothedUp();
    const EGG::Vector3f lateral = dir.perpInPlane(victimUp, true);
    const f32 force =
            std::min(100.0f, ratio * (3.0f + 0.11f * EGG::Mathf::abs(attacker->move()->speed())));

    const f32 weightClass = static_cast<f32>(static_cast<s32>(param()->stats().weightClass));
    const f32 lever = 0.3f * (1.0f + weightClass) * (-sign * victimCollide->boundingRadius());
    const EGG::Vector3f point = victim->pos() + normal * lever;

    f32 magnitude = sign * force;
    if (victimStatus.onBit(eStatus::OverZipper)) {
        magnitude *= 0.2f;
    } else if (victimStatus.onBit(eStatus::AirtimeOver20)) {
        magnitude *= 0.5f;
    }
    EGG::Vector3f impulse = lateral * magnitude;
    impulse = impulse - victimUp * impulse.dot(victimUp);

    victimMove->cueBump(magnitude); // USA 0x8056c388

    KartDynamics *victimDynamics = victim->dynamics();
    victimDynamics->addForce(impulse);
    victimDynamics->applyWrenchScaled(point, impulse, param()->stats().bumpDeviationLevel);

    const f32 speed = victimMove->speed();
    const f32 absSpeed = EGG::Mathf::abs(speed);
    const f32 slowed = std::max(0.5f * absSpeed, absSpeed - 0.5f * force);
    victimMove->setSpeed(speed < 0.0f ? -slowed : slowed);

    victimCollide->m_kartBumpTimer = BUMP_COOLDOWN;
    victimCollide->m_floorMomentRate = 0.01f;

    // Roll kick about the forward axis, signed by this racer's facing.
    const EGG::Vector3f roll = lateral.cross(componentZAxis());
    const f32 rollSign = roll.y < 0.0f ? 1.0f : -1.0f;
    victimDynamics->setAngVel0(
            victimDynamics->angVel0() + EGG::Vector3f(0.0f, 0.0f, rollSign * 0.05f * force));

    victimCollide->m_kartBumped = true;
    victimCollide->m_kartBumpForce = impulse;
    victimCollide->m_kartBumpAttacker = attacker->param()->playerIdx();
}

/// @addr{0x805730D4}
void KartCollide::resetHitboxes() {
    CollisionGroup *hitboxGroup = physics()->hitboxGroup();
    for (u16 idx = 0; idx < hitboxGroup->hitboxCount(); ++idx) {
        hitboxGroup->hitbox(idx).setLastPos(scale(), pose());
    }
}

/// @stage All
/// @brief On each frame, calculates the positions for each hitbox
/// @addr{0x8056EE24}
void KartCollide::calcHitboxes() {
    CollisionGroup *hitboxGroup = physics()->hitboxGroup();
    for (u16 idx = 0; idx < hitboxGroup->hitboxCount(); ++idx) {
        hitboxGroup->hitbox(idx).calc(move()->totalScale(), body()->sinkDepth(), scale(), fullRot(),
                pos());
    }
}

/// @stage All
/// @addr{0x80572C20}
void KartCollide::findCollision() {
    bool wasHalfPipe = status().onBit(eStatus::EndHalfPipe, eStatus::ActionMidZipper);
    const EGG::Quatf &rot = wasHalfPipe ? mainRot() : fullRot();
    calcBodyCollision(move()->totalScale(), body()->sinkDepth(), rot, scale());

    auto &colData = collisionData();
    bool existingWallCollision = colData.bWall || colData.bWall3;
    bool newWallCollision =
            m_surfaceFlags.onBit(eSurfaceFlags::ObjectWall, eSurfaceFlags::ObjectWall3);
    if (existingWallCollision || newWallCollision) {
        if (!existingWallCollision) {
            colData.wallNrm = m_totalReactionWallNrm;
            if (m_surfaceFlags.onBit(eSurfaceFlags::ObjectWall)) {
                colData.bWall = true;
            } else if (m_surfaceFlags.onBit(eSurfaceFlags::ObjectWall3)) {
                colData.bWall3 = true;
            }
        } else if (newWallCollision) {
            colData.wallNrm += m_totalReactionWallNrm;
            if (m_surfaceFlags.onBit(eSurfaceFlags::ObjectWall)) {
                colData.bWall = true;
            } else if (m_surfaceFlags.onBit(eSurfaceFlags::ObjectWall3)) {
                colData.bWall3 = true;
            }
        }

        colData.wallNrm.normalise();
    }

    FUN_80572F4C();
}

/// @stage 2
/// @addr{0x80572F4C}
/// @rename
void KartCollide::FUN_80572F4C() {
    f32 fVar1;

    auto &status = KartObjectProxy::status();

    if (isInRespawn() ||
            status.onBit(eStatus::Boost, eStatus::OverZipper, eStatus::ZipperInvisibleWall,
                    eStatus::NoSparkInvisibleWall, eStatus::HalfPipeRamp)) {
        fVar1 = 0.0f;
    } else {
        fVar1 = 0.05f;
    }

    bool resetXZ = fVar1 > 0.0f && status.onBit(eStatus::AirtimeOver20) &&
            dynamics()->velocity().y < -50.0f;

    FUN_805B72B8(status.onBit(eStatus::InAction) ? 0.3f : 0.01f, fVar1, resetXZ,
            status.offBit(eStatus::JumpPadDisableYsusForce));
}

/// @stage All
/// @brief Affects velocity when landing from airtime.
/// @addr{0x805B72B8}
/// @details Every frame, this function checks the player's velocity relative to the floor collision
/// and applies external velocity and angular velocity.
/// This is mostly relevant when you were midair and just landed.
/// @rename
void KartCollide::FUN_805B72B8(f32 param_1, f32 param_2, bool lockXZ, bool addExtVelY) {
    const auto &colData = collisionData();

    if (!colData.bFloor && !colData.bWall && !colData.bWall3) {
        return;
    }

    EGG::Vector3f collisionDir = colData.floorNrm + colData.wallNrm;
    collisionDir.normalise();

    f32 directionalVelocity = colData.vel.dot(collisionDir);
    if (directionalVelocity >= 0.0f) {
        return;
    }

    EGG::Matrix34f rotMat;
    EGG::Matrix34f rotMatTrans;

    rotMat.makeQ(dynamics()->mainRot());
    rotMatTrans = rotMat.transpose();
    rotMat = rotMat.multiplyTo(dynamics()->invInertiaTensor()).multiplyTo(rotMatTrans);

    EGG::Vector3f relPos = colData.relPos;
    if (lockXZ) {
        relPos.x = 0.0f;
        relPos.z = 0.0f;
    }

    EGG::Vector3f step1 = relPos.cross(collisionDir);
    EGG::Vector3f step2 = rotMat.multVector33(step1);
    EGG::Vector3f step3 = step2.cross(relPos);
    f32 val = (-directionalVelocity * (1.0f + param_2)) / (1.0f + collisionDir.dot(step3));
    EGG::Vector3f step4 = collisionDir.cross(-colData.vel);
    EGG::Vector3f step5 = step4.cross(collisionDir);
    step5.normalise();

    f32 fVar1 = param_1 * EGG::Mathf::abs(val);
    f32 otherVal = (val * colData.vel.dot(step5)) / directionalVelocity;

    f32 fVar3 = otherVal;
    if (fVar1 < EGG::Mathf::abs(otherVal)) {
        fVar3 = fVar1;
        if (otherVal < 0.0f) {
            fVar3 = -param_1 * EGG::Mathf::abs(val);
        }
    }

    EGG::Vector3f step6 = val * collisionDir + fVar3 * step5;

    f32 local_1d0 = step6.y;
    if (!addExtVelY) {
        local_1d0 = 0.0f;
    } else if (colData.bFloor) {
        f32 velY = intVel().y;
        if (velY > 0.0f) {
            velY += extVel().y;
            if (velY < 0.0f) {
                EGG::Vector3f newExtVel = extVel();
                newExtVel.y = velY;
                dynamics()->setExtVel(newExtVel);
            }
        }
    }

    f32 prevExtVelY = extVel().y;
    EGG::Vector3f extVelAdd = step6;
    extVelAdd.y = local_1d0;
    dynamics()->setExtVel(extVel() + extVelAdd);

    if (prevExtVelY < 0.0f && extVel().y > 0.0f && extVel().y < 10.0f) {
        EGG::Vector3f extVelNoY = extVel();
        extVelNoY.y = 0.0f;
        dynamics()->setExtVel(extVelNoY);
    }

    EGG::Vector3f step7 = relPos.cross(step6);
    EGG::Vector3f step8 = rotMat.multVector33(step7);
    EGG::Vector3f step9 = mainRot().rotateVectorInv(step8);
    step9.y = 0.0f;
    dynamics()->setAngVel0(dynamics()->angVel0() + step9);
}

/// @stage All
/// @addr{0x805B6724}
/// @brief Checks and acts on collision for each kart hitbox.
/// @param totalScale
/// @param rot
/// @param scale
void KartCollide::calcBodyCollision(f32 totalScale, f32 sinkDepth, const EGG::Quatf &rot,
        const EGG::Vector3f &scale) {
    CollisionGroup *hitboxGroup = physics()->hitboxGroup();
    CollisionData &collisionData = hitboxGroup->collisionData();
    collisionData.reset();

    EGG::Vector3f posRel = EGG::Vector3f::zero;
    s32 count = 0;
    Field::CollisionInfo colInfo;
    colInfo.bbox.setDirect(EGG::Vector3f::zero, EGG::Vector3f::zero);
    Field::KCLTypeMask maskOut;
    Field::CourseColMgr::NoBounceWallColInfo noBounceWallInfo;
    EGG::BoundBox3f minMax;
    minMax.setZero();
    bool bVar1 = false;

    for (u16 hitboxIdx = 0; hitboxIdx < hitboxGroup->hitboxCount(); ++hitboxIdx) {
        Field::KCLTypeMask flags = KCL_TYPE_DRIVER_SOLID_SURFACE;
        if(m_sinkholeFall&&status().onBit(eStatus::BeforeRespawn))flags&=~KCL_TYPE_FLOOR;
        Hitbox &hitbox = hitboxGroup->hitbox(hitboxIdx);

        if (hitbox.bspHitbox()->wallsOnly != 0) {
            flags = 0x4A109000;
            Field::CourseColMgr::Instance()->setNoBounceWallInfo(&noBounceWallInfo);
        }

        hitbox.calc(totalScale, sinkDepth, scale, rot, pos());

        if (Field::CollisionDirector::Instance()->checkSphereCachedFullPush(hitbox.radius(),
                    hitbox.worldPos(), hitbox.lastPos(), flags, &colInfo, &maskOut, 0)) {
            if (!!(maskOut & KCL_TYPE_VEHICLE_COLLIDEABLE)) {
                Field::CollisionDirector::Instance()->findClosestCollisionEntry(&maskOut,
                        KCL_TYPE_VEHICLE_COLLIDEABLE);
            }

            if (!FUN_805B6A9C(collisionData, hitbox, minMax, posRel, count, maskOut, colInfo)) {
                bVar1 = true;

                if (colInfo.movingFloorDist > -std::numeric_limits<f32>::min()) {
                    collisionData.bHasRoadVel = true;
                    collisionData.roadVelocity = colInfo.roadVelocity;
                }

                processBody(collisionData, hitbox, &colInfo, &maskOut);
            }
        }
    }

    if (bVar1) {
        EGG::Vector3f movement = minMax.min + minMax.max;
        applyBodyCollision(collisionData, movement, posRel, count);
    } else {
        collisionData.speedFactor = 1.0f;
        collisionData.rotFactor = 1.0f;
    }
}

/// @addr{0x80571634}
void KartCollide::calcFloorEffect() {
    if (status().onBit(eStatus::TouchingGround)) {
        m_surfaceFlags.setBit(eSurfaceFlags::Offroad, eSurfaceFlags::GroundBoostPanelOrRamp);
    }

    m_sumHitboxBottomHeightFloorOnly = 0.0f;
    m_surfaceFlags.resetBit(eSurfaceFlags::Wall, eSurfaceFlags::SolidOOB, eSurfaceFlags::BoostRamp,
            eSurfaceFlags::Offroad, eSurfaceFlags::Trickable, eSurfaceFlags::NotTrickable,
            eSurfaceFlags::StopHalfPipeState);
    m_sumHitboxBottomHeightSoftWall = 0.0f;
    m_numFloorOnlyCollisions = 0;
    m_numSoftWallCollisions = 0;

    Field::KCLTypeMask mask = KCL_NONE;
    calcTriggers(&mask, pos(), false);

    auto *colDir = Field::CollisionDirector::Instance();

    if (m_solidOobTimer >= 3 && m_surfaceFlags.onBit(eSurfaceFlags::SolidOOB) &&
            m_surfaceFlags.offBit(eSurfaceFlags::Wall)) {
        if (mask & KCL_TYPE_BIT(COL_TYPE_SOLID_OOB)) {
            colDir->findClosestCollisionEntry(&mask, KCL_TYPE_BIT(COL_TYPE_SOLID_OOB));
        }

        // Retail Kart::Link::SetCamera (presentation request only).
        m_oobCameraPending = 0x8;
        activateOob(true, &mask, false, false);
    }

    mask = KCL_NONE;
    calcTriggers(&mask, pos(), true);

    m_solidOobTimer =
            m_surfaceFlags.onBit(eSurfaceFlags::SolidOOB) ? std::min(3, m_solidOobTimer + 1) : 0;

    if (status().onBit(eStatus::Wall3Collision, eStatus::WallCollision)) {
        Field::KCLTypeMask maskOut = KCL_NONE;

        if (colDir->checkSphereCachedPartialPush(m_boundingRadius, pos(), EGG::Vector3f::inf,
                    KCL_TYPE_BIT(COL_TYPE_FALL_BOUNDARY), nullptr, &maskOut, 0)) {
            calcFallBoundary(&maskOut, true);
        }
    }
}

/// @addr{0x805718D4}
void KartCollide::calcTriggers(Field::KCLTypeMask *mask, const EGG::Vector3f &pos, bool twoPoint) {
    EGG::Vector3f v1 = twoPoint ? physics()->pos() : EGG::Vector3f::inf;
    Field::KCLTypeMask typeMask = twoPoint ? KCL_TYPE_DIRECTIONAL : KCL_TYPE_NON_DIRECTIONAL;
    f32 radius = twoPoint ? 80.0f : 100.0f * move()->totalScale();
    f32 scalar = -bsp().initialYPos * move()->totalScale() * 0.3f;
    EGG::Vector3f scaledPos = pos + scalar * componentYAxis();
    EGG::Vector3f back = dynamics()->mainRot().rotateVector(EGG::Vector3f::ez);

    m_smoothedBack += (back.dot(move()->smoothedUp()) - m_smoothedBack) * 0.3f;

    scalar = m_smoothedBack * -physics()->fc() * 1.8f * move()->totalScale();
    scaledPos += scalar * back;

    bool collide = Field::CollisionDirector::Instance()->checkSphereCachedPartialPush(radius,
            scaledPos, v1, typeMask, nullptr, mask, 0);

    if (!collide) {
        return;
    }

    if (twoPoint) {
        handleTriggers(mask);
    } else {
        if (*mask & KCL_TYPE_FLOOR) {
            Field::CollisionDirector::Instance()->findClosestCollisionEntry(mask, KCL_TYPE_FLOOR);
        }

        if (*mask & KCL_TYPE_WALL) {
            m_surfaceFlags.setBit(eSurfaceFlags::Wall);
        }

        if (*mask & KCL_TYPE_BIT(COL_TYPE_SOLID_OOB)) {
            m_surfaceFlags.setBit(eSurfaceFlags::SolidOOB);
        }
    }
}

/// @addr{0x8056F510}
void KartCollide::handleTriggers(Field::KCLTypeMask *mask) {
    // Retail ProcessTriggers (USA 0x8056a6c0) first hands a sound trigger's variant to
    // KartActor::ApplyKCLSoundTrigger (a presentation cue; processCannon below always
    // replaces the closest entry, as in retail).
    if (*mask & KCL_TYPE_BIT(COL_TYPE_SOUND_TRIGGER)) {
        auto *colDir = Field::CollisionDirector::Instance();
        if (colDir->findClosestCollisionEntry(mask, KCL_TYPE_BIT(COL_TYPE_SOUND_TRIGGER))) {
            move()->cueSoundTrigger(static_cast<u8>(colDir->closestCollisionEntry()->variant()));
        }
    }

    calcFallBoundary(mask, false);
    processCannon(mask);

    if (*mask & KCL_TYPE_BIT(COL_TYPE_EFFECT_TRIGGER)) {
        auto *colDir = Field::CollisionDirector::Instance();
        if (colDir->findClosestCollisionEntry(mask, KCL_TYPE_BIT(COL_TYPE_EFFECT_TRIGGER))) {
            if (colDir->closestCollisionEntry()->variant() == 4) {
                halfPipe()->end(true);
                status().setBit(eStatus::EndHalfPipe);
                m_surfaceFlags.setBit(eSurfaceFlags::StopHalfPipeState);
            }
        }
    }
}

/// @addr{0x80571D98}
void KartCollide::calcFallBoundary(Field::KCLTypeMask *mask, bool shortBoundary) {
    if (!(*mask & KCL_TYPE_BIT(COL_TYPE_FALL_BOUNDARY))) {
        return;
    }

    auto *colDir = Field::CollisionDirector::Instance();
    if (!colDir->findClosestCollisionEntry(mask, KCL_TYPE_BIT(COL_TYPE_FALL_BOUNDARY))) {
        return;
    }

    bool safe = false;
    const auto *entry = colDir->closestCollisionEntry();

    if (shortBoundary) {
        if (entry->variant() != 7) {
            safe = true;
        }
    }

    if (!safe) {
        // Retail fn_1_619E4: variants 1-3 StopCameraMovAndRotation, others
        // SetCamera; ActivateOOB(false, ...) adds UpdateCameraOnOOBHit
        // (presentation request only).
        const u16 variant = entry->variant();
        m_oobCameraPending = (variant >= 1 && variant <= 3 ? 0x18 : 0x8) | 0x400;
        activateOob(false, mask, false, false);
    }
}

/// @addr{0x80573ED4}
void KartCollide::calcBeforeRespawn() {
    if (pos().y < 0.0f) {
        activateOob(true, nullptr, false, false);
    }

    auto &status = KartObjectProxy::status();

    if (status.onBit(eStatus::BeforeRespawn)) {
        if (--m_respawnTimer > 0) {
            return;
        }

        status.resetBit(eStatus::BeforeRespawn);
        m_respawnTimer = 0;
        move()->triggerRespawn();
    }

    m_shrinkTimer = std::max(0, m_shrinkTimer - 1);
}

/// @addr{0x80573B00}
void KartCollide::activateOob(bool detachCamera, Field::KCLTypeMask *mask,
        bool /*somethingCPU*/, bool somethingBullet) {
    constexpr s16 RESPAWN_TIME = 130;

    auto &status = KartObjectProxy::status();
    const u16 cameraRequest = m_oobCameraPending;
    m_oobCameraPending = 0;

    if (status.onBit(eStatus::BeforeRespawn)) {
        return;
    }

    // PC retail 8056ecf0..8056ed20 -> 806a7228: water fall variants 1/3
    // select the pocha height with attribute bits 8..10, independently of
    // the moving-sea object and Diving status. Snapshot before initOob.
    m_oobWaterHeightSlot = -1;
    m_oobLavaHeightSlot = -1;
    m_sinkholeFall = false;
    if (mask && !detachCamera && !somethingBullet) {
        const auto *entry = Field::CollisionDirector::Instance()->closestCollisionEntry();
        if (entry && (entry->variant() == 1 || entry->variant() == 3))
            m_oobWaterHeightSlot = (static_cast<u16>(entry->attribute) >> 8) & 7;
        // Retail sand centre is fall-boundary6 (USA80800b10..b24).
        // Keep its non-solid state until rescue; a new wave must not catch
        // an already sinking kart and push it back above the sand.
        m_sinkholeFall = entry && entry->variant() == 6;
        if (entry && entry->variant() == 2)
            m_oobLavaHeightSlot = (static_cast<u16>(entry->attribute) >> 8) & 7;
    }

    // Port, audio only: retail fn_1_610D0 (USA 0x8056c634) picks the fall voice
    // from the variant of the entry the caller found (fn_1_611BC).
    {
        u8 voice = detachCamera ? 0x19 : 0x16;
        bool chosen = false;
        const auto *entry = Field::CollisionDirector::Instance()->closestCollisionEntry();
        const u16 variant = entry ? entry->variant() : 0;
        if (mask) {
            if (detachCamera) {
                if (variant == 0) {
                    voice = 0x17;
                    chosen = true;
                }
            } else if (variant >= 1 && variant <= 3) {
                voice = variant == 2 ? 0x18 : 0x17;
                chosen = variant != 2;
            } else if (variant == 4) {
                voice = 0x18;
            }
        }
        if (!chosen) {
            if (!mask && detachCamera) {
                voice = 0x17;
            } else if (!detachCamera && mask && variant == 7) {
                voice = 0x19;
            }
        }
        move()->cueFall(voice);
    }

    m_oobCameraRequest = cameraRequest;
    move()->initOob();

    m_respawnTimer = RESPAWN_TIME;
    status.setBit(eStatus::BeforeRespawn);
}

/// @stage All
/// @brief Checks wheel hitbox collision and stores position/velocity info.
/// @addr{0x805B6F4C}
/// @param hitboxGroup The wheel's collision information
/// @param colVel The wheel's velocity. In the base game, it is always \f$\vec{v} = \begin{bmatrix}
/// 0 \\ -13 \\ 0 \end{bmatrix}\f$
/// @param center The wheel's position
/// @param radius The wheel's size
void KartCollide::calcWheelCollision(u16 /*wheelIdx*/, CollisionGroup *hitboxGroup,
        const EGG::Vector3f &colVel, const EGG::Vector3f &center, f32 radius) {
    Hitbox &firstHitbox = hitboxGroup->hitbox(0);
    BSP::Hitbox *bspHitbox = const_cast<BSP::Hitbox *>(firstHitbox.bspHitbox());
    bspHitbox->radius = radius;
    hitboxGroup->resetCollision();
    firstHitbox.setWorldPos(center);

    Field::CollisionInfo colInfo;
    colInfo.bbox.setZero();
    Field::KCLTypeMask kclOut;
    Field::CourseColMgr::NoBounceWallColInfo noBounceWallInfo;
    Field::CourseColMgr::Instance()->setNoBounceWallInfo(&noBounceWallInfo);

    bool collided = Field::CollisionDirector::Instance()->checkSphereCachedFullPush(
            firstHitbox.radius(), firstHitbox.worldPos(), firstHitbox.lastPos(),
            m_sinkholeFall&&status().onBit(eStatus::BeforeRespawn)?
                    KCL_TYPE_VEHICLE_COLLIDEABLE&~KCL_TYPE_FLOOR:KCL_TYPE_VEHICLE_COLLIDEABLE,
            &colInfo, &kclOut, 0);

    CollisionData &collisionData = hitboxGroup->collisionData();

    if (!collided) {
        collisionData.speedFactor = 1.0f;
        collisionData.rotFactor = 1.0f;
        return;
    }

    collisionData.tangentOff = colInfo.tangentOff;

    if (noBounceWallInfo.dist > std::numeric_limits<f32>::min()) {
        collisionData.tangentOff += noBounceWallInfo.tangentOff;
        collisionData.noBounceWallNrm = noBounceWallInfo.fnrm;
        collisionData.bSoftWall = true;
    }

    if (kclOut & KCL_TYPE_FLOOR) {
        collisionData.bFloor = true;
        collisionData.floorNrm = colInfo.floorNrm;
    }

    collisionData.relPos = firstHitbox.worldPos() - pos();
    collisionData.vel = colVel;

    if (colInfo.movingFloorDist > -std::numeric_limits<f32>::min()) {
        collisionData.bHasRoadVel = true;
        collisionData.roadVelocity = colInfo.roadVelocity;
    }

    processWheel(collisionData, firstHitbox, &colInfo, &kclOut);

    if (!(kclOut & KCL_TYPE_VEHICLE_COLLIDEABLE)) {
        return;
    }

    Field::CollisionDirector::Instance()->findClosestCollisionEntry(&kclOut,
            KCL_TYPE_VEHICLE_COLLIDEABLE);
}

/// @stage 2
/// @addr{0x8056F26C}
void KartCollide::calcSideCollision(CollisionData &collisionData, Hitbox &hitbox,
        Field::CollisionInfo *colInfo) {
    if (colInfo->perpendicularity <= 0.0f) {
        return;
    }

    m_colPerpendicularity = std::max(m_colPerpendicularity, colInfo->perpendicularity);

    if (collisionData.bWallAtLeftCloser || collisionData.bWallAtRightCloser) {
        return;
    }

    f32 bspPosX = hitbox.bspHitbox()->position.x;
    if (EGG::Mathf::abs(bspPosX) > 10.0f) {
        if (bspPosX > 0.0f) {
            collisionData.bWallAtLeftCloser = true;
        } else {
            collisionData.bWallAtRightCloser = true;
        }

        collisionData.colPerpendicularity = colInfo->perpendicularity;

        return;
    }

    EGG::Vector3f right = dynamics()->mainRot().rotateVector(EGG::Vector3f::ex);
    std::array<f32, 2> tangents = {0.0f, 0.0f};

    // The loop is just to do left/right wall
    for (size_t i = 0; i < tangents.size(); ++i) {
        f32 sign = i == 1 ? -1.0f : 1.0f;
        f32 effectiveRadius = sign * hitbox.radius();
        EGG::Vector3f effectivePos = hitbox.worldPos() + effectiveRadius * right;
        Field::CollisionInfoPartial tempColInfo;

        if (Field::CollisionDirector::Instance()->checkSphereCachedPartial(hitbox.radius(),
                    effectivePos, hitbox.lastPos(), KCL_TYPE_DRIVER_WALL, &tempColInfo, nullptr,
                    0)) {
            tangents[i] = colInfo->tangentOff.squaredLength();
        }
    }

    if (tangents[0] > tangents[1]) {
        collisionData.bWallAtLeftCloser = true;
        collisionData.colPerpendicularity = colInfo->perpendicularity;
    } else if (tangents[1] > tangents[0]) {
        collisionData.bWallAtRightCloser = true;
        collisionData.colPerpendicularity = colInfo->perpendicularity;
    }
}

/// @addr{0x8056E70C}
void KartCollide::calcBoundingRadius() {
    m_boundingRadius = collisionGroup()->boundingRadius() * move()->hitboxScale();
}

/// @addr{0x80571F10}
void KartCollide::calcObjectCollision() {
    constexpr f32 COS_PI_OVER_4 = 0.707f;
    constexpr s32 DUMMY_POLE_ANG_VEL_TIME = 3;
    constexpr f32 DUMMY_POLE_ANG_VEL = 0.005f;
    constexpr s32 SHRINK_TIME = 60;

    m_totalReactionWallNrm = EGG::Vector3f::zero;
    m_surfaceFlags.resetBit(eSurfaceFlags::ObjectWall, eSurfaceFlags::ObjectWall3);

    auto *objColKart = objectCollisionKart();
    size_t collisionCount = objColKart->checkCollision(pose(), velocity());

    const auto *objectDirector = Field::ObjectDirector::Instance();

    for (size_t i = 0; i < collisionCount; ++i) {
        Reaction reaction = objectDirector->reaction(i);
        if (reaction != Reaction::None && reaction != Reaction::UNK_7) {
            // Preserve this racer's contact before ObjectDirector's shared
            // result array is reused by the next kart. The presentation reads
            // it with the existing per-step cue snapshot.
            const auto *contactObject = objectDirector->collidingObject(i);
            // The mole trail/hole clears hitDepth and uses reaction24 (soil).
            // Its retail callback (USA806b4cdc/806b510c) owns the tyre dust;
            // it does not request the generic solid-object impact flash.
            const bool moleSoil = reaction == Reaction::Wall &&
                    (!std::strcmp(contactObject->getName(), "choropu_ground") ||
                     !std::strcmp(contactObject->getName(), "holl"));
            if (!moleSoil) {
                const auto direction = objColKart->GetHitDirection(static_cast<u16>(i));
                move()->cueObjectContact(pos() - direction * m_boundingRadius);
            }
            size_t handlerIdx = static_cast<std::underlying_type_t<Reaction>>(reaction);
            Action newAction = (this->*s_objectCollisionHandlers[handlerIdx])(i);

            if (reaction == Reaction::SpinShrink && (m_shrinkTimer == 0)) {
                m_shrinkTimer = SHRINK_TIME;
                if (move()->applyLightningEffect(300, true, false)) move()->recordObjectDamage();
                move()->applyForce(30.0f, objColKart->GetHitDirection(i), false);
            } else if (reaction != Reaction::SmallBump && reaction != Reaction::BigBump) {
                const EGG::Vector3f &hitDepth = objectDirector->hitDepth(i);
                m_tangentOff += hitDepth;
                m_movement += hitDepth;

                if (newAction != Action::None) {
                    action()->setHitDepth(objectDirector->hitDepth(i));
                    // Retail counts only accepted damage (USA 8056d458), not
                    // repeated overlaps while invulnerable or an action is locked.
                    if (action()->start(newAction)) move()->recordObjectDamage();
                }
            }
        }

        if (objectDirector->collidingObject(i)->id() == Field::ObjectId::DummyPole) {
            EGG::Vector3f hitDirection = objectCollisionKart()->GetHitDirection(i);
            const EGG::Vector3f &lastDir = move()->lastDir();

            if (lastDir.dot(hitDirection) < -COS_PI_OVER_4) {
                EGG::Vector3f angVel = hitDirection.cross(lastDir);
                f32 sign = angVel.y > 0.0f ? -1.0f : 1.0f;

                m_poleAngVelTimer = DUMMY_POLE_ANG_VEL_TIME;
                m_poleYaw = DUMMY_POLE_ANG_VEL * sign;
            }
        }
    }

    calcPoleTimer();
}

/// @addr{Inlined in 0x80571F10}
void KartCollide::calcPoleTimer() {
    if (m_poleAngVelTimer > 0 && status().onBit(eStatus::Accelerate, eStatus::Brake)) {
        EGG::Vector3f angVel2 = dynamics()->angVel2();
        angVel2.y += m_poleYaw;
        dynamics()->setAngVel2(angVel2);
    }

    m_poleAngVelTimer = std::max(0, m_poleAngVelTimer - 1);
}

/// @stage All
/// @brief Processes moving water and floor collision effects
/// @addr{0x8056E8D4}
void KartCollide::processWheel(CollisionData &collisionData, Hitbox &hitbox,
        Field::CollisionInfo *colInfo, Field::KCLTypeMask *maskOut) {
    processMovingWater(collisionData, maskOut);
    processFloor(collisionData, hitbox, colInfo, maskOut, true);
}

/// @addr{0x8056E764}
void KartCollide::processBody(CollisionData &collisionData, Hitbox &hitbox,
        Field::CollisionInfo *colInfo, Field::KCLTypeMask *maskOut) {
    processMovingWater(collisionData, maskOut);

    bool hasWallCollision = processWall(collisionData, maskOut);

    processFloor(collisionData, hitbox, colInfo, maskOut, false);

    if (hasWallCollision) {
        calcSideCollision(collisionData, hitbox, colInfo);
    }

    processCannon(maskOut);
}

/// @addr{0x8056E930}
void KartCollide::processMovingWater(CollisionData &collisionData, Field::KCLTypeMask *maskOut) {
    if (!(*maskOut & KCL_TYPE_BIT(COL_TYPE_MOVING_WATER))) {
        return;
    }

    auto *colDir = Field::CollisionDirector::Instance();
    if (!colDir->findClosestCollisionEntry(maskOut, KCL_TYPE_BIT(COL_TYPE_MOVING_WATER))) {
        return;
    }

    state()->status().setBit(eStatus::StickyRoad);

    auto *entry = colDir->closestCollisionEntry();
    switch (entry->variant()) {
    case 1:
        collisionData.bMovingWaterMomentum = true;
        collisionData.bMovingWaterStickyRoad = true;
        collisionData.bMovingWaterDisableAccel = true;
        break;
    case 2:
        collisionData.bMovingWaterDecaySpeed = true;
        break;
    case 3:
        collisionData.bMovingWaterDecaySpeed = true;
        collisionData.bMovingWaterDisableAccel = true;
        collisionData.bMovingWaterVertical = true;
        break;
    default:
        collisionData.bMovingWaterMomentum = true;
        break;
    }
}

/// @addr{0x8056F184}
bool KartCollide::processWall(CollisionData &collisionData, Field::KCLTypeMask *maskOut) {
    if (!(*maskOut & KCL_TYPE_DRIVER_WALL_NO_INVISIBLE_WALL2)) {
        return false;
    }

    auto *colDirector = Field::CollisionDirector::Instance();
    if (!colDirector->findClosestCollisionEntry(maskOut, KCL_TYPE_DRIVER_WALL_NO_INVISIBLE_WALL2)) {
        return false;
    }

    if ((*maskOut & KCL_TYPE_DRIVER_WALL_NO_INVISIBLE_WALL) &&
            colDirector->findClosestCollisionEntry(maskOut,
                    KCL_TYPE_DRIVER_WALL_NO_INVISIBLE_WALL)) {
        auto *entry = colDirector->closestCollisionEntry();

        collisionData.closestWallFlags = entry->baseType();
        collisionData.closestWallSettings = entry->variant();

        if (entry->attribute.onBit(Field::CollisionDirector::eCollisionAttribute::Soft)) {
            collisionData.bSoftWall = true;
        }
    }

    return true;
}

/// @stage All
/// @addr{0x8056EA04}
/// @brief Processes the floor triangles' attributes.
/// @param collisionData Stores the resulting speed and handling info
/// @param maskOut Stores the flags from the floor KCL
/// @param wheel Differentiates between body and wheel floor collision (boost panels)
void KartCollide::processFloor(CollisionData &collisionData, Hitbox &hitbox,
        Field::CollisionInfo * /*colInfo*/, Field::KCLTypeMask *maskOut, bool wheel) {
    constexpr Field::KCLTypeMask BOOST_RAMP_MASK = KCL_TYPE_BIT(COL_TYPE_BOOST_RAMP);

    if (collisionData.bSoftWall) {
        ++m_numSoftWallCollisions;
        m_sumHitboxBottomHeightSoftWall += hitbox.worldPos().y - hitbox.radius();
    }

    if (!(*maskOut & KCL_TYPE_FLOOR)) {
        return;
    }

    auto *colDirector = Field::CollisionDirector::Instance();

    if (!colDirector->findClosestCollisionEntry(maskOut, KCL_TYPE_FLOOR)) {
        return;
    }

    const auto *closestColEntry = colDirector->closestCollisionEntry();

    if (closestColEntry->attribute.offBit(
                Field::CollisionDirector::eCollisionAttribute::Trickable)) {
        m_surfaceFlags.setBit(eSurfaceFlags::NotTrickable);
    } else {
        collisionData.bTrickable = true;
        m_surfaceFlags.setBit(eSurfaceFlags::Trickable);
    }

    collisionData.speedFactor = std::min(collisionData.speedFactor,
            param()->stats().kclSpeed[closestColEntry->baseType()]);

    collisionData.intensity = closestColEntry->intensity();
    collisionData.rotFactor += param()->stats().kclRot[closestColEntry->baseType()];

    auto &status = KartObjectProxy::status();

    if (closestColEntry->attribute.onBit(
                Field::CollisionDirector::eCollisionAttribute::RejectRoad)) {
        status.setBit(eStatus::RejectRoad);
    }

    // Retail processFloor, USA 80569d34: all touched types 2, 3 and 4,
    // not only the closest triangle or the vehicle's speed multiplier.
    if (*maskOut & 0x1cu) m_surfaceFlags.setBit(eSurfaceFlags::Offroad);

    collisionData.closestFloorFlags = closestColEntry->typeMask;
    collisionData.closestFloorSettings = closestColEntry->variant();

    if (wheel && !!(*maskOut & KCL_TYPE_BIT(COL_TYPE_BOOST_PAD))) {
        move()->padType().setBit(KartMove::ePadType::BoostPanel);
    }

    if (!!(*maskOut & BOOST_RAMP_MASK) &&
            colDirector->findClosestCollisionEntry(maskOut, BOOST_RAMP_MASK)) {
        closestColEntry = colDirector->closestCollisionEntry();
        move()->padType().setBit(KartMove::ePadType::BoostRamp);
        state()->setBoostRampType(closestColEntry->variant());
        m_surfaceFlags.setBit(eSurfaceFlags::BoostRamp, eSurfaceFlags::Trickable);
    } else {
        state()->setBoostRampType(-1);
        m_surfaceFlags.setBit(eSurfaceFlags::NotTrickable);
    }

    if (!collisionData.bSoftWall) {
        ++m_numFloorOnlyCollisions;
        m_sumHitboxBottomHeightFloorOnly += hitbox.worldPos().y - hitbox.radius();
    }

    if (*maskOut & KCL_TYPE_BIT(COL_TYPE_STICKY_ROAD)) {
        status.setBit(eStatus::StickyRoad);
    }

    Field::KCLTypeMask halfPipeRampMask = KCL_TYPE_BIT(COL_TYPE_HALFPIPE_RAMP);
    if ((*maskOut & halfPipeRampMask) &&
            colDirector->findClosestCollisionEntry(maskOut, halfPipeRampMask)) {
        status.setBit(eStatus::HalfPipeRamp);
        state()->setHalfPipeInvisibilityTimer(2);
        if (colDirector->closestCollisionEntry()->variant() == 1) {
            move()->padType().setBit(KartMove::ePadType::BoostPanel);
        }
    }

    Field::KCLTypeMask jumpPadMask = KCL_TYPE_BIT(COL_TYPE_JUMP_PAD);
    if (*maskOut & jumpPadMask && colDirector->findClosestCollisionEntry(maskOut, jumpPadMask)) {
        if (status.offAnyBit(eStatus::TouchingGround, eStatus::JumpPad) &&
                status.offBit(eStatus::JumpPadMushroomVelYInc)) {
            move()->padType().setBit(KartMove::ePadType::JumpPad);
            closestColEntry = colDirector->closestCollisionEntry();
            state()->setJumpPadVariant(closestColEntry->variant());
        }
        collisionData.bTrickable = true;
    }
}

/// @addr{0x8056F490}
/// @brief Checks if we are colliding with a cannon trigger and sets the state flag if so.
void KartCollide::processCannon(Field::KCLTypeMask *maskOut) {
    auto *colDirector = Field::CollisionDirector::Instance();
    if (colDirector->findClosestCollisionEntry(maskOut, KCL_TYPE_BIT(COL_TYPE_CANNON_TRIGGER))) {
        state()->setCannonPointId(colDirector->closestCollisionEntry()->variant());
        status().setBit(eStatus::CannonStart);
    }
}

/// @stage All
/// @brief Applies external and angular velocity based on the collision with the floor
/// @addr{0x805B7928}
/// @param down Always 0.1f
/// @param rate Downward velocity? Related to suspension stiffness
/// @param hitboxGroup Used to retrieve CollisionData reference
/// @param forward Current world facing direction of the kart
/// @param nextDir Updated facing direction of the kart
/// @param speed Tire speed
void KartCollide::applySomeFloorMoment(f32 down, f32 rate, CollisionGroup *hitboxGroup,
        const EGG::Vector3f &forward, const EGG::Vector3f &nextDir, const EGG::Vector3f &speed,
        bool b1, bool b2, bool b3) {
    CollisionData &colData = hitboxGroup->collisionData();
    if (!colData.bFloor) {
        return;
    }

    f32 velDotFloorNrm = colData.vel.dot(colData.floorNrm);

    if (velDotFloorNrm >= 0.0f) {
        return;
    }

    const EGG::Quatf &mainRot = dynamics()->mainRot();
    const EGG::Matrix34f &invInertia = dynamics()->invInertiaTensor();
    if (!m_floorMomentValid ||
            std::memcmp(&m_floorMomentRot, &mainRot, sizeof(EGG::Quatf)) != 0 ||
            std::memcmp(&m_floorMomentInertia, &invInertia, sizeof(EGG::Matrix34f)) != 0) {
        EGG::Matrix34f rotMat;
        rotMat.makeQ(mainRot);
        EGG::Matrix34f product = rotMat.multiplyTo(invInertia);
        EGG::Matrix34f rotMatTrans = rotMat.transpose();
        m_floorMomentTensor = product.multiplyTo(rotMatTrans);
        m_floorMomentRot = mainRot;
        m_floorMomentInertia = invInertia;
        m_floorMomentValid = true;
    }
    const EGG::Matrix34f &tmp = m_floorMomentTensor;

    EGG::Vector3f crossVec = colData.relPos.cross(colData.floorNrm);
    crossVec = tmp.multVector(crossVec);
    crossVec = crossVec.cross(colData.relPos);

    f32 scalar = -velDotFloorNrm / (1.0f + colData.floorNrm.dot(crossVec));
    EGG::Vector3f negSpeed = -speed;
    crossVec = colData.floorNrm.cross(negSpeed);
    crossVec = crossVec.cross(colData.floorNrm);

    if (std::numeric_limits<f32>::epsilon() >= crossVec.squaredLength()) {
        return;
    }

    crossVec.normalise();
    f32 speedDot = std::min(0.0f, speed.dot(crossVec));
    crossVec *= ((scalar * speedDot) / velDotFloorNrm);

    auto [proj, rej] = crossVec.projAndRej(forward);

    f32 projNorm = proj.length();
    f32 rejNorm = rej.length();
    f32 projNorm_ = projNorm;
    f32 rejNorm_ = rejNorm;

    f32 dVar7 = down * EGG::Mathf::abs(scalar);
    if (dVar7 < EGG::Mathf::abs(projNorm)) {
        projNorm_ = dVar7;
        if (projNorm < 0.0f) {
            projNorm_ = -down * EGG::Mathf::abs(scalar);
        }
    }

    f32 dVar5 = rate * EGG::Mathf::abs(scalar);
    if (EGG::Mathf::abs(rejNorm) > dVar5) {
        rejNorm_ = dVar5;
        if (rejNorm < 0.0f) {
            rejNorm_ = -rate * EGG::Mathf::abs(scalar);
        }
    }

    proj.normalise();
    rej.normalise();

    proj *= projNorm_;
    rej *= rejNorm_;

    EGG::Vector3f projRejSum = proj + rej;
    EGG::Vector3f projRejSumOrig = projRejSum;

    if (!b1) {
        projRejSum.x = 0.0f;
        projRejSum.z = 0.0f;
    }
    if (!b2) {
        projRejSum.y = 0.0f;
    }

    projRejSum = projRejSum.rej(nextDir);

    dynamics()->setExtVel(dynamics()->extVel() + projRejSum);

    if (b3) {
        EGG::Vector3f rotation = colData.relPos.cross(projRejSumOrig);
        EGG::Vector3f rotation2 = dynamics()->mainRot().rotateVectorInv(tmp.multVector(rotation));

        EGG::Vector3f angVel = rotation2;
        angVel.y = 0.0f;
        if (!b1) {
            angVel.x = 0.0f;
        }
        dynamics()->setAngVel0(dynamics()->angVel0() + angVel);
    }
}

/// @stage All
/// @brief Called on collision of a new KCL type??? This only happens after airtime so far.
/// @addr{0x805B6A9C}
/// @rename
bool KartCollide::FUN_805B6A9C(CollisionData &collisionData, const Hitbox &hitbox,
        EGG::BoundBox3f &minMax, EGG::Vector3f &relPos, s32 &count,
        const Field::KCLTypeMask &maskOut, const Field::CollisionInfo &colInfo) {
    if (maskOut & KCL_TYPE_WALL) {
        if (!(maskOut & KCL_TYPE_FLOOR) && status().onBit(eStatus::HWG) &&
                state()->softWallSpeed().dot(colInfo.wallNrm) < 0.3f) {
            return true;
        }

        bool skipWalls = false;

        collisionData.wallNrm += colInfo.wallNrm;

        if (maskOut & KCL_TYPE_ANY_INVISIBLE_WALL) {
            collisionData.bInvisibleWall = true;

            if (!(maskOut & KCL_TYPE_4010D000)) {
                collisionData.bInvisibleWallOnly = true;

                if (maskOut & KCL_TYPE_BIT(COL_TYPE_HALFPIPE_INVISIBLE_WALL)) {
                    skipWalls = true;
                }
            }
        }

        if (!skipWalls) {
            if (maskOut & KCL_TYPE_BIT(COL_TYPE_WALL_2)) {
                collisionData.bWall3 = true;
            } else {
                collisionData.bWall = true;
            }
        }
    }

    if (maskOut & KCL_TYPE_FLOOR) {
        collisionData.floorNrm += colInfo.floorNrm;
        collisionData.bFloor = true;
    }

    EGG::Vector3f tangentOff = colInfo.tangentOff;
    minMax.min = minMax.min.minimize(tangentOff);
    minMax.max = minMax.max.maximize(tangentOff);
    tangentOff.normalise();

    relPos += hitbox.relPos();
    relPos += -hitbox.radius() * tangentOff;
    ++count;

    return false;
}

/// @stage 2
/// @brief Saves collision info when vehicle body collision occurs.
/// @details Additionally may apply a change in position for certain KCL types.
/// @addr{0x805B6D48}
void KartCollide::applyBodyCollision(CollisionData &collisionData, const EGG::Vector3f &movement,
        const EGG::Vector3f &posRel, s32 count) {
    setPos(pos() + movement);

    if (!collisionData.bFloor && (collisionData.bWall || collisionData.bWall3)) {
        collisionData.movement = movement;
    }

    f32 rotFactor = 1.0f / static_cast<f32>(count);
    EGG::Vector3f scaledRelPos = rotFactor * posRel;
    collisionData.rotFactor *= rotFactor;

    EGG::Vector3f scaledAngVel0 = dynamics()->angVel0Factor() * dynamics()->angVel0();
    EGG::Vector3f local_48 = mainRot().rotateVectorInv(scaledRelPos);
    EGG::Vector3f local_30 = scaledAngVel0.cross(local_48);
    local_30 = mainRot().rotateVector(local_30);
    local_30 += extVel();

    collisionData.vel = local_30;
    collisionData.relPos = scaledRelPos;

    if (collisionData.bFloor) {
        f32 intVelY = dynamics()->intVel().y;
        if (intVelY > 0.0f) {
            collisionData.vel.y += intVelY;
        }
        collisionData.floorNrm.normalise();
    }
}

/// @addr{0x805713D8}
void KartCollide::startFloorMomentRate() {
    m_floorMomentRate = 0.01f;
}

/// @addr{0x805713FC}
void KartCollide::calcFloorMomentRate() {
    m_floorMomentRate = status().onBit(eStatus::InAction) &&
                    action()->flags().onBit(KartAction::eFlags::Rotating) ?
            0.01f :
            std::min(m_floorMomentRate + 0.01f, 0.8f);
}

/// @addr{0x8056E564}
Action KartCollide::handleReactNone(size_t /*idx*/) {
    return Action::None;
}

/// @addr{0x8057363C}
Action KartCollide::handleReactWallAllSpeed(size_t idx) {
    m_totalReactionWallNrm += Field::ObjectCollisionKart::GetHitDirection(idx);
    m_surfaceFlags.setBit(eSurfaceFlags::ObjectWall);

    return Action::None;
}

/// @addr{0x805733CC}
Action KartCollide::handleReactSpinAllSpeed(size_t /*idx*/) {
    return Action::UNK_0;
}

/// @addr{0x805733D4}
Action KartCollide::handleReactSpinSomeSpeed(size_t /*idx*/) {
    return Action::UNK_1;
}

/// @addr{0x805735AC}
Action KartCollide::handleReactFireSpin(size_t /*idx*/) {
    return Action::UNK_9;
}

/// @addr{0x805733C4}
Action KartCollide::handleReactSmallLaunch(size_t /*idx*/) {
    return Action::UNK_2;
}

/// @addr{0x805733DC}
Action KartCollide::handleReactKnockbackSomeSpeedLoseItem(size_t /*idx*/) {
    return Action::UNK_3;
}

/// @addr{0x8057353C}
Action KartCollide::handleReactLaunchSpinLoseItem(size_t /*idx*/) {
    return Action::UNK_6;
}

/// @addr{0x805733EC}
Action KartCollide::handleReactKnockbackBumpLoseItem(size_t /*idx*/) {
    return Action::UNK_4;
}

/// @addr{0x805735B4}
Action KartCollide::handleReactLongCrushLoseItem(size_t /*idx*/) {
    return Action::UNK_12;
}

/// @addr{0x805737B8}
Action KartCollide::handleReactSmallBump(size_t idx) {
    move()->applyForce(30.0f, objectCollisionKart()->GetHitDirection(idx), false);
    return Action::None;
}

/// @addr{0x805735BC}
Action KartCollide::handleReactSpinShrink(size_t /*idx*/) {
    return m_shrinkTimer <= 0 ? Action::UNK_15 : Action::None;
}

/// @addr{0x805733E4}
Action KartCollide::handleReactHighLaunchLoseItem(size_t /*idx*/) {
    return Action::UNK_8;
}

/// @addr{0x80573754}
Action KartCollide::handleReactWeakWall(size_t /*idx*/) {
    move()->setSpeed(move()->speed() * 0.82f);
    return Action::None;
}

/// @addr{0x80573790}
Action KartCollide::handleReactOffroad(size_t /*idx*/) {
    status().setBit(eStatus::CollidingOffroad);
    m_surfaceFlags.setBit(eSurfaceFlags::Offroad);
    return Action::None;
}

/// @addr{0x805733F4}
Action KartCollide::handleReactLaunchSpin(size_t idx) {
    action()->setTranslation(objectCollisionKart()->translation(idx));
    return Action::UNK_5;
}

/// @addr{0x805736C8}
Action KartCollide::handleReactWallSpark(size_t idx) {
    m_totalReactionWallNrm += Field::ObjectCollisionKart::GetHitDirection(idx);
    m_surfaceFlags.setBit(eSurfaceFlags::ObjectWall3);

    return Action::None;
}

/// @addr{0x80573A2C}
Action KartCollide::handleReactRubberWall(size_t idx) {
    constexpr f32 BASE_DIR_FORCE_SCALAR = 0.95f;
    constexpr f32 DIR_FORCE_SCALAR = 0.050000012f;
    constexpr f32 MAX_FORCE = 70.0f;

    const EGG::Vector3f &hitDir = Field::ObjectCollisionKart::GetHitDirection(idx);
    const EGG::Vector3f &zAxis = componentZAxis();

    f32 force = BASE_DIR_FORCE_SCALAR + DIR_FORCE_SCALAR * EGG::Mathf::abs(hitDir.dot(zAxis));
    move()->applyForce(MAX_FORCE * force, hitDir, true);

    return Action::None;
}

/// @addr{0x805735EC}
Action KartCollide::handleReactUntrickableJumpPad(size_t /*idx*/) {
    move()->setPadType(KartMove::PadType(KartMove::ePadType::JumpPad));
    state()->setJumpPadVariant(0);

    return Action::None;
}

/// @addr{0x805735D4}
Action KartCollide::handleReactShortCrushLoseItem(size_t /*idx*/) {
    return Action::UNK_14;
}

/// @addr{0x805735DC}
Action KartCollide::handleReactCrushRespawn(size_t /*idx*/) {
    return Action::UNK_16;
}

/// @addr{0x805735E4}
Action KartCollide::handleReactExplosionLoseItem(size_t /*idx*/) {
    return Action::UNK_7;
}

std::array<KartCollide::ObjectCollisionHandler, 33> KartCollide::s_objectCollisionHandlers = {{
        &KartCollide::handleReactNone,
        &KartCollide::handleReactNone,
        &KartCollide::handleReactNone,
        &KartCollide::handleReactNone,
        &KartCollide::handleReactNone,
        &KartCollide::handleReactNone,
        &KartCollide::handleReactNone,
        &KartCollide::handleReactNone,
        &KartCollide::handleReactWallAllSpeed,
        &KartCollide::handleReactSpinAllSpeed,
        &KartCollide::handleReactSpinSomeSpeed,
        &KartCollide::handleReactFireSpin,
        &KartCollide::handleReactNone,
        &KartCollide::handleReactSmallLaunch,
        &KartCollide::handleReactKnockbackSomeSpeedLoseItem,
        &KartCollide::handleReactLaunchSpinLoseItem,
        &KartCollide::handleReactKnockbackBumpLoseItem,
        &KartCollide::handleReactLongCrushLoseItem,
        &KartCollide::handleReactSmallBump,
        &KartCollide::handleReactNone,
        &KartCollide::handleReactSpinShrink,
        &KartCollide::handleReactHighLaunchLoseItem,
        &KartCollide::handleReactNone,
        &KartCollide::handleReactWeakWall,
        &KartCollide::handleReactOffroad,
        &KartCollide::handleReactLaunchSpin,
        &KartCollide::handleReactWallSpark,
        &KartCollide::handleReactRubberWall,
        &KartCollide::handleReactNone,
        &KartCollide::handleReactUntrickableJumpPad,
        &KartCollide::handleReactShortCrushLoseItem,
        &KartCollide::handleReactCrushRespawn,
        &KartCollide::handleReactExplosionLoseItem,
}};

} // namespace Kinoko::Kart
