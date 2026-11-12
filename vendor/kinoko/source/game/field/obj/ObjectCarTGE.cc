#include "ObjectCarTGE.hh"

#include "game/field/ObjectCollisionCylinder.hh"
#include "game/field/ObjectDirector.hh"
#include "game/field/RailManager.hh"
#include "game/field/obj/ObjectHighwayManager.hh"

#include "game/kart/KartObject.hh"
#include "game/kart/KartParam.hh"
#include "game/kart/KartAction.hh"
#include "game/render/KartCamera.hh"

#include "game/system/RaceConfig.hh"

#include <limits>

namespace Kinoko::Field {

// Bomb cars (retail USA 0x806C9118 / 0x806C9D1C / 0x806C9FA8).
static constexpr f32 BOMB_ITEM_RADIUS_MULTIPLIER = 1.0f; ///< USA808BF46C
static constexpr f32 BOMB_BLAST_RADIUS = 1250.0f;  ///< 0x808BF464
static constexpr u32 BOMB_BLAST_SPIN_FRAME = 32;    ///< +0x162: later blast hits only spin
static constexpr u32 BOMB_BLAST_END_FRAME = 46;     ///< +0x164
static constexpr f32 BOMB_REGROW_DELAY = 300.0f;    ///< 0x808BF45C
static constexpr f32 BOMB_REGROW_SETTLE = 5.0f;     ///< 0x808BF460
static constexpr f32 BOMB_REGROW_STRETCH = 10.0f;   ///< 0x808BF454

/// @brief Peak height of a regrowing bomb car (0x808BF450). Retail's constructor stores 1.5 times
/// the constructed car's scale for every car, so the last car built sets it.
static f32 s_bombRegrowPeak = 2.5f;

/// @addr{0x806D5EE4}
ObjectCarTGE::ObjectCarTGE(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), StateManager(this, STATE_ENTRIES), m_auxCollision(nullptr),
      m_carName{}, m_mdlName{}, m_carType(CarType::Normal), m_dummyId(ObjectId::None),
      m_scaledTangentDir(EGG::Vector3f::zero), m_currSpeed(0.0f), m_up(EGG::Vector3f::zero),
      m_tangent(EGG::Vector3f::zero), m_squashed{} {
    u32 carVariant = static_cast<u32>(params.setting(3));
    m_highwayVel = static_cast<f32>(params.setting(2));
    m_localVel = static_cast<f32>(params.setting(1));

    s16 pathId = params.pathId();

    // The base game returns before the StateManager sets its state entries.
    // Since we handle this in the template specialization's constructor in Kinoko,
    // we need to reset the StateManager parameters back to their default values before returning.
    if (pathId == -1) {
        m_nextStateId = -1;
        m_currentStateId = 0;
        m_currentFrame = 0;

        return;
    }

    // The base game preemptively calculates collision for all points along the rail.
    RailManager::Instance()->rail(pathId)->checkSphereFull();

    const auto *name = getName();

    if (strcmp(name, "car_body") == 0) {
        snprintf(m_carName, sizeof(m_carName), "%s", "K_car_body");
        m_carType = CarType::Normal;
    } else if (strcmp(name, "kart_truck") == 0) {
        snprintf(m_carName, sizeof(m_carName), "%s", "K_truck");
        m_carType = CarType::Truck;
    } else if (strcmp(name, "K_bomb_car") == 0 || strcmp(name, "K_bomb_car1") == 0) {
        snprintf(m_carName, sizeof(m_carName), "%s", name);
        m_carType = CarType::BombCar;
    }

    const auto *resourceName = getResources();

    if (m_carType == CarType::BombCar) {
        snprintf(m_mdlName, sizeof(m_mdlName), "%s", resourceName);
    } else switch (carVariant) {
    case 0: {
        if (strcmp(resourceName, "K_truck") == 0) {
            snprintf(m_mdlName, sizeof(m_mdlName), "%s_b", "K_truck");
        } else if (strcmp(resourceName, "K_car_body") == 0) {
            snprintf(m_mdlName, sizeof(m_mdlName), "%s_b", "K_car");
        }
    } break;
    case 1: {
        if (strcmp(resourceName, "K_truck") == 0) {
            snprintf(m_mdlName, sizeof(m_mdlName), "%s_o", "K_truck");
        } else if (strcmp(resourceName, "K_car_body") == 0) {
            snprintf(m_mdlName, sizeof(m_mdlName), "%s_r", "K_car");
        }
    } break;
    case 2: {
        if (strcmp(resourceName, "K_truck") == 0) {
            snprintf(m_mdlName, sizeof(m_mdlName), "%s_g", "K_truck");
        } else if (strcmp(resourceName, "K_car_body") == 0) {
            snprintf(m_mdlName, sizeof(m_mdlName), "%s_y", "K_car");
        }
    } break;
    default: {
        PANIC("Bomb cars are not implemented!");
        break;
    }
    }

    switch (m_carType) {
    case CarType::Normal:
        m_dummyId = ObjectDirector::Instance()->flowTable().getIdFromName("car_body_dummy");
        break;
    case CarType::Truck:
        m_dummyId = ObjectDirector::Instance()->flowTable().getIdFromName("kart_truck_dummy");
        break;
    case CarType::BombCar:
        m_dummyId = ObjectDirector::Instance()->flowTable().getIdFromName("K_bomb_car_dummy");
        break;
    }

    m_baseScale = scale().x;
    s_bombRegrowPeak = 1.5f * scale().x;

    if (System::RaceConfig::Instance()->raceScenario().course == Course::Moonview_Highway) {
        registerManagedObject();
    }
}

/// @addr{0x806D691C}
ObjectCarTGE::~ObjectCarTGE() {
    EGG::egg_delete(m_auxCollision);
}

/// @addr{0x806D6B14}
void ObjectCarTGE::init() {
    constexpr f32 HIT_ANGLE_TRUCK = 20.0f;
    constexpr f32 HIT_ANGLE_NORMAL = 40.0f;

    ASSERT(m_mapObj);
    if (m_mapObj->pathId() == -1) {
        return;
    }

    u16 idx = m_mapObj->setting(0);
    m_railInterpolator->init(0.0f, idx);

    auto *rail = RailManager::Instance()->rail(m_mapObj->pathId());
    u16 speedSetting = rail->points()[idx].setting[1];
    if (speedSetting == 1) {
        m_railInterpolator->setCurrVel(m_highwayVel);
    } else if (speedSetting == 0) {
        m_railInterpolator->setCurrVel(m_localVel);
    }

    u16 curPointSpeedSetting = m_railInterpolator->curPoint().setting[1];
    u16 nextPointSpeedSetting = m_railInterpolator->nextPoint().setting[1];

    if (curPointSpeedSetting == 0 && nextPointSpeedSetting == 1) {
        m_nextStateId = 1;
    } else if (curPointSpeedSetting == 1 && nextPointSpeedSetting == 0) {
        m_nextStateId = 2;
    }

    m_squashed.fill(false);
    setPos(m_railInterpolator->curPos());
    m_currSpeed = m_railInterpolator->speed();
    m_scaledTangentDir = m_railInterpolator->curTangentDir() * m_currSpeed;
    m_hitAngle = (m_carType == CarType::Truck) ? HIT_ANGLE_TRUCK : HIT_ANGLE_NORMAL;

    m_hitLanded = m_hitRecovered = m_hitReverse = m_hitByBomb = false;
    m_exploded = false;
    m_blastActive = false;
    m_shown = true;
    m_explodeFrame = 0;
    m_regrownFrame = 0;
    m_regrowHeight = 0.0f;
    m_regrowWidth = 0.0f;
    m_blastRadius = BOMB_BLAST_RADIUS;
    m_blastPhase = 0;
}

/// @addr{0x806D6ECC}
void ObjectCarTGE::calc() {
    StateManager::calc();

    if (m_railInterpolator->calc() == RailInterpolator::Status::SegmentEnd) {
        u16 curPointSpeedSetting = m_railInterpolator->curPoint().setting[1];
        u16 nextPointSpeedSetting = m_railInterpolator->nextPoint().setting[1];

        if (curPointSpeedSetting == 0 && nextPointSpeedSetting == 1) {
            m_nextStateId = 1;
        } else if (curPointSpeedSetting == 1 && nextPointSpeedSetting == 0) {
            m_nextStateId = 2;
        }
    }

    if (m_carType == CarType::BombCar) {
        // USA 0x806C808C: the rail runs on while the car is blown up.
        if (!m_exploded) {
            calcBombPos();
            if (m_shown) {
                cueSound(0x20e, SoundCall::Hold, m_currSpeed);
            }
        } else {
            calcExploded();
        }
    } else if (m_exploded) {
        calcHit();
    } else {
        calcPos();

        if (m_carType == CarType::Normal) {
            cueSound(0x1fc, SoundCall::Hold, m_currSpeed); // SE_CC_TGE_CAR_N_MV
        } else if (m_carType == CarType::Truck) {
            cueSound(0x206, SoundCall::Hold, m_currSpeed); // SE_CC_TGE_CAR_T_MV
        }
    }

    m_hasAuxCollision = false;
}

/// @addr{0x806D7AF8}
void ObjectCarTGE::createCollision() {
    constexpr f32 TRUCK_RADIUS = 190.0f;
    constexpr f32 TRUCK_HEIGHT = 500.0f;
    constexpr f32 NORMAL_RADIUS = 150.0f;
    constexpr f32 NORMAL_HEIGHT = 200.0f;

    ObjectCollidable::createCollision();

    // Bomb cars keep the flow table's sphere alone (USA 0x806C8CB8).
    if (m_carType == CarType::BombCar) {
        return;
    }

    bool isTruck = (m_carType == CarType::Truck);

    f32 radius = isTruck ? TRUCK_RADIUS : NORMAL_RADIUS;
    f32 height = isTruck ? TRUCK_HEIGHT : NORMAL_HEIGHT;

    m_auxCollision = EGG::egg_new<ObjectCollisionCylinder>(radius, height, collisionCenter());
}

/// @addr{0x806D7BF8}
void ObjectCarTGE::calcCollisionTransform() {
    auto *col = collision();

    if (!col) {
        return;
    }

    calcTransform();
    col->transform(transform(), scale(), m_scaledTangentDir);
    if (!m_auxCollision) {
        return;
    }
    calcTransform();
    EGG::Matrix34f mat;
    SetRotTangentHorizontal(mat, transform().base(2), EGG::Vector3f::ey);
    calcTransform();
    mat.setBase(3, transform().base(3));
    m_auxCollision->transform(mat, scale(), m_scaledTangentDir);
}

/// @addr{0x806D69C0}
f32 ObjectCarTGE::getCollisionRadius() const {
    constexpr f32 NORMAL_RADIUS = 600.0f;
    constexpr f32 TRUCK_RADIUS = 1100.0f;
    constexpr f32 BOMB_RADIUS = 400.0f;

    switch (m_carType) {
    case CarType::Truck:
        return TRUCK_RADIUS;
    case CarType::BombCar:
        return BOMB_RADIUS;
    default:
        return NORMAL_RADIUS;
    }
}

/// @addr{0x806D7328}
Kart::Reaction ObjectCarTGE::onCollision(Kart::KartObject *kartObj, Kart::Reaction reactionOnKart,
        Kart::Reaction reactionOnObj, EGG::Vector3f &hitDepth) {
    constexpr u32 SQUASH_INVULNERABILITY = 200;

    if (m_carType == CarType::BombCar) {
        // Retail USA 0x806C84E8: touching the bomb car blows it up (object reaction 5 for
        // every kart, GeoHitTableKartObj); the kart reacts with an explosion (32), and once
        // the blast is past its first 32 frames, with a spin (10).
        hitDepth.setZero();
        if (m_blastPhase == 1 && reactionOnKart == Kart::Reaction::ExplosionLoseItem) {
            reactionOnKart = Kart::Reaction::SpinSomeSpeed;
        }
    } else if (!m_hasAuxCollision) {
        EGG::Vector3f hitDepthNorm = hitDepth;
        hitDepthNorm.normalise2();

        if (hitDepthNorm.y > 0.9f) {
            return Kart::Reaction::UntrickableJumpPad;
        }
    }

    // Retail USA 0x806C84E8 reads and marks the colliding player's own squash state.
    const size_t playerIdx = kartObj->param()->playerIdx();

    if (m_highwayMgr && m_highwayMgr->squashTimer(playerIdx) < SQUASH_INVULNERABILITY) {
        const auto &hitTable = ObjectDirector::Instance()->hitTableKart();
        reactionOnKart = hitTable.reaction(hitTable.slot(static_cast<ObjectId>(m_dummyId)),
                ObjectHitTable::KartColumn(kartObj));
        const auto &objectTable = ObjectDirector::Instance()->hitTableKartObject();
        reactionOnObj = objectTable.reaction(objectTable.slot(m_dummyId),
                ObjectHitTable::KartColumn(kartObj));
    }

    if (reactionOnObj == Kart::Reaction::UNK_3 || reactionOnObj == Kart::Reaction::UNK_5) {
        if (reactionOnKart != Kart::Reaction::None && playerIdx < m_squashed.size())
            m_squashed[playerIdx] = true;
        startHit();
        return reactionOnKart;
    }
    if (reactionOnObj != Kart::Reaction::None) return reactionOnKart;

    // Original ordinary collision branch (reactionOnObj == 0).
    if (reactionOnKart != Kart::Reaction::None && reactionOnKart != Kart::Reaction::WallAllSpeed) {
        if (playerIdx < m_squashed.size()) {
            m_squashed[playerIdx] = true;
        }
        calcTransform();
        EGG::Vector3f v2 = transform().base(2);
        v2.y = 0.0f;
        v2.normalise2();
        EGG::Vector3f posDelta = kartObj->pos() - pos();
        posDelta.y = 0.0f;
        posDelta.normalise2();

        if (v2.dot(posDelta) < EGG::Mathf::CosFIdx(0.7111111f * m_hitAngle) && !m_hasAuxCollision) {
            reactionOnKart = Kart::Reaction::LaunchSpin;
        }

        hitDepth.setZero();
    }

    // USA806c88e8: only the ordinary non-bomb contact requests the camera dip.
    if (m_carType != CarType::BombCar && reactionOnKart != Kart::Reaction::LaunchSpin &&
            reactionOnKart != Kart::Reaction::WallAllSpeed &&
            kartObj->action()->currentAction() != Kart::Action::UNK_5 && playerIdx == 0) {
        if (auto *camera = Render::KartCamera::Instance()) camera->onTrafficContact();
    }
    return reactionOnKart;
}

// Original item context is inspected before entering the shared hit path.
void ObjectCarTGE::processItemReactions(s16 type, bool global, s16 &item, s16 &object) {
    ObjectBase::processItemReactions(type, global, item, object);
    if (!m_exploded && object >= 3 && object <= 6 && !global && type == 9)
        m_hitByBomb = true;
}

s16 ObjectCarTGE::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
        s16 item, s16 object, EGG::Vector3f &) {
    if (object >= 3 && object <= 6) startHit();
    return item;
}

