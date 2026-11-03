#include "ObjectCannon.hh"
#include "game/field/ObjectDirector.hh"
#include <cstring>

namespace Kinoko::Field {
bool ObjectCannon::matches(const char *name) {
    return !std::strcmp(name, "DonkyCannon_wii") || !std::strcmp(name, "DonkyCannonGC") ||
            !std::strcmp(name, "tree_cannon") || !std::strcmp(name, "StarRing");
}
void ObjectCannon::load() { ObjectDirector::Instance()->addObject(this); }
void ObjectCannon::init() {
    m_frame = 0.0f;
    m_starRing = !std::strcmp(getName(), "StarRing");
    m_playing = m_pending = false;
    m_launchSerial = 0;
    m_sound = {};
}
void ObjectCannon::activateFirst() {
    auto *director = ObjectDirector::Instance();
    if (!director) return;
    // USA806bb5ac: the first cannon instance responds to CannonStart.
    // Additional instances are explicitly activated by their own source events.
    for (auto *object : director->objects()) {
        const auto *flow = director->flowTable().set(director->flowTable().slot(object->id()));
        if (flow && matches(flow->name)) {
            static_cast<ObjectCannon *>(object)->activate();
            return;
        }
    }
}
void ObjectCannon::activate() {
    // A volley of racers does not restart an already running recoil clip.
    if (!m_playing) m_pending = true;
}
void ObjectCannon::calc() {
    const char *name = getName();
    if (m_pending) {
        m_pending = false;
        m_playing = true;
        m_frame = 0.0f;
        m_sound = {};
        ++m_launchSerial;
        if (m_starRing) cueSound(0x2f9, SoundCall::Start);
    } else if (m_playing || m_starRing) {
        ++m_frame;
    }
    if (!m_playing) return;
    if (!m_starRing) m_sound.update(*this, name, name, m_frame);
    // USA806bb844: rewind at the last frame; never loop the firing animation.
    // Original StarRing_shot CHR0 has40frames; wait has60 and loops.
    const f32 frames = m_starRing ? 40.0f : f32(mkw::ObjectAnimationSound::frames(name, name));
    if (m_frame >= frames - 1.0f) {
        m_frame = 0.0f;
        m_playing = false;
    }
}
}
