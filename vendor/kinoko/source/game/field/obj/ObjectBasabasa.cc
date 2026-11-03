#include "ObjectBasabasa.hh"

#include "game/field/ObjectDirector.hh"
#include "game/kart/KartObject.hh"
#include <cmath>
#include "game/field/RailManager.hh"

#include "game/system/RaceConfig.hh"
#include "game/system/RaceManager.hh"

namespace Kinoko::Field {

/// @addr{0x806B5C84}
ObjectBasabasaDummy::ObjectBasabasaDummy(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), StateManager(this, STATE_ENTRIES),
      m_bigBump(params.setting(7) == 1) {
    m_active = true;
}

/// @addr{0x806B7630}
ObjectBasabasaDummy::~ObjectBasabasaDummy() = default;

/// @addr{0x806B5E80}
void ObjectBasabasaDummy::init() {
    m_railInterpolator->init(0.0f, 0);
    setPos(m_railInterpolator->curPos());

    auto &rng = System::RaceManager::Instance()->random();
    rng.next();
    f32 y = rng.getF32(ObjectBasabasa::initialYRange());
    f32 x = rng.getF32(ObjectBasabasa::initialXRange());

    m_initialPos = EGG::Vector3f(x - ObjectBasabasa::initialXRange() * 0.5f, y, 0.0f);

    m_nextStateId = 0;
    m_hitVisible = true;
}

/// @addr{0x806B6874}
Kart::Reaction ObjectBasabasaDummy::onCollision(Kart::KartObject *kartObj,
        Kart::Reaction reactionOnKart, Kart::Reaction reactionOnObj,
        EGG::Vector3f & /*hitDepth*/) {
    auto course = System::RaceConfig::Instance()->raceScenario().course;
    if (course == Course::Dry_Dry_Ruins && m_bigBump) {
        const auto &hitTable = ObjectDirector::Instance()->hitTableKart();
        const auto column = ObjectHitTable::KartColumn(kartObj);
        reactionOnKart = hitTable.reaction(hitTable.slot(ObjectId::BasabasaDummy), column);
        const auto &objectTable = ObjectDirector::Instance()->hitTableKartObject();
        reactionOnObj = objectTable.reaction(objectTable.slot(ObjectId::BasabasaDummy), column);
    }

    if (static_cast<s16>(reactionOnObj) == 3 || static_cast<s16>(reactionOnObj) == 5)
        requestHit(kartObj->velocity());
    return reactionOnKart;
}

// USA806b2590: a slow item's position supplies its outward direction;
// a global hit uses the negated current forward axis. Preserve the item result.
void ObjectBasabasaDummy::requestHit(const EGG::Vector3f &velocity) {
    if (m_currentStateId == 1) return;
    m_nextStateId = 1;
    m_hitVelocity = velocity;
}
s16 ObjectBasabasaDummy::onItemCollision(const EGG::Vector3f &position,
        const EGG::Vector3f &velocity, s16 itemReaction, s16 objectReaction, EGG::Vector3f &) {
    if (objectReaction >= 3 && objectReaction <= 6 && m_currentStateId != 1) {
        auto direction = velocity;
        if (direction.length() < 30.0f) {
            direction = pos() - position;
            direction.normalise();
            direction *= 80.0f;
        }
        requestHit(direction);
    }
    return itemReaction;
}
void ObjectBasabasaDummy::onGlobalItemCollision(s16, s16 objectReaction) {
    if (objectReaction >= 3 && objectReaction <= 6) {
        calcTransform();
        requestHit(transform().base(2) * -80.0f);
    }
}

// USA806b1e00: detach the collision, launch at 20 degrees and speed 150.
void ObjectBasabasaDummy::enterState1() {
    unregisterCollision();
    const float horizontal = std::sqrt(m_hitVelocity.x * m_hitVelocity.x +
            m_hitVelocity.z * m_hitVelocity.z);
    m_hitVelocity.y = horizontal * std::tan(20.0f * DEG2RAD);
    m_hitVelocity.normalise();
    m_hitVelocity *= 150.0f;
    calcTransform();
    m_hitForward = m_hitAxis = transform().base(2);
    m_hitUp = transform().base(1);
    cueSound(0x23f, SoundCall::Voice);
}

// USA806b20a4: gravity 2/tick, roll 30 degrees/tick. The second angular
// term is a zero-initialized REL BSS value with no other references.
void ObjectBasabasaDummy::calcState1() {
    m_hitVelocity.y -= 2.0f;
    addPos(m_hitVelocity);
    EGG::Matrix34f rotation;
    rotation.setAxisRotation(30.0f * DEG2RAD, m_hitAxis);
    rotation.setBase(3, EGG::Vector3f::zero);
    m_hitUp = rotation.ps_multVector(m_hitUp);
    EGG::Matrix34f basis = EGG::Matrix34f::ident;
    basis.setBase(0, m_hitUp.cross(m_hitForward));
    basis.setBase(1, m_hitUp);
    basis.setBase(2, m_hitForward);
    basis.setBase(3, pos());
    setTransform(basis);
    m_hitVisible = m_currentFrame > 130 ? m_currentFrame % 5 == 0 :
            m_currentFrame > 110 ? m_currentFrame % 3 != 0 :
            m_currentFrame > 90 ? m_currentFrame % 10 != 0 : true;
    if (m_currentFrame > 130) {
        init();
        m_active = false;
        m_hitVisible = false;
    }
}

/// @addr{0x806B6100}
void ObjectBasabasaDummy::calcState0() {
    if (!m_active) {
        return;
    }

    if (m_railInterpolator->calc() == RailInterpolator::Status::ChangingDirection) {
        m_active = false;
        unregisterCollision();
    } else {
        setMatrixFromOrthonormalBasisAndPos(m_railInterpolator->curTangentDir());
        calcTransform();

        EGG::Matrix34f mat = transform();
        mat.setBase(3, EGG::Vector3f::zero);
        setPos(m_railInterpolator->curPos() + mat.ps_multVector(m_initialPos));

        // Retail 806b1c78: wings and a screech in flight (the actor thins both out).
        cueSound(0x23b, SoundCall::Hold);  // SE_CC_BASA_FLY
        cueSound(0x23e, SoundCall::Voice); // SE_CC_BASA_V_ATK
    }
}

/// @addr{0x806B70D0}
ObjectBasabasa::ObjectBasabasa(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), m_initialTimer(params.setting(1)),
      m_batsPerGroup(params.setting(2)), m_startFrame(params.setting(6)),
      m_batSpacing(static_cast<u32>(static_cast<f32>(params.setting(5)) /
              static_cast<f32>(params.setting(0)) / static_cast<f32>(m_batsPerGroup))) {
    f32 railLen = RailManager::Instance()->rail(params.pathId())->getPathLength();
    u32 groupCount = static_cast<u32>(railLen / static_cast<f32>(params.setting(0)) /
                             static_cast<f32>(m_initialTimer)) +
            1;
    u32 batCount = groupCount * m_batsPerGroup;

    m_bats = owning_span<ObjectBasabasaDummy *>(batCount);

    for (auto *&bat : m_bats) {
        bat = EGG::egg_new<ObjectBasabasaDummy>(params);
        bat->load();
    }

    s_initialXRange = params.setting(3);
    s_initialYRange = params.setting(4);
}

/// @addr{0x806B72F4}
ObjectBasabasa::~ObjectBasabasa() = default;

/// @addr{0x806B7334}
void ObjectBasabasa::init() {
    for (auto *&bat : m_bats) {
        if (bat->active()) {
            bat->setActive(false);
            bat->unregisterCollision();
        }
    }

    m_cycleTimer = m_initialTimer;
    m_batsActive = 0;
}

/// @addr{0x806B74C4}
void ObjectBasabasa::calc() {
    if (System::RaceManager::Instance()->timer() <= m_startFrame) {
        return;
    }

    if (m_cycleTimer == m_initialTimer + m_batSpacing * (m_batsActive % m_batsPerGroup)) {
        auto *&bat = m_bats[m_batsActive++];
        bat->init();
        bat->setActive(true);
        bat->loadAABB(0.0f);

        if ((m_batsActive % m_batsPerGroup) == 0) {
            m_cycleTimer = m_batSpacing * m_batsPerGroup;
        }
    }

    m_batsActive %= m_bats.size();

    ++m_cycleTimer;
}

f32 ObjectBasabasa::s_initialXRange;
f32 ObjectBasabasa::s_initialYRange;

} // namespace Kinoko::Field
