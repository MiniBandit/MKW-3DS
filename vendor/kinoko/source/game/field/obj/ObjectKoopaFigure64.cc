#include "game/field/obj/ObjectKoopaFigure64.hh"

#include "game/field/ObjectDirector.hh"

#include "game/system/RaceManager.hh"
#include "game/system/RaceConfig.hh"
#include "game/kart/KartObjectManager.hh"
#include "game/kart/KartObject.hh"

namespace Kinoko::Field {

/// @addr{0x806DA914}
ObjectKoopaFigure64::ObjectKoopaFigure64(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), m_isBigStatue(params.setting(1) == 1),
      m_startDelay(static_cast<u32>(params.setting(2))) {}

/// @addr{0x806DB114}
ObjectKoopaFigure64::~ObjectKoopaFigure64() = default;

/// @addr{0x806DAA44}
void ObjectKoopaFigure64::init() {
    constexpr f32 BIG_SCALE = 20.0f;

    if (m_isBigStatue) {
        const auto &flowTable = ObjectDirector::Instance()->flowTable();
        const auto *collisionSet = flowTable.set(flowTable.slot(id()));
        ASSERT(collisionSet);
        s16 radius = parse<s16>(collisionSet->params.cylinder.radius);
        resize(BIG_SCALE * static_cast<f32>(radius), 0.0f);
    }

    m_cycleFrame = FIRE_DURATION + COOLDOWN_DURATION;

    if (m_isBigStatue) {
        disableCollision();
    }
}

/// @addr{0x806DAB5C}
void ObjectKoopaFigure64::calc() {
    u32 timer = System::RaceManager::Instance()->timer();
    if (timer < m_startDelay) {
        return;
    }

    // RMCU806ecd58..ed030: normal races restart only when the sampled
    // kart is within setting0 in XZ; TT/Ghost cycles independently of proximity.
    using Mode = System::RaceConfig::Scenario::GameMode;
    const auto mode = System::RaceConfig::Instance()->raceScenario().gameMode;
    const bool independent = mode == Mode::Time_Trial || mode == Mode::Ghost_Race;
    auto *karts = Kart::KartObjectManager::Instance();
    bool inRange = false;
    if (karts && karts->count()) {
        const auto delta = karts->object(timer % karts->count())->pos() - pos();
        const f32 range = static_cast<f32>(m_mapObj->setting(0));
        inRange = delta.x * delta.x + delta.z * delta.z < range * range;
    }
    m_fireActive = m_cycleFrame < FIRE_DURATION;
    if (m_cycleFrame == CYCLE_DURATION && (independent || inRange)) {
        m_cycleFrame = 0;
        if (m_isBigStatue) enableCollision();
    }
    if (m_cycleFrame >= FIRE_DURATION && m_isBigStatue) disableCollision();
    if (m_fireActive) cueSound(0x35d, SoundCall::Hold); // SE_CR_64K_FIGURE_FRAME
    if (m_cycleFrame < CYCLE_DURATION) ++m_cycleFrame;
}

/// @addr{0x806DAFB8}
void ObjectKoopaFigure64::calcCollisionTransform() {
    constexpr EGG::Vector3f FIRE_POS_OFFSET = EGG::Vector3f(0.0f, -280.0f, 4000.0f);

    calcTransform();
    EGG::Matrix34f rotMat = transform();
    rotMat.setBase(3, EGG::Vector3f::zero);

    EGG::Matrix34f targetMat = EGG::Matrix34f::ident;
    targetMat.setBase(3, FIRE_POS_OFFSET);

    EGG::Matrix34f posMat = EGG::Matrix34f::ident;
    posMat.setBase(3, pos());

    m_collision->transform(posMat.multiplyTo(rotMat.multiplyTo(targetMat)), scale());
}

} // namespace Kinoko::Field