void ObjectCarTGE::onGlobalItemCollision(s16, s16 object) {
    if (object >= 3 && object <= 6) startHit();
}

void ObjectCarTGE::startHit() {
    if (m_exploded) return;
    if (m_carType == CarType::BombCar) {
        explode();
        return;
    }
    m_exploded = true;
    m_hitLanded = m_hitRecovered = false;
    m_explodeFrame = 0;
    disableCollision();
    m_hitLandFrame = 2.0f * 100.0f / 3.0f;
    m_hitRotationSpeed = 390.0f / m_hitLandFrame;
    m_hitStartY = pos().y;
}

// USA806c943c: the rail continues during both arcs; recovery depends on contact,
// not on a guessed animation duration.
void ObjectCarTGE::calcHit() {
    const f32 length = (m_carType == CarType::Truck ? 1600.0f : 1500.0f) * scale().z * 0.5f;
    EGG::Vector3f ahead, ignored;
    m_railInterpolator->evalCubicBezierOnPath(length, ahead, ignored);
    EGG::Vector3f dir = m_railInterpolator->curPos() - ahead;
    dir.normalise2();
    m_tangent.x += 0.1f * (dir.x - m_tangent.x);
    m_tangent.z += 0.1f * (dir.z - m_tangent.z);
    m_tangent.y = dir.y;
    m_tangent.normalise2();
    dir = m_tangent;
    EGG::Vector3f up = OrthonormalBasis(dir).base(1);
    const f32 slopeAngle = dir.y > 0.05f ? 20.0f : (dir.y < -0.05f ? 33.0f : 24.0f);
    const f32 frame = static_cast<f32>(m_explodeFrame);
    f32 angle = frame * m_hitRotationSpeed;
    if (m_hitLanded) {
        const f32 rate = (m_hitReverse ? 1.0f : -1.0f) * slopeAngle / (0.3f * m_hitLandFrame);
        angle = m_hitLandFrame * m_hitRotationSpeed - (frame - m_hitLandFrame) * rate;
    }
    EGG::Matrix34f rotation = EGG::Matrix34f::ident;
    rotation.setAxisRotation(angle * DEG2RAD, dir);
    up = rotation.multVector33(up);
    up.normalise2();
    dir.normalise2();
    setPos(m_railInterpolator->curPos() - dir * length * 0.5f);
    const u32 arcFrame = m_hitLanded ? static_cast<u32>(frame - m_hitLandFrame) : m_explodeFrame;
    const f32 t = static_cast<f32>(arcFrame);
    const f32 height = (m_hitLanded ? 30.0f : 100.0f) * t - 0.5f * 3.0f * t * t;
    calcTransform();
    // Retail samples the previous model orientation at local +/-350, zero Y.
    const auto left = transform().multVector33(EGG::Vector3f(350.0f, 0.0f, 0.0f));
    const auto right = transform().multVector33(EGG::Vector3f(-350.0f, 0.0f, 0.0f));
    const f32 lowY = left.y < right.y ? left.y : right.y;
    const bool contact = m_hitStartY + height + lowY < pos().y;
    const bool first = contact && m_explodeFrame > 5 && !m_hitLanded && !m_hitRecovered;
    const bool second = contact && frame > m_hitLandFrame + 5.0f && m_hitLanded && !m_hitRecovered;
    if (first || second) {
        if (first) {
            m_hitLanded = true;
            if (left.y > right.y) m_hitReverse = true;
        } else {
            m_exploded = false;
            enableCollision();
            m_hitRecovered = true;
        }
        m_hitLandFrame = frame;
        setPos(pos() - EGG::Vector3f::ey * lowY);
        m_hitStartY = pos().y;
        if (second) {
            calcTransform();
            m_up = transform().base(1);
        }
        cueSound(m_carType == CarType::Normal ? 0x205 : 0x20d, SoundCall::Start);
    } else {
        auto p = pos(); p.y = m_hitStartY + height; setPos(p);
        m_tangent = dir;
        m_up = up;
        setMatrixTangentTo(m_up, m_tangent);
    }
    ++m_explodeFrame;
}

