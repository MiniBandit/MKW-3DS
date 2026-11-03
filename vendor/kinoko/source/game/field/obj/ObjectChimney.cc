#include "ObjectChimney.hh"
#include "game/field/ObjectDirector.hh"
#include "game/kart/KartObject.hh"
#include "game/system/RaceConfig.hh"
#include <algorithm>
namespace Kinoko::Field {
ObjectChimney::ObjectChimney(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), m_wide(params.id() == 0x2c0),
      m_variant(params.setting(1)), m_delay(u32(s32(s16(params.setting(0))) * 60)) {
    if (m_wide) {
        const auto &flow = ObjectDirector::Instance()->flowTable();
        m_center.y = parse<s16>(flow.set(flow.slot(id()))->params.cylinder.height);
    }
}
void ObjectChimney::loadGraphics() {
    // W parameters1/2 use particles only; parameter0 keeps the original SRT model.
    if (!m_wide || m_variant == 0) ObjectBase::loadGraphics();
}
void ObjectChimney::createCollision() {
    // The original internal mode is1 for BOTH nonzero W settings, not just raw1.
    if (!m_wide || m_variant == 1 || m_variant == 2) ObjectCollidable::createCollision();
}
void ObjectChimney::init() {
    m_tick = 0; m_hitTicks = 10; m_hits = {};
    m_touched = m_previous = false; m_effectSuppressed = true; m_pauseTrack = false;
    m_modelVisible = m_variant == 0 || (!m_wide && m_variant == 1);
    m_emitting = m_wide ? m_variant != 0 : m_variant == 1;
    if (m_collision) {
        if (!m_wide && (m_variant == 2 || m_variant == 4)) disableCollision();
        else enableCollision();
    }
}
void ObjectChimney::calc() {
    if (m_wide) {
        if (m_touched && !m_previous) m_tick = 0;
        m_previous = m_touched; m_touched = false;
        if (m_variant != 0) {
            m_emitting = true;
            if (m_tick == 0) m_pauseTrack = true;
            else if (m_tick == 6) m_pauseTrack = false;
            ++m_tick;
        }
        return;
    }
    if (m_variant == 1) { m_modelVisible = false; m_emitting = true; }
    else if (m_variant == 2 || m_variant == 4) {
        if (m_tick == m_delay && m_collision) enableCollision();
        m_emitting = m_tick >= m_delay;
        if (m_tick > m_delay) {
            if (m_hitTicks == 0) m_pauseTrack = true;
            else if (m_hitTicks == 6) m_pauseTrack = false;
        }
    } else if (m_variant == 3) m_emitting = true;
    // Retail caps the general counter at2000000 and the recent-hit counter at7.
    m_tick = std::min<u32>(m_tick + 1, 2000000);
    if (m_hitTicks <= 6) ++m_hitTicks;
}
void ObjectChimney::calcCollisionTransform() {
    if (!m_collision) return;
    if (m_wide) { ObjectCollidable::calcCollisionTransform(); return; }
    calcTransform();
    auto matrix = transform();
    // USA806cee48 constant808bfbf4: world Y, independent of object scale/rotation.
    matrix.setBase(3, pos() + EGG::Vector3f::ey * 2000.0f);
    m_collision->transform(matrix, scale(), getCollisionTranslation());
}
Kart::Reaction ObjectChimney::onCollision(Kart::KartObject *kart, Kart::Reaction result,
        Kart::Reaction, EGG::Vector3f &depth) {
    depth = EGG::Vector3f::zero;
    const auto player = kart->param()->playerIdx();
    const auto &scenario = System::RaceConfig::Instance()->raceScenario();
    if (m_wide) {
        m_touched = true;
        if (!m_effectSuppressed && !m_previous && player < scenario.playerCount &&
                scenario.players[player].type == System::RaceConfig::Player::Type::Local)
            ++m_hits[player];
    } else if (!m_effectSuppressed && player < scenario.playerCount &&
            scenario.players[player].type != System::RaceConfig::Player::Type::Ghost &&
            EGG::Mathf::abs(kart->speed()) > 10.0f) {
        ++m_hits[player]; m_hitTicks = 0;
    }
    return result;
}
}
