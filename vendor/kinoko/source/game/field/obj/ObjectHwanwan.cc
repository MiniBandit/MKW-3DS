#include "ObjectHwanwan.hh"

#include "game/field/CollisionDirector.hh"
#include "game/system/RaceConfig.hh"
#include <cmath>

namespace Kinoko::Field {

/// @addr{0x806E95B0}
ObjectHwanwan::ObjectHwanwan(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), StateManager(this, STATE_ENTRIES), m_initPos(pos()) {}

/// @addr{0x806EC6E0}
ObjectHwanwan::~ObjectHwanwan() = default;

/// @addr{0x806E9724}
void ObjectHwanwan::init() {
    m_workPos = m_initPos + EGG::Vector3f::ey * DIAMETER;
    m_extVel.setZero();
    m_bounceVel.setZero();
    m_tangent = EGG::Vector3f::ez;
    m_up = EGG::Vector3f::ey;
    m_targetUp = EGG::Vector3f::ey;
    m_touchingGround = false;
    m_initJumpVel = 0.0f;
    m_targetY = m_workPos.y;
    m_hitPitch = m_hitYaw = 0.0f;
    m_floorSound = 0;

    calcTransform();
    m_modelTransform = transform();

    m_nextStateId = 0;
}

/// @addr{0x806E9A78}
void ObjectHwanwan::calc() {
    StateManager::calc();

    m_extVel += m_bounceVel - GRAVITY;
    m_workPos += m_extVel;
    m_bounceVel.setZero();

    checkFloorCollision();
    calcUp();

    EGG::Matrix34f mat;
    SetRotTangentHorizontal(mat, m_up, m_tangent);
    mat.setBase(3, m_workPos);
    setTransform(mat);
}

Kart::Reaction ObjectHwanwan::onCollision(Kart::KartObject *kart,
        Kart::Reaction onKart, Kart::Reaction onObj, EGG::Vector3f &) {
    const auto reaction = static_cast<s16>(onObj);
    if (reaction == 0 && kart->speedRatioCapped() < 0.5f) {
        return Kart::Reaction::WallAllSpeed;
    }
    if ((reaction == 3 || reaction == 5) && m_currentStateId != 2) {
        m_nextStateId = 2;
    }
    return onKart;
}

s16 ObjectHwanwan::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
        s16 onItem, s16 onObj, EGG::Vector3f &) {
    onGlobalItemCollision(onItem, onObj);
    return onItem;
}

void ObjectHwanwan::onGlobalItemCollision(s16, s16 onObj) {
    if (onObj >= 3 && onObj <= 6 && m_currentStateId != 2) {
        m_nextStateId = 2;
    }
}

static void wrapHwanwanAngle(f32 &angle) {
    if (std::fabs(angle) > 360.0f) {
        angle -= static_cast<s32>(angle) / 360 * 360.0f;
    }
}

void ObjectHwanwan::chomp() {
    m_animationFrame = static_cast<f32>(m_chompFrame++ % 15);
    if (m_animationFrame == 9.0f) cueSound(0x235, SoundCall::Start);
    cueSound(0x234, SoundCall::Hold);
    updateModelTransform();
}

void ObjectHwanwan::calcState0() {
    if (m_touchingGround) m_bounceVel += EGG::Vector3f::ey * 12.0f;
    wrapHwanwanAngle(m_hitPitch);
    if (std::fabs(m_hitPitch) < 10.0f) m_hitPitch = 0.0f;
    else m_hitPitch += 3.0f;
    wrapHwanwanAngle(m_hitYaw);
    if (std::fabs(m_hitYaw) < 2.0f) m_hitYaw = 0.0f;
    m_hitYaw += m_hitYaw > 0.0f ? -0.5f : 0.5f;
    chomp();
}

void ObjectHwanwan::calcState1() {
    m_hitPitch += 6.0f;
    wrapHwanwanAngle(m_hitYaw);
    if (std::fabs(m_hitYaw) < 2.0f) m_hitYaw = 0.0f;
    m_hitYaw += m_hitYaw > 0.0f ? -0.5f : 0.5f;
    m_chompFrame = 9;
    m_animationFrame = 9.0f;
    updateModelTransform();
}

// USA 806ef934: retain pitch from the previous rolling state, reset yaw only.
void ObjectHwanwan::enterState2() {
    m_hitContactFrame = -1;
    m_hitYaw = 0.0f;
    m_initJumpVel = 150.0f;
    m_pitchRate = 30.0f;
    m_pitchAcceleration = 0.4f;
    m_yawRate = 1.0f;
    if (System::RaceConfig::Instance()->raceScenario().course == Course::Mario_Circuit &&
            m_floorSound == 1) {
        m_initJumpVel = 50.0f;
        m_pitchRate = m_pitchAcceleration = m_yawRate = 0.0f;
    }
}

