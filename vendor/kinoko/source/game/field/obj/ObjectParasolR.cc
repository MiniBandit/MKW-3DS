#include "ObjectParasolR.hh"
#include "game/field/CollisionDirector.hh"
#include "game/kart/KartObjectManager.hh"
#include "game/system/RaceConfig.hh"
#include <algorithm>
#include <cmath>

namespace Kinoko::Field {

/// @addr{0x8077902C}
ObjectParasolR::ObjectParasolR(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), m_initialPos(params.pos()) {}

/// @addr{0x80779EFC}
ObjectParasolR::~ObjectParasolR() = default;

void ObjectParasolR::loadAnims() {
    std::array<const char *, 2> names = {"close", "open"};
    std::array<Render::AnmType, 2> types = {Render::AnmType::Chr, Render::AnmType::Chr};
    linkAnims(names, types);
}

// USA8076459c initializes setting1==0 to the open endpoint. The main
// model uses SHP0, while its shadow uses the corresponding CHR0 clips.
void ObjectParasolR::init() {
    setPos(m_initialPos);
    m_open = m_mapObj->setting(1) == 0;
    m_frame = 20.0f; m_velocityY = 0.0f; m_lastPlayer = -1; m_bounces = 0;
    m_armed = true; m_airborne = false;
    m_drawMdl->anmMgr()->playAnim(m_frame, 0.0f, m_open ? 1 : 0);
}

s32 ObjectParasolR::nearbyPlayer() const {
    const auto *manager = Kart::KartObjectManager::Instance();
    if (!manager) return -1;
    // Original8077e7e4 returns the first player inside the600-unit sphere.
    for (u32 i = 0; i < manager->count(); ++i)
        if ((manager->object(i)->pos() - pos()).squaredLength() < 600.0f * 600.0f)
            return static_cast<s32>(i);
    return -1;
}

void ObjectParasolR::toggle(s32 player) {
    if (!m_armed || player < 0 || m_frame < 20.0f) return;
    m_open = !m_open; m_frame = 0.0f;
    m_lastPlayer = player; m_armed = false;
    m_drawMdl->anmMgr()->playAnim(0.0f, 1.0f, m_open ? 1 : 0);
    cueSound(m_open ? 0x328 : 0x329, SoundCall::Start);
}

void ObjectParasolR::jump() {
    m_airborne = true; m_velocityY = 100.0f; m_bounces = 2;
}

void ObjectParasolR::react(s16 reactionOnObj, s32 player) {
    if (reactionOnObj == 4 || reactionOnObj == 6) jump();
    if (reactionOnObj >= 3 && reactionOnObj <= 6) toggle(player);
}

Kart::Reaction ObjectParasolR::onCollision(Kart::KartObject *kart,
        Kart::Reaction reactionOnKart, Kart::Reaction reactionOnObj, EGG::Vector3f &) {
    // USA80764c98 excludes a ghost's ordinary hit unless IsTTReplay:
    // mode2/5, at least one player, and player0 itself a ghost.
    const auto reaction = static_cast<s16>(reactionOnObj);
    if (reaction == 3 || reaction == 5) {
        const auto &s = System::RaceConfig::Instance()->raceScenario();
        const auto ghost = System::RaceConfig::Player::Type::Ghost;
        const auto mode = static_cast<unsigned>(s.gameMode);
        const bool replay = (mode == 2 || mode == 5) && s.playerCount && s.players[0].type == ghost;
        if (s.players[kart->param()->playerIdx()].type == ghost && !replay) return reactionOnKart;
    }
    react(reaction, kart->param()->playerIdx());
    return reactionOnKart;
}

s16 ObjectParasolR::onItemCollision(const EGG::Vector3f &, const EGG::Vector3f &,
        s16 reactionOnItem, s16 reactionOnObj, EGG::Vector3f &) {
    // The original item branch stores12 as its last player, regardless of owner.
    react(reactionOnObj, 12); return reactionOnItem;
}

void ObjectParasolR::onGlobalItemCollision(s16, s16 reactionOnObj) {
    react(reactionOnObj, 12);
}

void ObjectParasolR::calc() {
    if (nearbyPlayer() != m_lastPlayer) m_armed = true;
    m_frame = std::min(20.0f, m_frame + 1.0f);
    if (!m_airborne) return;
    // Original807646e8 /80764930: vertical100, gravity2, radius100,
    // restitution.2 and stop below.2. Only the first two landings sound.
    m_velocityY -= 2.0f;
    addPos(EGG::Vector3f(0.0f, m_velocityY, 0.0f));
    CollisionInfo info; info.reset();
    if (!CollisionDirector::Instance()->checkSphereFull(100.0f,
            pos() + EGG::Vector3f(0.0f, 100.0f, 0.0f), EGG::Vector3f::inf,
            KCLTypeMask(0x20e80fff), &info, nullptr, 0)) return;
    addPos(info.tangentOff);
    m_velocityY *= -0.2f;
    if (std::fabs(m_velocityY) < 0.2f) m_airborne = false;
    if (m_bounces > 0) cueSound(m_bounces-- == 2 ? 0x32b : 0x32c, SoundCall::Start);
}

} // namespace Kinoko::Field