/// @addr{0x806DA660}
bool ObjectCarTGE::checkCollision(ObjectCollisionBase *lhs, EGG::Vector3f &dist) {
    dist = EGG::Vector3f::zero;
    bool hasCol = lhs->check(*m_collision, dist);

    if (!hasCol && m_auxCollision) {
        hasCol = lhs->check(*m_auxCollision, dist);
        m_hasAuxCollision = hasCol;
    }

    return hasCol;
}

/// @addr{0x806D7CF8}
const EGG::Vector3f &ObjectCarTGE::collisionCenter() const {
    static constexpr EGG::Vector3f CENTER_TRUCK = EGG::Vector3f(0.0f, 300.0f, 0.0f);
    static constexpr EGG::Vector3f CENTER_NORMAL = EGG::Vector3f(0.0f, 100.0f, 0.0f);
    static constexpr EGG::Vector3f CENTER_DEFAULT = EGG::Vector3f(0.0f, 0.0f, 0.0f);

    switch (m_carType) {
    case CarType::Truck:
        return CENTER_TRUCK;
    case CarType::Normal:
        return CENTER_NORMAL;
    default:
        return CENTER_DEFAULT;
    }
}

void ObjectCarTGE::enterStateStub() {}

void ObjectCarTGE::calcStateStub() {}