// USA 806ef9a8: these displacements precede the shared gravity/floor update.
void ObjectHwanwan::calcState2() {
    if (m_hitContactFrame != -1 &&
            m_currentFrame > static_cast<u32>(m_hitContactFrame + 10) && m_touchingGround) {
        m_nextStateId = 0;
        chomp();
        return;
    }
    if (m_currentFrame >= 10 && m_touchingGround) m_hitContactFrame = m_currentFrame;
    if (m_hitContactFrame == -1) {
        m_workPos.y += m_initJumpVel - 2.5f * m_currentFrame;
        m_hitYaw += m_yawRate;
        m_hitPitch -= m_pitchRate - m_pitchAcceleration * m_currentFrame;
        m_chompFrame = 7;
    } else {
        m_workPos.y += 0.6f * m_initJumpVel - 2.5f * (m_currentFrame - m_hitContactFrame);
        wrapHwanwanAngle(m_hitPitch);
        m_hitPitch -= 1.5f;
        m_hitYaw -= 2.5f * m_yawRate;
        if (m_currentFrame <= static_cast<u32>(m_hitContactFrame + 16)) m_chompFrame = 9;
    }
    m_animationFrame = static_cast<f32>(m_chompFrame);
    updateModelTransform();
}

void ObjectHwanwan::updateModelTransform() {
    EGG::Matrix34f pitch, yaw, base = transform();
    base.setBase(3, EGG::Vector3f::zero);
    pitch.makeRT({m_hitPitch * DEG2RAD, 0.0f, 0.0f}, EGG::Vector3f::zero);
    yaw.makeRT({0.0f, m_hitYaw * DEG2RAD, 0.0f}, EGG::Vector3f::zero);
    m_modelTransform = base.multiplyTo(pitch.multiplyTo(yaw));
    m_modelTransform.setBase(3, m_workPos);
}

/// @addr{0x806EA784}
void ObjectHwanwan::checkFloorCollision() {
    constexpr f32 RADIUS = DIAMETER * 0.5f;

    m_touchingGround = false;

    CollisionInfo colInfo;
    KCLTypeMask mask;
    EGG::Vector3f pos = m_workPos + EGG::Vector3f(0.0f, -DIAMETER, 0.0f);

    bool hasCol = CollisionDirector::Instance()->checkSphereFullPush(RADIUS, pos,
            EGG::Vector3f::inf, KCL_TYPE_FLOOR, &colInfo, &mask, 0);

    if (!hasCol || pos.y - m_targetY >= 300.0f) {
        return;
    }

    m_touchingGround = true;

    f32 len = colInfo.tangentOff.length();
    m_workPos += EGG::Vector3f::ey * len;

    if (colInfo.floorDist > -std::numeric_limits<f32>::min()) {
        m_targetUp = colInfo.floorNrm;
    }
    m_extVel.y = 0.0f;
    auto *director = CollisionDirector::Instance();
    if (director->findClosestCollisionEntry(&mask, static_cast<KCLTypeMask>(~0u))) {
        m_floorSound = (static_cast<u16>(director->closestCollisionEntry()->attribute) >> 8) & 7;
    }
}

/// @addr{0x806EAAE8}
void ObjectHwanwan::calcUp() {
    m_up = Interpolate(0.1f, m_up, m_targetUp);
    if (m_up.squaredLength() > std::numeric_limits<f32>::epsilon()) {
        m_up.normalise2();
    } else {
        m_up = EGG::Vector3f::ey;
    }
}

/// @addr{0x806C5354}
ObjectHwanwanManager::ObjectHwanwanManager(const System::MapdataGeoObj &params)
    : ObjectCollidable(params) {
    m_hwanwan = EGG::egg_new<ObjectHwanwan>(params);
    m_hwanwan->setScale(2.0f);
    m_hwanwan->load();
}

/// @addr{0x806C56DC}
ObjectHwanwanManager::~ObjectHwanwanManager() = default;

/// @addr{0x806C571C}
void ObjectHwanwanManager::init() {
    m_railInterpolator->init(0.0f, 0);
    m_hwanwan->m_tangent = m_railInterpolator->curTangentDir();
    const auto &curPos = m_railInterpolator->curPos();
    m_hwanwan->m_workPos.x = curPos.x;
    m_hwanwan->m_workPos.z = curPos.z;
    m_hwanwan->m_targetY = curPos.y;

    m_hwanwan->calc();
    m_hwanwan->calc();

    ASSERT(m_mapObj);
    m_railInterpolator->setCurrVel(static_cast<f32>(m_mapObj->setting(0)));
}

/// @addr{0x806C5AC4}
void ObjectHwanwanManager::calc() {
    calcState();

    const auto &curPos = m_railInterpolator->curPos();
    m_hwanwan->m_workPos.x = curPos.x;
    m_hwanwan->m_workPos.z = curPos.z;
    m_hwanwan->m_targetY = curPos.y;
    m_hwanwan->m_tangent = m_railInterpolator->curTangentDir();
}

/// @addr{0x806C5DE0}
void ObjectHwanwanManager::calcState() {
    if (m_railInterpolator->calc() == RailInterpolator::Status::SegmentEnd &&
            m_railInterpolator->curPoint().setting[1] == 1 && m_hwanwan->m_currentStateId != 2) {
        m_hwanwan->m_nextStateId = 1;
    }

    if (m_hwanwan->m_currentStateId == 1 && m_hwanwan->m_currentFrame >= 60) {
        m_hwanwan->m_nextStateId = 0;
    }
}

} // namespace Kinoko::Field
