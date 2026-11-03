#include "ObjectHanachan.hh"

#include "game/field/ObjectDirector.hh"
#include "game/field/CollisionDirector.hh"

namespace Kinoko::Field {

/// @addr{0x806F2FE8}
HanachanChainManager::HanachanChainManager(const std::span<const f32> &linkDistances) {
    size_t count = linkDistances.size() + 1;

    m_links = owning_span<Field::SphereLink>(count);
    m_links[0].initLinkLen(linkDistances[0]);

    for (size_t i = 1; i < count - 1; ++i) {
        m_links[i].initLinkLen(linkDistances[i]);
        m_links[i - 1].setNext(&m_links[i]);
        m_links[i].setPrev(&m_links[i - 1]);
    }

    auto &last = m_links.back();
    auto &secondToLast = m_links[count - 2];
    last.initLinkLen(0.0f);
    secondToLast.setNext(&last);
    last.setPrev(&secondToLast);
}

/// @addr{0x806F31F4}
HanachanChainManager::~HanachanChainManager() = default;

/// @addr{0x806F49BC}
void HanachanChainManager::calc() {
    for (u32 i = 0; i < 2; ++i) {
        for (auto &link : m_links) {
            link.calc();
        }

        for (auto &link : m_links) {
            link.calcPos();
        }
    }

    for (size_t i = 0; i < m_links.size() - 1; ++i) {
        EGG::Vector3f delta = m_links[i].pos() - m_links[i + 1].pos();
        f32 len = m_links[i].linkLen();

        if (delta.squaredLength() - len * len > 0.0f) {
            calcConstraints();
            break;
        }
    }

    for (auto &link : m_links) {
        link.checkCollision();
    }
}

// USA806bc1c4/806bc448: ordinary contacts request a damped lateral shake.
// Reactions3/5 (kart),3..6 (item) use the separate airborne damage state.
Kart::Reaction ObjectHanachanPart::onCollision(Kart::KartObject *, Kart::Reaction onKart,
        Kart::Reaction onObject, EGG::Vector3f &) {
    const auto reaction = static_cast<s16>(onObject);
    if (!m_damageLocked) {
        if (reaction == 3 || reaction == 5) startBounce();
        else m_contactPending = true;
    }
    return onKart;
}
void ObjectHanachanPart::processItemReactions(s16 type, bool direct, s16 &onItem, s16 &onObject) {
    ObjectBase::processItemReactions(type, direct, onItem, onObject);
    // USA806bc254: a null item is a global hit; a live thunder cloud is ignored.
    m_ignoreItemContact = !direct && type == 9;
}
void ObjectHanachanPart::onGlobalItemCollision(s16 onItem, s16 onObject) {
    EGG::Vector3f depth = EGG::Vector3f::zero;
    onItemCollision(pos(), EGG::Vector3f::zero, onItem, onObject, depth);
}
s16 ObjectHanachanPart::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
        s16 onItem, s16 onObject, EGG::Vector3f &) {
    if (!m_damageLocked && !m_ignoreItemContact) {
        if (onObject >= 3 && onObject <= 6) startBounce();
        else m_contactPending = true;
    }
    return onItem;
}

void ObjectHanachanPart::startBounce() {
    if (m_bounceFrame >= 0) return;
    m_bounceFrame = 0;
    m_bounced = m_bounceFinished = false;
    m_contactPending = false;
    m_contactFrame = 60;
    disableCollision();
}

void ObjectHanachanPart::calcBounce() {
    m_bounceFinished = false;
    if (m_bounceFrame < 0) return;
    ++m_bounceFrame;
    const s32 ticks = m_bounced ? m_bounceFrame - m_firstLandingFrame : m_bounceFrame;
    const f32 t = static_cast<f32>(ticks);
    // USA806b1520/806bc700: v*t - gravity*t*t/2, then a half-speed rebound.
    f32 height = (m_bounced ? 35.0f : 70.0f) * t - 1.5f * t * t;
    CollisionInfo info{}; info.reset();
    KCLTypeMask mask{};
    const auto probe = pos() + EGG::Vector3f(0.0f, height, 0.0f);
    if (ticks > 5 && CollisionDirector::Instance()->checkSphereFull(10.0f, probe,
            EGG::Vector3f::inf, KCLTypeMask(0x64ebdfff), &info, &mask, 0)) {
        height = 0.0f;
        if (!m_bounced) {
            m_bounced = true;
            m_firstLandingFrame = m_bounceFrame;
            ++m_landingSerial;
            cueSound(0x26c, SoundCall::Start);
        } else {
            m_bounceFrame = -1;
            m_bounceFinished = true;
            enableCollision();
        }
    }
    m_hitOffset = EGG::Vector3f(0.0f, height, 0.0f);
}

