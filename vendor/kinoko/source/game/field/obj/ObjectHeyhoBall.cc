#include "ObjectHeyhoBall.hh"

#include "game/field/ObjectDirector.hh"
#include "game/system/RaceConfig.hh"

namespace Kinoko::Field {

/// @addr{0x806D02C4}
ObjectHeyhoBall::ObjectHeyhoBall(const System::MapdataGeoObj &params)
    : ObjectProjectile(params), StateManager(this, STATE_ENTRIES),
      m_airtime(static_cast<f32>(params.setting(1))), m_initPos(params.pos()) {
    registerManagedObject();
}

/// @addr{0x806D1820}
ObjectHeyhoBall::~ObjectHeyhoBall() = default;

/// @addr{0x806D05F0}
void ObjectHeyhoBall::init() {
    m_nextStateId = 0;
    m_shipPos.setZero();
    m_workingPos = pos();
    m_intensity = ExplosionIntensity::ExplosionLoseItem;

    resize(BLAST_RADIUS, 0.0f);

    const auto &flowTable = ObjectDirector::Instance()->flowTable();
    m_blastRadiusRatio = BLAST_RADIUS /
            static_cast<f32>(parse<s16>(flowTable.set(flowTable.slot(id()))->params.sphere.radius));
}

/// @addr{0x806D0880}
Kart::Reaction ObjectHeyhoBall::onCollision(Kart::KartObject * /*kartObj*/,
        Kart::Reaction reactionOnKart, Kart::Reaction reactionOnObj,
        EGG::Vector3f &hitDepth) {
    // USA806ea760: ObjectsMgr +55 selects the TT/Ghost branch only.
    using Mode = System::RaceConfig::Scenario::GameMode;
    const auto mode = System::RaceConfig::Instance()->raceScenario().gameMode;
    if (mode == Mode::Time_Trial || mode == Mode::Ghost_Race) {
        if (m_currentStateId == 3) {
            if (m_intensity == ExplosionIntensity::ExplosionLoseItem) {
                return Kart::Reaction::ExplosionLoseItem;
            }
            hitDepth.setZero();
            return Kart::Reaction::SpinSomeSpeed;
        }
        return Kart::Reaction::WallAllSpeed;
    }
    // Ordinary races preserve the collision-table reaction, including immunity.
    if (m_intensity == ExplosionIntensity::SpinSomeSpeed) {
        if (reactionOnKart == Kart::Reaction::ExplosionLoseItem) {
            reactionOnKart = Kart::Reaction::SpinSomeSpeed;
        }
        hitDepth.setZero();
    }
    if (m_currentStateId != 3 &&
            (static_cast<s16>(reactionOnObj) == 3 || static_cast<s16>(reactionOnObj) == 5)) {
        m_nextStateId = 3;
    }
    return reactionOnKart;
}

// USA806ea858..806ea8dc: object reactions 3..6 request the existing explosion;
// an already exploding ball must not restart its 46-frame lifetime.
void ObjectHeyhoBall::reactToItem(s16 reactionOnObj) {
    if (reactionOnObj >= 3 && reactionOnObj <= 6 && m_currentStateId != 3) {
        m_nextStateId = 3;
    }
}

s16 ObjectHeyhoBall::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
        s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) {
    reactToItem(reactionOnObj);
    return reactionOnItem;
}

void ObjectHeyhoBall::onGlobalItemCollision(s16, s16 reactionOnObj) {
    reactToItem(reactionOnObj);
}

/// @addr{0x806D10A4}
void ObjectHeyhoBall::initProjectile(const EGG::Vector3f &pos) {
    m_shipPos = pos;
    m_xzDir = (m_initPos + EGG::Vector3f::ey * -BALL_RADIUS) - m_shipPos;
    m_xzDir.y = 0.0f;
    m_xzDir.normalise2();

    EGG::Vector2f xzDist = EGG::Vector2f(m_shipPos.x - m_initPos.x, m_shipPos.z - m_initPos.z);
    m_xzSpeed = EGG::Mathf::sqrt(xzDist.dot()) / m_airtime;
    m_yDist = m_initPos.y + -BALL_RADIUS - m_shipPos.y;
    m_initYSpeed = m_yDist / m_airtime + 0.5f * 4.0f * m_airtime;
}

/// @addr{0x806D0A3C}
void ObjectHeyhoBall::enterFalling() {
    if (!getUnit()) {
        loadAABB(0.0f);
        resize(BLAST_RADIUS, 0.0f);
    }
}

/// @addr{0x806D0AD8}
void ObjectHeyhoBall::calcFalling() {
    cueSound(0x333, SoundCall::StartNew); // SE_CR_GBH_CANNON_MOV
    f32 currentHeight = m_workingPos.y + (m_initYSpeed - 4.0f * static_cast<f32>(m_currentFrame));
    if (currentHeight > m_initPos.y + -BALL_RADIUS) {
        m_workingPos.y = currentHeight;
        m_workingPos.x += m_xzDir.x * m_xzSpeed;
        m_workingPos.z += m_xzDir.z * m_xzSpeed;
    } else {
        m_nextStateId = 2;
        m_workingPos = (m_workingPos + m_initPos + EGG::Vector3f::ey * -BALL_RADIUS) * 0.5f;
    }
}

/// @addr{0x806D0F24}
void ObjectHeyhoBall::calcExploding() {
    constexpr u32 EXPLODE_FRAMES = 46;
    constexpr EGG::Vector3f BALL_SCALE = EGG::Vector3f(1.001f, 1.001f, 1.001f);

    /// Colliding after this frame does not cause player to launch upwards and lose item
    constexpr u32 SPIN_FRAME = 32;

    if (m_currentFrame >= EXPLODE_FRAMES) {
        setScale(BALL_SCALE);

        if (getUnit()) {
            unregisterCollision();
        }

        m_nextStateId = 0;
        m_intensity = ExplosionIntensity::ExplosionLoseItem;
    } else {
        f32 shrinkFrames = static_cast<f32>(m_currentFrame) - 40.0f;
        f32 scale = 1.2f * m_blastRadiusRatio + -m_scaleChangeRate * shrinkFrames * shrinkFrames;

        scale = std::min(m_blastRadiusRatio, scale);
        setScale(scale);

        m_intensity = m_currentFrame >= SPIN_FRAME ? ExplosionIntensity::SpinSomeSpeed :
                                                     ExplosionIntensity::ExplosionLoseItem;
    }
}

} // namespace Kinoko::Field
