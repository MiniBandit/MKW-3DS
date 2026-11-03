#include "ObjectLeafEffect.hh"
#include "game/kart/KartObject.hh"
#include "game/system/RaceConfig.hh"
namespace Kinoko::Field {
ObjectLeafEffect::ObjectLeafEffect(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}
void ObjectLeafEffect::init() {
    m_touched = m_previous = false; m_effectSuppressed = true; m_serial = {}; m_player = {};
}
void ObjectLeafEffect::calc() {
    // Objects update before kart contacts, matching EffectSpawner's b0/b1 latch.
    m_previous = m_touched; m_touched = false;
}
Kart::Reaction ObjectLeafEffect::onCollision(Kart::KartObject *kart, Kart::Reaction result,
        Kart::Reaction, EGG::Vector3f &depth) {
    depth = EGG::Vector3f::zero; m_touched = true;
    if (m_previous) return result;
    const auto player = kart->param()->playerIdx();
    const auto &scenario = System::RaceConfig::Instance()->raceScenario();
    if (!m_effectSuppressed && player < scenario.playerCount && scenario.players[player].type == System::RaceConfig::Player::Type::Local) {
        // Original KartState+8 bit4 (InCannon) selects rk_leaf_effect;
        // the ordinary driving branch selects rk_leaf_effectDash.
        const unsigned kind = kart->status().onBit(Kart::eStatus::InCannon) ? 0 : 1;
        ++m_serial[kind]; m_player[kind] = player;
    }
    cueSound(0x2e4, SoundCall::Start);
    return result;
}
}