void ObjectHanachan::calcDamageWave() {
    // USA806c0fd0: spread from the struck segment toward both ends every4ticks.
    if (m_damageWaveFrame < 0) {
        for (size_t i = 0; i < m_parts.size(); ++i) {
            if (m_parts[i]->m_bounceFrame != 1) continue;
            m_damageWaveFrame = 0;
            m_damageOrigin = i;
            m_damageLast = i <= m_parts.size()/2 ? m_parts.size()-1 : 0;
            headPart()->cueSound(0x26f, ObjectBase::SoundCall::Hold);
            for (auto *part : m_parts) part->m_damageLocked = true;
            break;
        }
    }
    if (m_damageWaveFrame < 0) return;
    if (m_damageWaveFrame % 4 == 0) {
        const size_t distance = static_cast<size_t>(m_damageWaveFrame / 4);
        if (distance <= m_damageOrigin) m_parts[m_damageOrigin-distance]->startBounce();
        if (m_damageOrigin+distance < m_parts.size()) m_parts[m_damageOrigin+distance]->startBounce();
    }
    if (m_parts[m_damageLast]->m_bounceFinished) {
        m_damageWaveFrame = -1;
        for (auto *part : m_parts) part->m_damageLocked = false;
    } else ++m_damageWaveFrame;
}

void ObjectHanachan::calcHitReactions() {
    for (auto *part : m_parts) {
        part->calcBounce();
        if (part->m_bounceFrame >= 0 || part->m_bounceFinished) continue;
        if (part->m_contactPending) {
            part->m_contactPending = false;
            part->m_contactFrame = 0;
            headPart()->cueSound(0x26e, ObjectBase::SoundCall::Hold);
        } else if (part->m_contactFrame < 60) ++part->m_contactFrame;
        const f32 frame = static_cast<f32>(part->m_contactFrame);
        // USA806bee4c: amplitude30, period6, linear decay over60frames.
        const f32 amount = 30.0f * (1.0f - frame / 60.0f) *
                EGG::Mathf::SinFIdx(RAD2FIDX * F_TAU * frame / 6.0f);
        part->m_hitOffset = part->transform().base(0) * amount;
    }
    calcDamageWave();
}

/// @addr{0x806C7D74}
ObjectHanachanHead::ObjectHanachanHead(const char *name, const EGG::Vector3f &pos,
        const EGG::Vector3f &rot, const EGG::Vector3f &scale)
    : ObjectHanachanPart(name, pos, rot, scale), m_lastPos(EGG::Vector3f::zero) {}

/// @addr{0x806CCB94}
ObjectHanachanHead::~ObjectHanachanHead() = default;

/// @addr{0x806C8450}
void ObjectHanachanHead::calcCollisionTransform() {
    calcTransform();

    EGG::Matrix34f trans = transform();
    EGG::Vector3f scaledUp = trans.base(1) * 330.0f * scale().y;
    EGG::Vector3f scaledForward = trans.base(2) * 100.0f * scale().y;
    trans.setBase(3, pos() + scaledUp + scaledForward);
    EGG::Vector3f speed = pos() - m_lastPos;

    m_collision->transform(trans, scale(), speed);
    m_lastPos = pos();
}

ObjectHanachanBody::ObjectHanachanBody(const System::MapdataGeoObj &params, const char *mdlName)
    : ObjectHanachanPart(params), m_mdlName(mdlName), m_lastSegment(false),
      m_lastPos(EGG::Vector3f::zero) {}

