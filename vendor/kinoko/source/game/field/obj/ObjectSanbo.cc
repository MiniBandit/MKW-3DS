#include "ObjectSanbo.hh"
#include "game/kart/KartObject.hh"
#include "game/kart/KartCollide.hh"
#include "game/system/RaceManager.hh"
#include <mkw/pokey_joint_assets.hpp>
#include <cmath>
#include <algorithm>

#include "game/field/CollisionDirector.hh"
#include "game/field/KColData.hh"

namespace Kinoko::Field {

/// @addr{0x80779F3C}
ObjectSanbo::ObjectSanbo(const System::MapdataGeoObj &params) : ObjectCollidable(params) {
    m_yVel = 0.0f;
}

/// @addr{0x8077A1A8}
ObjectSanbo::~ObjectSanbo() = default;

/// @addr{0x8077A1E8}
void ObjectSanbo::init() {
    m_recoveryTimer = m_reviveTimer = 0;
    m_animationSound = {};
    enableCollision();
    m_flinchTimer = 0;
    m_animationFrame = 0.0f;
    m_up = EGG::Vector3f::ey;
    EGG::Matrix34f rotMat;
    rotMat.makeR(rot());
    m_tangent = rotMat.base(2);
    m_tangent.normalise();
    m_standstill = false;
    m_railInterpolator->init(0.0f, 0);
}

/// @addr{0x8077A36C}
void ObjectSanbo::calc() {
    calcMove();
    ++m_animationFrame;
    // USA80765c04: movement continues along the rail in every state.
    if (m_recoveryTimer > 0) {
        if (--m_recoveryTimer == 0) {
            m_reviveTimer = 86;
            m_animationFrame = 0.0f;
            m_animationSound = {};
            enableCollision();
        } else {
            for (auto &part : m_parts) {
                part.velocity.y -= 2.0f;
                part.position += part.velocity;
                part.angles += part.angularVelocity;
                EGG::Matrix34f rotation;
                rotation.makeR(part.angles);
                part.matrix = part.initial.multiplyTo(rotation);
                part.matrix.setBase(3, part.position);
            }
        }
    } else if (m_reviveTimer > 0) {
        if (--m_reviveTimer == 0) m_animationFrame = 0.0f;
    } else if (m_flinchTimer > 0) {
        if (--m_flinchTimer == 0) m_animationFrame = 0.0f;
    } else {
        cueSound(0x247, SoundCall::Hold);
    }
    if (m_reviveTimer > 0)
        m_animationSound.update(*this, "sanbo", "revive", m_animationFrame);

}

// USA8076605c/8076617c: reactions4/6 play damage for90 ticks.
void ObjectSanbo::flinch(s16 reaction) {
    if ((reaction != 4 && reaction != 6) || m_flinchTimer > 0) return;
    m_reviveTimer = 0;
    m_flinchTimer = 90;
    m_animationFrame = 0.0f;
    cueSound(0x249, SoundCall::Start); // SE_CC_SANBO_COL
}
Kart::Reaction ObjectSanbo::onCollision(Kart::KartObject *kart, Kart::Reaction onKart,
        Kart::Reaction onObject, EGG::Vector3f &depth) {
    if (scattered()) return Kart::Reaction::None;
    const auto reaction = static_cast<s16>(onObject);
    if (reaction == 3 || reaction == 5) scatter(depth, kart->velocity());
    else flinch(reaction);
    return onKart;
}
s16 ObjectSanbo::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &velocity,
        s16 onItem, s16 onObject, EGG::Vector3f &depth) {
    if (scattered()) return 0;
    if (onObject == 3 || onObject == 5) scatter(depth, velocity);
    else flinch(onObject);
    return onItem;
}
void ObjectSanbo::onGlobalItemCollision(s16, s16 onObject) {
    if (scattered()) return;
    if (onObject == 3 || onObject == 5) {
        calcTransform();
        scatter(transform().base(2), -transform().base(2));
    } else flinch(onObject);
}

