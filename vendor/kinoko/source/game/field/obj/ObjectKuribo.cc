#include "ObjectKuribo.hh"

#include "game/field/CollisionDirector.hh"
#include "game/kart/KartCollide.hh"
#include "game/kart/KartObject.hh"
#include "game/field/ObjectDirector.hh"
#include "game/field/RailManager.hh"
#include "game/item/ItemDirector.hh"
#include "game/system/RaceConfig.hh"

#include <cmath>

namespace Kinoko::Field {

/// @addr{0x806DB184}
ObjectKuribo::ObjectKuribo(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), StateManager(this, STATE_ENTRIES) {
    ASSERT(m_mapObj);
    m_initialScale = scale();
    m_animStep = static_cast<f32>(m_mapObj->setting(2)) / 100.0f;
    m_speedStep = static_cast<f32>(m_mapObj->setting(1)) / 100.0f;
}

/// @addr{0x806DB3A0}
ObjectKuribo::~ObjectKuribo() = default;

/// @addr{0x806DB40C}
void ObjectKuribo::init() {
    calcTransform();
    m_origin = transform().base(2);

    m_railInterpolator->init(0.0f, 0);
    m_railInterpolator->setCurrVel(0.0f);

    m_currSpeed = 0.0f;
    m_animTimer = 0.0f;
    m_frameCount = 0;
    m_damagePhase = DamagePhase::Alive;
    setScale(m_initialScale);
    if (getUnit()) enableCollision();
    m_damageFrame = m_flatFrame = 0;
    m_recoveryDelay = static_cast<s16>(m_mapObj->setting(0));
    m_damageWidth = m_damageHeight = 1.0f;
    m_visible = true;
    m_animationSound = {};

    auto *anmMgr = m_drawMdl->anmMgr();
    anmMgr->playAnim(0.0f, m_animStep, 0);
    m_maxAnimTimer = anmMgr->activeAnim(Render::AnmType::Chr)->frameCount();
    m_nextStateId = 1;
}

/// @addr{0x806DB5B0}
void ObjectKuribo::calc() {
    m_animTimer = ::fmodf(static_cast<f32>(m_frameCount) * m_animStep, m_maxAnimTimer);

    StateManager::calc();
    calcDamage();

    // walk_l/walk_r BRASD point events at frames28/56. Keep an unwrapped
    // timer so even a fractional/high animation rate crosses each event.
    if (m_damagePhase == DamagePhase::Alive) {
        m_animationSound.update(*this, "kuribo", "walk_l",
                static_cast<f32>(m_frameCount) * m_animStep, m_maxAnimTimer);
    } else m_animationSound.update(*this, "kuribo", "none", 0.0f);

    ++m_frameCount;
}

/// @addr{0x806dd278}
void ObjectKuribo::loadAnims() {
    std::array<const char *, 2> names = {{
            "walk_l",
            "walk_r",
    }};

    std::array<Render::AnmType, 2> types = {{
            Render::AnmType::Chr,
            Render::AnmType::Chr,
    }};

    linkAnims(names, types);
}

void ObjectKuribo::enterStateStub() {}


/// @addr{0x806DC220}
/// @brief Called when the Goomba is changing direction.
void ObjectKuribo::calcStateReroute() {
    if (m_railInterpolator->curPoint().setting[0] < m_currentFrame) {
        m_nextStateId = 1;
    }

    checkSphereFull();
    calcRot();
    setMatrixTangentTo(m_rot, m_origin);
}

/// @addr{0x806DC3F8}
/// @brief Called when Goomba is walking along the rail.
void ObjectKuribo::calcStateWalk() {
    calcAnim();
}

/// @addr{0x806DCDDC}
void ObjectKuribo::calcAnim() {
    bool shouldMove;

    if (m_railInterpolator->isMovementDirectionForward()) {
        shouldMove = m_animTimer > 15.0f && m_animTimer < 25.0f;
    } else {
        shouldMove = m_animTimer > 45.0f && m_animTimer < 55.0f;
    }

    if (shouldMove) {
        m_currSpeed = std::min(10.0f, m_currSpeed + m_speedStep);
    } else {
        m_currSpeed = std::max(0.0f, m_currSpeed - m_speedStep);
    }

    m_railInterpolator->setCurrVel(m_currSpeed);
    const auto &curPos = m_railInterpolator->curPos();
    setPos(EGG::Vector3f(curPos.x, pos().y, curPos.z));

    if (m_railInterpolator->calc() == RailInterpolator::Status::ChangingDirection) {
        m_nextStateId = 0;
        m_currSpeed = 0.0f;
    }

    checkSphereFull();
    calcRot();
    setMatrixTangentTo(m_rot, m_origin);
}

/// @addr{0x806DCC9C}
void ObjectKuribo::calcRot() {
    m_rot = Interpolate(0.1f, m_rot, m_floorNrm);

    if (m_rot.squaredLength() > std::numeric_limits<f32>::epsilon()) {
        m_rot.normalise2();
    } else {
        m_rot = EGG::Vector3f::ey;
    }
}

/// @addr{0x806DCB58}
void ObjectKuribo::checkSphereFull() {
    constexpr f32 RADIUS = 50.0f;

    // Apply gravity if we're not changing direction
    if (m_currentStateId != 0) {
        subPos(EGG::Vector3f(0.0f, 2.0f, 0.0f));
    }

    CollisionInfo colInfo;
    EGG::Vector3f colPos = pos();
    colPos.y += RADIUS;

    bool hasCol = CollisionDirector::Instance()->checkSphereFull(RADIUS, colPos, EGG::Vector3f::inf,
            KCL_TYPE_FLOOR, &colInfo, nullptr, 0);

    if (hasCol) {
        addPos(colInfo.tangentOff);

        if (colInfo.floorDist > -std::numeric_limits<f32>::min()) {
            m_floorNrm = colInfo.floorNrm;
        }
    }
}

// The source squash/hide/grow flags run beside the route clock. The
// object's walking geometry keeps its hit-frame pose until recovery completes.
void ObjectKuribo::beginSquash() {
    if (m_damagePhase != DamagePhase::Alive) return;
    m_damagePhase = DamagePhase::Squash;
    m_damageFrame = m_flatFrame = 0;
    m_damageWidth = m_damageHeight = 1.0f;
    m_hitAnimFrame = m_animTimer;
    m_visible = true;
    disableCollision();
    cueSound(0x22a, SoundCall::Start); // SE_CC_KURIBO_KILL
}

void ObjectKuribo::enterHiddenState() {
    m_damagePhase = DamagePhase::Hidden;
    m_damageFrame = 0;
    disableCollision();
}

Kart::Reaction ObjectKuribo::onCollision(Kart::KartObject *kart, Kart::Reaction reactionOnKart,
        Kart::Reaction reactionOnObj, EGG::Vector3f &) {
    if ((reactionOnObj == Kart::Reaction::UNK_3 || reactionOnObj == Kart::Reaction::UNK_5) &&
            m_damagePhase == DamagePhase::Alive) {
        beginSquash();
        dropMushroom(kart->velocity(), kart->param()->playerIdx());
    }
    return reactionOnKart;
}

s16 ObjectKuribo::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &velocity,
        s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) {
    if ((reactionOnObj == 3 || reactionOnObj == 5) && m_damagePhase == DamagePhase::Alive) {
        beginSquash();
        dropMushroom(velocity, ObjectDirector::Instance()->itemOwner());
    }
    return reactionOnItem;
}