ObjectHanachanBody::ObjectHanachanBody(const char *name, const EGG::Vector3f &pos,
        const EGG::Vector3f &rot, const EGG::Vector3f &scale, const char *mdlName)
    : ObjectHanachanPart(name, pos, rot, scale), m_mdlName(mdlName), m_lastSegment(false),
      m_lastPos(EGG::Vector3f::zero) {}

/// @addr{0x806CCAD8}
ObjectHanachanBody::~ObjectHanachanBody() = default;

/// @addr{0x806C8908}
void ObjectHanachanBody::calcCollisionTransform() {
    if (!m_lastSegment) {
        ObjectCollidable::calcCollisionTransform();
        return;
    }

    EGG::Matrix34f trans = transform();
    trans.setBase(3, pos() + trans.base(2) * 80.0f * scale().y);
    EGG::Vector3f posDelta = pos() - m_lastPos;

    m_collision->transform(trans, scale(), posDelta);
    m_lastPos = pos();
}

/// @addr{0x806C8A5C}
ObjectHanachan::ObjectHanachan(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), StateManager(this, STATE_ENTRIES), m_chain(BODY_PART_DISTANCES),
      m_movingVel(static_cast<f32>(static_cast<s16>(params.setting(0)))) {
    constexpr f32 SCALE = 3.0f;
    constexpr EGG::Vector3f SCALE_VEC = EGG::Vector3f(SCALE, SCALE, SCALE);

    auto *&head = headPart();
    head = EGG::egg_new<ObjectHanachanHead>("BossHanachanHead", EGG::Vector3f::zero,
            EGG::Vector3f::ez, EGG::Vector3f::ez);
    head->setScale(SCALE_VEC);

    const auto &mdlNames = ObjectHanachanBody::MDL_NAMES;
    size_t mdlNameCount = ObjectHanachanBody::MDL_NAMES.size();
    auto parts = bodyParts();
    for (size_t i = 0; i < parts.size(); ++i) {
        auto *&part = parts[i];
        part = EGG::egg_new<ObjectHanachanBody>(params, mdlNames[i % mdlNameCount]);
        part->setScale(SCALE_VEC);
    }

    head->load();

    const auto &flowTable = ObjectDirector::Instance()->flowTable();
    for (size_t i = 0; i < parts.size(); ++i) {
        auto *&part = parts[i];
        part->load();

        const auto *collisionSet = flowTable.set(flowTable.slot(part->id()));
        f32 radius = SCALE * static_cast<f32>(parse<s16>(collisionSet->params.sphere.radius));
        part->resize(radius, 0.0f);
    }

    m_partDisplacement[0] = 0.0f;
    for (size_t i = 1; i < m_partDisplacement.size(); ++i) {
        m_partDisplacement[i] = m_partDisplacement[i - 1] + BODY_PART_DISTANCES[i - 1];
    }
}

/// @addr{0x806C9598}
ObjectHanachan::~ObjectHanachan() = default;

/// @addr{0x806C9630}
void ObjectHanachan::init() {
    m_animationFrames = {};
    m_damageWaveFrame = -1;
    for (auto *part : m_parts) {
        part->m_bounceFrame = -1;
        part->m_bounced = part->m_bounceFinished = part->m_damageLocked = false;
        part->m_landingSerial = 0;
        part->m_ignoreItemContact = false;
        part->enableCollision();
        part->m_contactPending = false;
        part->m_contactFrame = 60;
        part->m_hitOffset = EGG::Vector3f::zero;
    }
    m_animationSounds = {};
    m_bodyWaiting = {};
    m_headAnimation = 0;
    m_currentStateId = 0; m_nextStateId = 0; m_currentFrame = 0;
    m_railInterpolator->init(0.0f, 0);
    m_railInterpolator->setCurrVel(m_movingVel);

    initBody();

    m_still = false;
    m_stillAngVel = 15.0f;
    m_leftMisalignFrame = 0;
    m_right = EGG::Vector3f::ez;
    m_railAlignment = RailAlignment::Unknown;
    m_prevRailAlignment = RailAlignment::Unknown;

    reinterpret_cast<ObjectHanachanBody *>(m_parts.back())->m_lastSegment = true;

    initChain();
}