/// @addr{0x806D7D70}
/// @brief The state when cars are speeding up.
/// @details On Moonview Highway, the speed cap m_highwayVel is 70, which means this state is only
/// executed for 1 frame when cars get on the highway.
void ObjectCarTGE::calcState1() {
    m_currSpeed += TOLL_BOOTH_ACCEL;

    if (m_currSpeed > m_highwayVel) {
        m_currSpeed = m_highwayVel;
        m_nextStateId = 0;
    }

    m_railInterpolator->setCurrVel(m_currSpeed);
    m_scaledTangentDir = m_railInterpolator->curTangentDir() * m_currSpeed;
}

/// @addr{0x806D7E0C}
/// @brief The state when cars are slowing down.
/// @details On Moonview Highway, the speed floor m_localVel is 40, which means this state is only
/// executed for 1 frame when cars get off the highway.
void ObjectCarTGE::calcState2() {
    m_currSpeed -= TOLL_BOOTH_ACCEL;

    if (m_currSpeed < m_localVel) {
        m_currSpeed = m_localVel;
        m_nextStateId = 0;
    }

    m_railInterpolator->setCurrVel(m_currSpeed);
    m_scaledTangentDir = m_railInterpolator->curTangentDir() * m_currSpeed;
}

/// @addr{0x806D9000}
void ObjectCarTGE::calcPos() {
    constexpr f32 NORMAL_SPEED = 1500.0f;
    constexpr f32 TRUCK_SPEED = 1600.0f;

    f32 speed = (m_carType == CarType::Truck) ? TRUCK_SPEED : NORMAL_SPEED;
    f32 t = speed * scale().z * 0.5f;

    EGG::Vector3f curDir;
    EGG::Vector3f curTangentDir;
    m_railInterpolator->evalCubicBezierOnPath(t, curDir, curTangentDir);

    const EGG::Vector3f curPos = m_railInterpolator->curPos();
    EGG::Vector3f posDelta = curPos - curDir;
    posDelta.normalise2();
    m_tangent += 0.1f * (posDelta - m_tangent);
    m_tangent.y = posDelta.y;
    m_tangent.normalise2();

    if (m_tangent.y > -0.05f) {
        setPos(curPos - m_tangent * t * 0.5f);
    } else {
        setPos(curPos - m_tangent * t * 0.5f - EGG::Vector3f::ey * 5.0f);
    }

    m_up = OrthonormalBasis(m_tangent).base(1);
    setMatrixTangentTo(m_up, m_tangent);
}