void ObjectKuribo::onGlobalItemCollision(s16, s16 reactionOnObj) {
    // Null-item branch806cc5c4 kills an alive Goomba without a Mushroom.
    // The physical-item branch above separately captures its drop direction.
    if (reactionOnObj == 3 || reactionOnObj == 5) beginSquash();
}

// USA806cda78 chooses the side of the authored first/last route line after
// removing the incoming velocity component along the route. The source
// course pickup uses that initial model facing *50 and the attacker's owner.
void ObjectKuribo::dropMushroom(const EGG::Vector3f &velocity, u8 owner) {
    const auto &scenario = System::RaceConfig::Instance()->raceScenario();
    using Mode = System::RaceConfig::Scenario::GameMode;
    if (scenario.gameMode == Mode::Time_Trial || scenario.gameMode == Mode::Battle ||
            (scenario.playerCount && scenario.players[0].type == System::RaceConfig::Player::Type::Ghost)) return;
    const auto *rail = RailManager::Instance()->rail(m_mapObj->pathId());
    const auto points = rail->points();
    EGG::Vector3f direction = m_origin;
    if (points.size() >= 2) {
        auto normal = points.back().pos - points.front().pos; normal.normalise2();
        const auto perpendicular = velocity - normal * velocity.dot(normal);
        const auto midpoint = (points.front().pos + points.back().pos) * 0.5f;
        if (CheckPointAgainstLineSegment(midpoint + perpendicular, points.front().pos, points.back().pos) < 0.0f)
            direction = -direction;
    }
    Item::ItemDirector::CreateCourseMushroom(pos(), direction * 50.0f, owner);
}