/// @addr{0x806C9BC0}
void ObjectHanachan::initWalk() {
    m_animationFrames = {};
    m_bodyWaiting = {};
    m_headAnimation = 0;
    setMovingVel();
    m_stillAngVel = 15.0f;
    m_still = false;
    m_leftMisalignFrame = 0;
}

void ObjectHanachan::initWait() {
    // USA806bf2d4: stop the head first; the body segments stop4 ticks apart.
    m_headAnimation = 1;
    m_animationFrames[0] = 0;
}

void ObjectHanachan::calcAnimationSounds() {
    // Head walk/wait have no BRASD. The idle flourish has its own one-shot;
    // each body segment owns the walking footfalls at its original rate.
    if (m_headAnimation == 2)
        m_animationSounds[0].update(*m_parts[0], "hanachan", "head_wait2", m_animationFrames[0]);
    for (size_t i = 1; i < m_parts.size(); ++i) {
        if (!m_bodyWaiting[i - 1])
            m_animationSounds[i].update(*m_parts[i], "hanachan", "walk", m_animationFrames[i],
                    mkw::ObjectAnimationSound::frames("hanachan", "walk"));
    }
}

/// @addr{0x806C9D38}
void ObjectHanachan::calcWalk() {
    if (m_still) {
        m_railInterpolator->setCurrVel(0.0f);
        m_nextStateId = 1;
    }

    m_prevRailAlignment = m_railAlignment;
    m_railAlignment = calcRailAlignment();

    clearChain();
    calcRailAlignmentMotion();
    m_chain.calc();

    // Retail 806bf144: every 15 frames of walking the ground shakes under the middle segment.
    if (m_currentFrame % 15 == 0) {
        m_parts[(m_parts.size() & ~size_t(1)) / 2]->cueSound(0x26c, SoundCall::Start);
    }

    m_stillAngVel = 15.0f;
}

/// @addr{0x806C9F98}
void ObjectHanachan::calcWait() {
    // USA806bf3ec..806bf52c: successive body waits, then head_wait2 at60;
    // the authored180-frame flourish returns to ordinary head_wait.
    for (size_t i = 0; i < m_bodyWaiting.size(); ++i) {
        if (m_currentFrame == i * 4) {
            m_bodyWaiting[i] = true;
            m_animationFrames[i + 1] = 0;
        }
    }
    if (m_currentFrame == 60) { m_headAnimation = 2; m_animationFrames[0] = 0; }
    if (m_currentFrame == 240) { m_headAnimation = 1; m_animationFrames[0] = 0; }
    if (shouldStartMoving()) {
        m_nextStateId = 0;
    }

    clearChain();

    if (m_stillAngVel >= 0.0f) {
        m_stillAngVel -= 0.25f;
    } else {
        m_stillAngVel += 0.25f;
    }

    if (EGG::Mathf::abs(m_stillAngVel) <= 1.0f) {
        m_stillAngVel = 0.0f;
    }

    calcSlowMotion();

    m_chain.calc();
}

/// @addr{0x806CA24C}
void ObjectHanachan::onSegmentEnd() {
    u16 setting = m_railInterpolator->curPoint().setting[0];
    if (setting != 0) {
        m_still = true;
        m_stillDuration = setting;
    }
}

/// @addr{0x806CA27C}
void ObjectHanachan::calcRail() {
    m_right = m_railInterpolator->curTangentDir();

    if (m_railInterpolator->calc() == RailInterpolator::Status::SegmentEnd) {
        onSegmentEnd();
    }
}

/// @addr{0x806CA2F0}
void ObjectHanachan::calcBody() {
    auto *&head = headPart();
    head->calcTransform();
    EGG::Vector3f forward = head->transform().base(2);
    EGG::Vector3f dir = Interpolate(0.1f, forward, m_railInterpolator->curTangentDir());
    head->calcTransformFromUpAndTangent(m_chain.pos(0), m_chain.up(0), dir);

    auto parts = bodyParts();
    for (size_t i = 0; i < parts.size(); ++i) {
        const EGG::Vector3f &pos = m_chain.pos(i + 1);
        dir = m_chain.pos(i) - pos;
        dir.normalise2();
        parts[i]->calcTransformFromUpAndTangent(pos, m_chain.up(i + 1), dir);
    }
}

