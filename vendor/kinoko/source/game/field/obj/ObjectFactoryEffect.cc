#include "ObjectFactoryEffect.hh"
#include "game/field/ObjectDirector.hh"
#include <cstring>
namespace Kinoko::Field {
void ObjectFactoryEffect::load() { ObjectDirector::Instance()->addObject(this); }
void ObjectFactoryEffect::init() {
    m_steam = !std::strcmp(getName(), "Steam");
    m_active = false;
    m_emitting = !m_steam;
    // USA8077dc0c: setting1 is the first delay; zero uses the repeating delay.
    m_timer = s16(mapObj()->setting(1));
    if (!m_timer) m_timer = s16(mapObj()->setting(2));
}
void ObjectFactoryEffect::calc() {
    // Alarm (USA8076e068) maintains the authored particle effect at its matrix.
    if (!m_steam) { m_emitting = true; return; }
    m_emitting = m_active;
    if (!m_active) {
        // USA8077dd20: transition occurs below zero, emission begins next tick.
        if (--m_timer < 0) { m_active = true; m_timer = s16(mapObj()->setting(3)); }
        return;
    }
    // USA8077ddf0: zero repeating delay leaves the steam continuously enabled.
    const s32 pause = s16(mapObj()->setting(2));
    if (!pause) return;
    if (--m_timer < 0) { m_active = false; m_timer = pause; }
    cueSound(0x2a5, SoundCall::Hold);
}
}
