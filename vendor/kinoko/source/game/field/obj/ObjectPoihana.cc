#include "ObjectPoihana.hh"

#include "game/field/CollisionDirector.hh"
#include "game/kart/KartCollide.hh"
#include "game/kart/KartMove.hh"
#include "game/kart/KartObjectManager.hh"
#include "game/system/RaceConfig.hh"
#include <algorithm>
#include <cmath>

namespace Kinoko::Field {

/// @addr{0x80747198}
ObjectPoihanaBase::ObjectPoihanaBase(const System::MapdataGeoObj &params)
    : StateManager(this, {}), ObjectCollidable(params), m_walkState(WalkState::NeedTarget),
      m_heightOffset(0.0f), m_radius(150.0f), m_targetPos(EGG::Vector3f::zero) {}

/// @addr{0x80747208}
ObjectPoihanaBase::~ObjectPoihanaBase() = default;

/// @addr{0x80747248}
void ObjectPoihanaBase::init() {
    m_workMat = EGG::Matrix34f::ident;
    m_accel.setZero();
    m_extVel.setZero();
    m_vel.setZero();
    m_curRot = EGG::Vector3f::ey;
    m_up = EGG::Vector3f::ez;
    m_floorNrm = EGG::Vector3f::ey;
    m_walkState = WalkState::NeedTarget;
    m_workMat.setBase(3, pos());
}

/// @addr{0x8074816C}
ObjectPoihana::ObjectPoihana(const System::MapdataGeoObj &params)
    : StateManager(this, STATE_ENTRIES), ObjectPoihanaBase(params) {}

/// @addr{0x807488BC}
ObjectPoihana::~ObjectPoihana() = default;

/// @addr{0x80748958}
void ObjectPoihana::init() {
    ObjectPoihanaBase::init();

    m_initPos = curPos();
    m_dir = EGG::Vector3f::ez;
    m_targetVel.setZero();
    m_accel = EGG::Vector3f(0.0f, -1.2f, 0.0f);
    m_radius = 100.0f;
    m_heightOffset = -160.0f;
    m_homeRadius = static_cast<s16>(m_mapObj->setting(1)) > 0 ?
            static_cast<f32>(m_mapObj->setting(1)) : 4000.0f;
    m_throwPending = m_hitPending = m_toppling = m_grounded = false;
    m_actionFrame = m_auxFrame = m_toppleCount = m_seekCursor = m_actionPhase = 0;
    m_target = m_lastTarget = -1;
    m_airVelocity = m_airOffset = 0.0f;
    const auto &scenario = System::RaceConfig::Instance()->raceScenario();
    m_fixedMovement = scenario.gameMode == System::RaceConfig::Scenario::GameMode::Time_Trial &&
            scenario.playerCount && scenario.players[0].type == System::RaceConfig::Player::Type::Ghost;
    playAnimation("walk");
    m_nextStateId = 0;
}

/// @addr{0x80747530}
void ObjectPoihana::calcCurRot(f32 t) {
    m_curRot = Interpolate(t, m_curRot, m_floorNrm);
    if (m_curRot.squaredLength() > std::numeric_limits<f32>::epsilon()) {
        m_curRot.normalise2();
    } else {
        m_curRot = EGG::Vector3f::ey;
    }
}

/// @addr{0x807475DC}
void ObjectPoihana::calcOrthonormalBasis() {
    EGG::Vector3f side = m_curRot.cross(m_up);
    side.normalise2();

    if (side.squaredLength() <= std::numeric_limits<f32>::epsilon()) {
        side = EGG::Vector3f::ex;
    }

    EGG::Vector3f up = side.cross(m_curRot);
    up.normalise2();

    m_workMat.setBase(0, side);
    m_workMat.setBase(1, m_curRot);
    m_workMat.setBase(2, up);
}

/// @addr{0x80747788}
void ObjectPoihana::calcCollision() {
    CollisionInfo info;
    m_grounded = false;
    EGG::Vector3f pos = collisionPos();

    bool hasCol = CollisionDirector::Instance()->checkSphereFull(m_radius, pos, EGG::Vector3f::inf,
            KCL_TYPE_64EBDFFF, &info, nullptr, 0);

    if (!hasCol) {
        return;
    }

    m_workMat.setBase(3, m_workMat.base(3) + info.tangentOff);

    if (info.wallDist > -std::numeric_limits<f32>::min()) {
        m_extVel += Project(m_extVel, info.wallNrm) * -2.0f;
    }

    if (info.floorDist > -std::numeric_limits<f32>::min()) {
        m_floorNrm = info.floorNrm;
        m_extVel.setZero();
        m_grounded = true;
    }
}

/// @addr{0x80748D98}
void ObjectPoihana::calcStep() {
    constexpr f32 COARSE_SCALAR = 0.1f;
    constexpr f32 FINE_SCALAR = 100.0f;
    constexpr f32 CLAMP = 0.5f;
    constexpr s32 MOD = 100;
    constexpr f32 INV_MOD = 1.0f / static_cast<f32>(MOD);
    constexpr f32 SPEED = 7.0f;
    constexpr f32 RAND_POS_MAX = 2000.0f;
    constexpr f32 RAND_POS_MIN = 1000.0f;
    constexpr f32 RAND_POS_RANGE = RAND_POS_MAX - RAND_POS_MIN;
    constexpr f32 TARGET_DIST_THRESHOLD = 200.0f;

    if (m_walkState == WalkState::NeedTarget) {
        // psuedo-random number generator based on current position of the Poihana
        EGG::Vector3f pos = curPos();
        f32 sum = EGG::Mathf::abs(pos.x + pos.z);
        s32 tmp1 = static_cast<s32>(COARSE_SCALAR * sum);
        s32 tmp2 = static_cast<s32>(FINE_SCALAR * sum);
        s32 remSum = (tmp1 % MOD) + (tmp2 % MOD);
        f32 x = INV_MOD * static_cast<f32>(remSum % MOD);
        f32 z = INV_MOD * static_cast<f32>(remSum * 7 % MOD);

        EGG::Vector3f dir = EGG::Vector3f(x - CLAMP, 0.0f, z - CLAMP);
        dir.normalise2();

        f32 scalar = RAND_POS_RANGE * (INV_MOD * static_cast<f32>(remSum * 3 % MOD)) + RAND_POS_MIN;
        m_targetPos = m_initPos + dir * scalar;

        calcDir();

        m_walkState = WalkState::HasTarget;
    }

    if (m_walkState == WalkState::HasTarget) {
        EGG::Vector3f delta = m_targetPos - curPos();
        delta.y = 0.0f;

        f32 len = 0.0f;
        if (delta.squaredLength() > std::numeric_limits<f32>::epsilon()) {
            len = delta.normalise();
        }

        m_targetVel = delta * SPEED;

        if (len < TARGET_DIST_THRESHOLD) {
            m_walkState = WalkState::NeedTarget;
        }
    }

    calcCurRot(0.1f);
    calcUp();

    m_vel = Interpolate(0.05f, m_vel, m_targetVel);
}

/// @addr{0x80749610}
void ObjectPoihana::calcDir() {
    EGG::Vector3f delta = m_targetPos - curPos();
    delta.y = 0.0f;
    if (delta.squaredLength() > std::numeric_limits<f32>::epsilon()) {
        delta.normalise2();
        m_dir = delta;
    }
}

/// @addr{0x807496C4}
void ObjectPoihana::calcUp() {
    EGG::Vector3f up = Interpolate(0.1f, m_workMat.base(2), m_dir);
    if (up.squaredLength() > std::numeric_limits<f32>::epsilon()) {
        up.normalise2();
    } else {
        up = EGG::Vector3f::ez;
    }

    m_up = up;
}

// These clocks belong to simulation. The renderer samples them without
// changing state, so animation sounds survive culling and stereo rendering.
void ObjectPoihana::playAnimation(const char *name, f32 rate) {
    m_animationName = name;
    m_animationFrame = 0.0f;
    m_animationRate = rate;
}

void ObjectPoihana::calc() {
    if (m_nextStateId >= 0) {
        if (m_currentStateId == 3 || m_currentStateId == 4) m_throwPending = false;
        // Retail OnEndTopple also runs when another hit restarts the same
        // action. Recovery's -.6 gravity must not leak into the next fall.
        if (m_currentStateId >= 5) {
            m_toppling = false;
            m_accel = EGG::Vector3f(0.0f, -1.2f, 0.0f);
        }
    }
    ObjectPoihanaBase::calc();
    m_animationFrame += m_animationRate;
    const bool loop = !std::strcmp(m_animationName, "walk");
    m_animationSound.update(*this, "poihana", m_animationName, m_animationFrame,
            loop ? mkw::ObjectAnimationSound::frames("poihana", "walk") : 0.0f);
}

// USA807394ac: normal hits request a throw; Star/Mega hits request a topple.
Kart::Reaction ObjectPoihana::onCollision(Kart::KartObject *kart,
        Kart::Reaction reactionOnKart, Kart::Reaction reactionOnObj, EGG::Vector3f &) {
    const auto reaction = static_cast<unsigned>(reactionOnObj);
    if (reaction == 4 || reaction == 6) {
        if (m_toppling || m_hitPending) return Kart::Reaction::WallAllSpeed;
        const auto &scenario = System::RaceConfig::Instance()->raceScenario();
        if (scenario.players[kart->param()->playerIdx()].type == System::RaceConfig::Player::Type::Ghost &&
                !m_fixedMovement) return reactionOnKart;
        if (!m_throwPending) {
            m_throwPending = true;
            m_throwPosition = kart->pos();
            kart->move()->cuePoihanaThrow();
        }
    } else if ((reaction == 3 || reaction == 5) && !m_hitPending) {
        m_hitPending = true;
        m_hitDirection = curPos() - kart->pos();
        m_hitPosition = kart->pos();
        cueSound(0x262, SoundCall::Voice); // SE_CC_POIHANA_DMG
    }
    return reactionOnKart;
}

void ObjectPoihana::publishTransform() {
    calcOrthonormalBasis();
    setTransform(m_workMat);
}

// USA80739638 copies the item's actual velocity and position, rather than
// the kart-contact separation vector, then shares the pending-topple flag.
s16 ObjectPoihana::onItemCollision(const EGG::Vector3f &position,
        const EGG::Vector3f &velocity, s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) {
    if ((reactionOnObj == 3 || reactionOnObj == 5) && !m_hitPending) {
        m_hitPending = true;
        m_hitDirection = velocity;
        m_hitPosition = position;
        cueSound(0x262, SoundCall::Voice);
    }
    return reactionOnItem;
}

void ObjectPoihana::onGlobalItemCollision(s16, s16 reactionOnObj) {
    if ((reactionOnObj != 3 && reactionOnObj != 5) || m_hitPending) return;
    // Null-item branch807396c0: vertical-only hit vector, and a virtual
    // impact point100units behind the authored/current movement direction.
    m_hitPending = true;
    m_hitDirection = EGG::Vector3f::ey;
    m_hitPosition = curPos() - m_dir * 100.0f;
    cueSound(0x262, SoundCall::Voice);
}

// The retail child checks a 7000-unit XZ box and rejects a fast racer moving
// away (signed movement direction dot toward object below -.3).
bool ObjectPoihana::targetAvailable(unsigned player) const {
    auto *manager = Kart::KartObjectManager::Instance();
    if (!manager || player >= manager->count()) return false;
    const auto *kart = manager->object(player);
    EGG::Vector3f delta = curPos() - kart->pos();
    if (std::fabs(delta.x) > 7000.0f || std::fabs(delta.z) > 7000.0f) return false;
    if (std::fabs(kart->speed()) <= 30.0f) return true;
    delta.normalise2();
    auto dir = kart->move()->dir();
    if (kart->speed() < 0.0f) dir = -dir;
    dir.normalise2();
    return delta.dot(dir) >= -0.3f;
}

s32 ObjectPoihana::findTarget() {
    auto *manager = Kart::KartObjectManager::Instance();
    if (!manager || !manager->count()) return -1;
    const unsigned count = manager->count();
    const unsigned first = m_seekCursor++ % count;
    for (unsigned n = 0; n < count; ++n) {
        const unsigned player = (first + n) % count;
        if (targetAvailable(player)) return static_cast<s32>(player);
    }
    return -1;
}

EGG::Vector3f ObjectPoihana::predictedTarget() const {
    const auto *kart = Kart::KartObjectManager::Instance()->object(m_target);
    auto dir = kart->move()->dir();
    dir.y = 0.0f;
    if (kart->speed() < 0.0f) dir = -dir;
    dir.normalise2();
    const f32 distance = (curPos() - kart->pos()).dot(dir);
    return kart->pos() + dir * (std::max(0.0f, distance) * 0.7f);
}

void ObjectPoihana::enterState0() {
    m_walkState = WalkState::NeedTarget;
    m_actionFrame = 0;
    playAnimation("walk");
}

void ObjectPoihana::calcState0And1() {
    calcStep();
    calcPosAndCollision();
    publishTransform();
    if (m_hitPending) { m_nextStateId = m_fixedMovement ? 7 : 5; return; }
    if (m_throwPending) { m_nextStateId = m_fixedMovement ? 4 : 3; return; }
    if (m_fixedMovement) return;
    const s32 target = findTarget();
    m_actionFrame = std::min<u32>(240, m_actionFrame + 1);
    EGG::Vector3f home = curPos() - m_initPos;
    home.y = 0.0f;
    if (target >= 0 && m_actionFrame >= 30 &&
            (m_actionFrame >= 240 || target != m_lastTarget) && home.length() <= m_homeRadius) {
        m_target = m_lastTarget = target;
        m_nextStateId = 2;
    }
}

void ObjectPoihana::enterChase() {
    m_actionPhase = m_actionFrame = 0;
    m_animationRate = 0.0f;
}

void ObjectPoihana::calcChase() {
    if (m_target < 0 || !targetAvailable(m_target)) { m_nextStateId = 0; return; }
    EGG::Vector3f toward = predictedTarget() - curPos();
    toward.y = 0.0f;
    toward.normalise2();
    if (toward.squaredLength() > std::numeric_limits<f32>::epsilon()) m_dir = toward;
    ++m_actionFrame;
    if (m_actionPhase == 0) {
        m_targetVel.setZero();
        if (m_actionFrame > 18) { m_actionPhase = 1; m_animationRate = 2.7f; }
        cueSound(0x25f, SoundCall::StartNew); // SE_CC_POIHANA_FIND
    } else m_targetVel = toward * (7.0f * 2.7f);
    calcCurRot(0.1f);
    m_up = Interpolate(0.4f, m_workMat.base(2), m_dir);
    if (m_up.squaredLength() > std::numeric_limits<f32>::epsilon()) m_up.normalise2();
    else m_up = EGG::Vector3f::ez;
    m_vel = m_targetVel;
    calcPosAndCollision();
    publishTransform();
    if (m_hitPending) m_nextStateId = 5;
    else if (m_throwPending) m_nextStateId = 3;
    else {
        EGG::Vector3f home = curPos() - m_initPos; home.y = 0.0f;
        if (home.length() > m_homeRadius) m_nextStateId = 0;
    }
}

void ObjectPoihana::enterCatapult() {
    playAnimation("throw");
    m_vel.setZero(); m_targetVel.setZero();
    EGG::Vector3f toward = m_throwPosition - curPos(); toward.y = 0.0f;
    if (toward.squaredLength() > std::numeric_limits<f32>::epsilon()) {
        toward.normalise2(); m_dir = m_up = toward;
    }
    m_actionFrame = 0;
}

void ObjectPoihana::calcCatapult() {
    ++m_actionFrame;
    calcPosAndCollision(); publishTransform();
    if (m_hitPending) m_nextStateId = 5;
    else if (m_actionFrame > 80) m_nextStateId = 0;
}

void ObjectPoihana::enterCatapultNoMove() {
    playAnimation("throw"); m_auxFrame = 0;
}

void ObjectPoihana::calcCatapultNoMove() {
    calcStep(); calcPosAndCollision(); publishTransform();
    ++m_auxFrame;
    if (m_hitPending) m_nextStateId = 7;
    else if (m_auxFrame > 50) { playAnimation("walk"); m_nextStateId = 1; }
}

void ObjectPoihana::rotateTopple(f32 angle, EGG::Vector3f &up, EGG::Vector3f &forward) {
    EGG::Matrix34f rotation;
    rotation.setAxisRotation(angle, m_toppleAxis);
    up = rotation.multVector33(up);
    forward = rotation.multVector33(forward);
}

void ObjectPoihana::topple() {
    playAnimation("overturn"); m_toppling = true;
    m_actionPhase = m_actionFrame = 0;
    EGG::Vector3f away = m_hitDirection; away.y = 0.0f; away.normalise2();
    m_extVel = away * 10.0f; m_extVel.y = 35.0f;
    m_vel.setZero();
    m_toppleAxis = EGG::Vector3f::ey.cross(curPos() - m_hitPosition);
    if (m_toppleAxis.squaredLength() > std::numeric_limits<f32>::epsilon()) m_toppleAxis.normalise2();
    else m_toppleAxis = EGG::Vector3f::ez;
}

void ObjectPoihana::enterTopple() { m_toppleCount = 0; topple(); }
void ObjectPoihana::enterToppleAgain() { ++m_toppleCount; topple(); }

void ObjectPoihana::calcTopple() {
    ++m_actionFrame;
    if (m_actionPhase == 0) {
        rotateTopple(m_toppleCount ? 0.21f : 0.15f, m_curRot, m_up);
        if (m_actionFrame > 5) {
            m_radius = 100.0f; m_heightOffset = 30.0f;
            if (m_grounded) { m_hitPending = false; ++m_actionPhase; }
        }
    } else if (m_actionPhase == 1) {
        if (m_actionFrame > 180) {
            m_accel = EGG::Vector3f(0.0f, -0.6f, 0.0f);
            m_extVel = EGG::Vector3f(0.0f, 15.0f, 0.0f);
            m_animationRate = 3.0f;
            m_actionFrame = 0; ++m_actionPhase;
        }
        if (m_hitPending) m_nextStateId = 6;
    } else {
        if (m_actionFrame > 10) calcCurRot(0.15f);
        if (m_actionFrame > 20) { m_radius = 100.0f; m_heightOffset = -160.0f; }
        if (m_actionFrame == 25) { playAnimation("walk", 0.0f); }
        if (m_actionFrame > 40) m_nextStateId = 0;
        if (m_hitPending) m_nextStateId = m_actionFrame > 20 ? 5 : 6;
    }
    calcPosAndCollision(); publishTransform();
}

void ObjectPoihana::enterToppleNoBury() {
    playAnimation("overturn"); m_toppling = true;
    m_actionPhase = m_auxFrame = 0;
    m_airVelocity = 35.0f; m_airOffset = 0.0f;
    m_visualForward = m_up; m_visualUp = m_curRot;
    m_toppleAxis = EGG::Vector3f::ey.cross(curPos() - m_hitPosition);
    if (m_toppleAxis.squaredLength() > std::numeric_limits<f32>::epsilon()) m_toppleAxis.normalise2();
    else m_toppleAxis = EGG::Vector3f::ez;
}

void ObjectPoihana::calcToppleNoBury() {
    calcStep(); calcPosAndCollision(); calcOrthonormalBasis();
    ++m_auxFrame;
    EGG::Vector3f drawPos = curPos();
    if (m_actionPhase == 0) {
        rotateTopple(0.2f, m_visualUp, m_visualForward);
        m_airVelocity += m_accel.y; m_airOffset += m_airVelocity;
        drawPos.y += m_airOffset;
        CollisionInfo info;
        EGG::Vector3f center = collisionPos(); center.y += m_airOffset;
        if (CollisionDirector::Instance()->checkSphereFull(m_radius, center, EGG::Vector3f::inf,
                KCL_TYPE_64EBDFFF, &info, nullptr, 0)) {
            drawPos += info.tangentOff;
            ++m_actionPhase; m_auxFrame = 0; m_hitPending = false;
            playAnimation("walk");
        }
    } else {
        m_visualUp = Interpolate(0.2f, m_visualUp, m_curRot);
        m_visualForward = Interpolate(0.2f, m_visualForward, m_up);
        if (m_visualUp.squaredLength() > std::numeric_limits<f32>::epsilon()) m_visualUp.normalise2();
        else m_visualUp = EGG::Vector3f::ey;
        if (m_visualForward.squaredLength() > std::numeric_limits<f32>::epsilon()) m_visualForward.normalise2();
        else m_visualForward = EGG::Vector3f::ey;
        if (m_hitPending) m_nextStateId = 7;
        else if (m_auxFrame > 30) m_nextStateId = 1;
    }
    auto side = m_visualUp.cross(m_visualForward);
    if (side.squaredLength() > std::numeric_limits<f32>::epsilon()) side.normalise2();
    else side = EGG::Vector3f::ex;
    auto forward = side.cross(m_visualUp); forward.normalise2();
    EGG::Matrix34f draw = m_workMat;
    draw.setBase(0, side); draw.setBase(1, m_visualUp); draw.setBase(2, forward); draw.setBase(3, drawPos);
    setTransform(draw);
}

} // namespace Kinoko::Field