/// @addr{0x806CA72C}
void ObjectHanachan::initBody() {
    headPart()->setPos(m_railInterpolator->curPos());

    const EGG::Vector3f &curTanDir = m_railInterpolator->curTangentDir();
    for (size_t i = 1; i < m_parts.size(); ++i) {
        m_parts[i]->setPos(m_parts[i - 1]->pos() - curTanDir * BODY_PART_DISTANCES[i - 1]);
        m_parts[i]->setMatrixTangentTo(EGG::Vector3f::ey, curTanDir);
    }
}

/// @addr{0x806CA9AC}
void ObjectHanachan::initChain() {
    m_chain.init();

    for (size_t i = 0; i < m_parts.size(); ++i) {
        m_chain.setPos(i, m_parts[i]->pos());
    }
}

/// @addr{0x806CAAD0}
void ObjectHanachan::clearChain() {
    m_chain.setPos(0, m_railInterpolator->curPos());
    m_chain.setVel(0, EGG::Vector3f::zero);
    m_chain.addSpringForce(0, EGG::Vector3f::ey * SphereLink::Gravity());
}

/// @addr{0x806CAB5C}
/// @brief If the wiggler has moved out of alignment of the rail, then applies a lateral adjustment
/// to the wiggler's body part segments.
void ObjectHanachan::calcRailAlignmentMotion() {
    constexpr u16 MISALIGNMENT_CORRECTION_DURATION = 80;

    bool becameUnaligned = m_prevRailAlignment == RailAlignment::Aligned &&
            m_railAlignment != RailAlignment::Aligned;

    if (becameUnaligned && m_railAlignment == RailAlignment::MisalignedLeft) {
        m_leftMisalignFrame = m_currentFrame;
    }

    if (m_leftMisalignFrame != 0 && m_currentFrame >= m_leftMisalignFrame &&
            m_currentFrame <
                    static_cast<u32>(m_leftMisalignFrame + MISALIGNMENT_CORRECTION_DURATION)) {
        calcFastMotion(m_currentFrame - m_leftMisalignFrame);
    } else {
        calcSlowMotion();
    }
}

/// @addr{0x806CACD0}
void ObjectHanachan::calcLateralMotion(f32 amplitude, f32 period, f32 wavelength, s16 frame) {
    f32 velAmplitude = F_TAU * amplitude / period;

    auto parts = bodyParts();
    for (size_t i = 0; i < parts.size(); ++i) {
        auto *&part = parts[i];
        f32 phase =
                F_TAU * (static_cast<f32>(frame) / period - m_partDisplacement[i + 1] / wavelength);
        part->calcTransform();
        EGG::Vector3f right = RotateXZByYaw(HALF_PI, part->transform().base(2));
        EGG::Vector3f posOffset = right * (amplitude * EGG::Mathf::SinFIdx(RAD2FIDX * phase));
        m_chain.setPos(i + 1, m_parts[i + 1]->pos() + posOffset);
        m_chain.setVel(i + 1, right * (velAmplitude * EGG::Mathf::CosFIdx(RAD2FIDX * phase)));
    }
}

/// @addr{0x806CAFB8}
ObjectHanachan::RailAlignment ObjectHanachan::calcRailAlignment() const {
    constexpr f32 EPSILON = 0.9995f;

    EGG::Vector3f curTanDir = m_railInterpolator->curTangentDir();
    EGG::Vector2f railTan = EGG::Vector2f(curTanDir.x, curTanDir.z);
    EGG::Vector2f right = EGG::Vector2f(m_right.x, m_right.z);
    railTan.normalise2();
    right.normalise2();

    if (railTan.dot(right) >= EPSILON) {
        return RailAlignment::Aligned;
    }

    return right.cross(railTan) < 0.0f ? RailAlignment::MisalignedLeft :
                                         RailAlignment::MisalignedRight;
}

} // namespace Kinoko::Field