/// @addr{0x806CA1C0}
/// @brief Bomb cars sit on the rail point, their heading and up vector eased 10% a frame
/// toward the rail tangent and the floor normal between the segment's points.
void ObjectCarTGE::calcBombPos() {
    constexpr f32 EASE = 0.1f;

    const f32 t = m_railInterpolator->segmentT();
    EGG::Vector3f up = m_railInterpolator->floorNrm(m_railInterpolator->curPointIdx()) * (1.0f - t) +
            m_railInterpolator->floorNrm(m_railInterpolator->nextPointIdx()) * t;
    if (up.squaredLength() <= std::numeric_limits<f32>::epsilon()) {
        up = EGG::Vector3f::ey;
    }
    up.normalise2();

    m_tangent += (m_railInterpolator->curTangentDir() - m_tangent) * EASE;
    m_tangent.normalise2();
    setPos(m_railInterpolator->curPos());
    m_up += (up - m_up) * EASE;
    m_up.normalise2();

    setMatrixTangentTo(m_up, m_tangent);
}

/// @addr{0x806C9118}
/// @brief The bomb car blows up: its models hide and its collision grows to the blast radius.
void ObjectCarTGE::explode() {
    if (m_exploded) {
        return;
    }

    m_exploded = true;
    m_blastActive = true;
    m_explodeFrame = 0;
    m_shown = false;
    m_regrownFrame = 0;
    m_regrowHeight = 0.0f;
    m_regrowWidth = 0.0f;
    // USA806c9240/806c92bc: the original bomb-trigger multiplier is 1.0.
    m_blastRadius = BOMB_BLAST_RADIUS * (m_hitByBomb ? BOMB_ITEM_RADIUS_MULTIPLIER : 1.0f);

    const auto &flowTable = ObjectDirector::Instance()->flowTable();
    const f32 flowRadius = static_cast<f32>(
            parse<s16>(flowTable.set(flowTable.slot(id()))->params.sphere.radius));
    setScale(m_blastRadius / flowRadius);
    resize(m_blastRadius, 0.0f);

    cueSound(0x210, SoundCall::Start);
}