void ObjectSanbo::scatter(const EGG::Vector3f &depth, const EGG::Vector3f &velocity) {
    if (scattered()) return;
    calcTransform();
    unsigned frame = static_cast<unsigned>(m_animationFrame);
    const float (*points)[3] = reviving() ? mkw::Pokey_revive[std::min(frame,85u)] :
            flinching() ? mkw::Pokey_damage[frame%30] : mkw::Pokey_move[frame%86];
    auto &random = System::RaceManager::Instance()->random();
    EGG::Vector3f direction = velocity - depth;
    direction.y = 0.0f;
    direction.normalise();
    const f32 crossLength = std::sqrt(direction.x * direction.x);
    f32 heading = std::atan2(crossLength, direction.z) * RAD2DEG;
    if (depth.x >= 0.0f) heading = -heading;
    const f32 magnitude = depth.length() + velocity.length();
    const f32 speed = magnitude < 30.0f ? 30.0f : magnitude < 100.0f ? 50.0f : 100.0f;
    for (unsigned i=0;i<4;++i) {
        auto &part=m_parts[i];
        part.initial=transform();
        const EGG::Vector3f local(points[i][0]*scale().x,points[i][1]*scale().y,points[i][2]*scale().z);
        part.position=transform().multVector(local);
        part.matrix=part.initial;
        part.matrix.setBase(3,part.position);
        EGG::Matrix34f yaw,pitch;
        yaw.makeR(EGG::Vector3f(0.0f,(heading-90.0f+random.getF32(180.0f))*DEG2RAD,0.0f));
        pitch.makeR(EGG::Vector3f(-(30.0f+random.getF32(30.0f))*DEG2RAD,0.0f,0.0f));
        part.velocity=yaw.multiplyTo(pitch).multVector33(EGG::Vector3f::ez)*speed;
        part.angles=EGG::Vector3f::zero;
        part.angularVelocity.z=(static_cast<f32>(random.getU32(12))-6.0f)*DEG2RAD;
        part.angularVelocity.y=(static_cast<f32>(random.getU32(12))-6.0f)*DEG2RAD;
        part.angularVelocity.x=(static_cast<f32>(random.getU32(12))-6.0f)*DEG2RAD;
        switch(random.getU32(3)) {
        case 0: part.angularVelocity.x=0.0f;break;
        case 1: part.angularVelocity.y=0.0f;break;
        default: part.angularVelocity.z=0.0f;break;
        }
    }
    // USA80766290: four child actors fly while the hidden parent keeps moving.
    m_recoveryTimer=300;
    m_reviveTimer=m_flinchTimer=0;
    disableCollision();
    cueSound(0x24a,SoundCall::Start);
}

/// @addr{0x8077A5F8}
void ObjectSanbo::calcMove() {
    constexpr f32 GRAVITY = 2.0f;

    if (m_standstill) {
        if (--m_stillDuration == 0) {
            m_standstill = false;
        }

        return;
    }

    auto railStatus = m_railInterpolator->calc();
    if (railStatus == RailInterpolator::Status::ChangingDirection) {
        m_standstill = true;
        m_stillDuration = m_railInterpolator->curPoint().setting[0];
    }

    const EGG::Vector3f &railPos = m_railInterpolator->curPos();
    m_yVel -= GRAVITY;
    setPos(EGG::Vector3f(railPos.x, m_yVel + pos().y, railPos.z));

    checkSphere();
}

/// @addr{0x8077A8B8}
/// @brief Handles collision between the pokie and the floor (including sandcones).
void ObjectSanbo::checkSphere() {
    constexpr f32 RADIUS = 10.0f;
    constexpr EGG::Vector3f POS_OFFSET = EGG::Vector3f(0.0f, RADIUS, 0.0f);

    CollisionInfo colInfo;
    colInfo.bbox.setZero();

    EGG::Vector3f norm = m_up;

    if (CollisionDirector::Instance()->checkSphereFull(RADIUS, pos() + POS_OFFSET,
                EGG::Vector3f::inf, KCL_TYPE_FLOOR, &colInfo, nullptr, 0)) {
        m_yVel = 0.0f;
        addPos(colInfo.tangentOff);
        norm = colInfo.floorNrm;
    }

    EGG::Vector3f upNrm = m_up;
    upNrm.normalise();
    norm.normalise();

    m_up += (norm - upNrm).multInv(60.0f);
    m_up.normalise();

    setMatrixTangentTo(m_up, m_tangent);
}

} // namespace Kinoko::Field