void ObjectKuribo::calcDamage() {
    constexpr f32 TAU = 6.283185307179586f;
    if (m_damagePhase == DamagePhase::Squash) {
        // USA806ccf30: once the squash passes below1, amplitude increases
        // from .5 to1; width rises .1 per frame, capped1.5; height clamps .1.
        const f32 amplitude = m_damageHeight < 1.0f ? 1.0f : 0.5f;
        m_damageHeight = 1.0f + amplitude * std::sin(TAU * static_cast<f32>(m_damageFrame) / 20.0f);
        if (m_damageHeight < 0.1f) {
            m_damageHeight = 0.1f; m_damageWidth = 1.5f;
            m_flatFrame = m_damageFrame;
            m_damagePhase = DamagePhase::Hidden; m_damageFrame = 0;
        } else {
            if (m_damageHeight < 1.0f) m_damageWidth = std::min(1.5f, m_damageWidth + 0.1f);
            ++m_damageFrame;
        }
    }
    if (m_damagePhase == DamagePhase::Hidden) {
        // USA806cd0e0: authored KMP setting0 is the complete recovery delay;
        // the hide countdown includes the ticks spent squashing.
        if (static_cast<s32>(m_damageFrame + m_flatFrame) > m_recoveryDelay) {
            m_damagePhase = DamagePhase::Growing; m_damageFrame = 0;
            m_visible = true;
            cueSound(0x22b, SoundCall::Start); // SE_CC_KURIBO_REVIVE
        } else {
            if (m_damageFrame > 71) m_visible = false;
            else if (m_damageFrame > 70) m_visible = m_damageFrame % 5 == 0;
            else if (m_damageFrame > 50) m_visible = m_damageFrame % 3 != 0;
            else if (m_damageFrame > 30) m_visible = m_damageFrame % 10 != 0;
            ++m_damageFrame;
        }
    }
    if (m_damagePhase == DamagePhase::Growing) {
        // USA806cd328: rise/overshoot with a40-tick sine, settling to1 on
        // its downward crossing after tick10. Collision returns at settling.
        m_damageHeight = 1.5f * std::sin(TAU * static_cast<f32>(m_damageFrame) / 40.0f);
        m_damageWidth = std::min(1.0f, m_damageWidth + 0.1f);
        if (m_damageFrame > 10 && m_damageHeight <= 1.0f) {
            m_damagePhase = DamagePhase::Alive; m_damageWidth = m_damageHeight = 1.0f;
            enableCollision();
            if (m_currentStateId >= 2) m_nextStateId = 1;
        } else ++m_damageFrame;
    }
    if (m_damagePhase != DamagePhase::Alive || scale() != m_initialScale) {
        setScale(EGG::Vector3f(m_initialScale.x * m_damageWidth,
                m_initialScale.y * m_damageHeight, m_initialScale.z * m_damageWidth));
        // The route has already published a matrix for this step. Publish
        // scale via the regular model pass instead of another movement step.
    }
}

} // namespace Kinoko::Field