/// @addr{0x806C9D1C}
/// @brief While blown up: the blast for 46 frames, then the car rides its rail unseen and
/// intangible until it regrows, 300 frames after the blast.
void ObjectCarTGE::calcExploded() {
    if (static_cast<f32>(m_explodeFrame) >= BOMB_REGROW_DELAY && m_regrownFrame == 0) {
        calcRegrow();
    }

    if (m_regrownFrame != 0 &&
            static_cast<f32>(m_regrownFrame) + BOMB_REGROW_SETTLE <
                    static_cast<f32>(m_explodeFrame)) {
        enableCollision();
        m_exploded = false;
    }

    if (m_explodeFrame < BOMB_BLAST_END_FRAME) {
        m_blastPhase = m_explodeFrame < BOMB_BLAST_SPIN_FRAME ? 0 : 1;
    } else {
        if (m_blastActive) {
            disableCollision();
            setScale(m_baseScale);
            const auto &flowTable = ObjectDirector::Instance()->flowTable();
            resize(static_cast<f32>(parse<s16>(
                           flowTable.set(flowTable.slot(id()))->params.sphere.radius)),
                    0.0f);
        }

        m_blastActive = false;
        m_hitByBomb = false; // Retail USA806c9ef8: clear when the blast ends.
        m_blastPhase = 0;
    }

    if (!m_blastActive) {
        setPos(m_railInterpolator->curPos());
        calcBombPos();
    }

    ++m_explodeFrame;
}

/// @addr{0x806C9FA8}
/// @brief The bomb car pops back: its height swings up to the regrow peak and back while its
/// width grows 0.1 a frame, then it settles at its own scale.
void ObjectCarTGE::calcRegrow() {
    constexpr f32 WIDTH_STEP = 0.1f;

    m_shown = true;

    const f32 frame = static_cast<f32>(m_explodeFrame) - BOMB_REGROW_DELAY;
    const f32 height = s_bombRegrowPeak *
            EGG::Mathf::SinFIdx(
                    RAD2FIDX * ((F_TAU * frame) / (4.0f * BOMB_REGROW_STRETCH)));
    m_regrowHeight = height;

    if (frame <= BOMB_REGROW_STRETCH || m_baseScale < height) {
        m_regrowWidth += WIDTH_STEP;
        if (m_baseScale < m_regrowWidth) {
            m_regrowWidth = m_baseScale;
        }

        setScale(EGG::Vector3f(m_regrowWidth, m_regrowHeight, m_regrowWidth));

        if (frame == 0.0f) {
            cueSound(0x211, SoundCall::Start);
        }
    } else {
        m_regrownFrame = m_explodeFrame;
        setScale(m_baseScale);
    }
}

} // namespace Kinoko::Field
