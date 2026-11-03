#include "ObjectKoopaFigure.hh"
#include "game/field/ObjectDirector.hh"
#include "game/system/RaceConfig.hh"

namespace Kinoko::Field {
void ObjectKoopaFigure::load() {
    // Geometry/CHR0 live in ObjectAssets; only register the simulation actor.
    ObjectDirector::Instance()->addObject(this);
}
s32 ObjectKoopaFigure::cycleFrames() {
    const auto mode = System::RaceConfig::Instance()->raceScenario().gameMode;
    using Mode = System::RaceConfig::Scenario::GameMode;
    // USA 8075f3a4: ObjectsMgr +55 is Time Trial/Ghost Race.
    return mode == Mode::Time_Trial || mode == Mode::Ghost_Race ? 210 : 270;
}
void ObjectKoopaFigure::init() {
    // USA 8075f26c: initial fireball delay 221 minus mouth preparation 42.
    m_cooldown = 221 - 42;
    m_breathSerial = 0;
    m_vomiting = false;
    m_frame = -1.0f;
    m_sound = {};
}
void ObjectKoopaFigure::calc() {
    ++m_frame;
    // USA 8075f354: the statue owns a timer independent of the travelling ball.
    if (!m_vomiting && m_cooldown < 0) {
        m_vomiting = true;
        ++m_breathSerial;
        m_cooldown = cycleFrames();
        m_frame = 0.0f;
    } else if (m_vomiting && m_frame >= mkw::ObjectAnimationSound::frames("koopaFigure", "vomit")) {
        m_vomiting = false;
        m_frame = 0.0f;
    }
    const char *clip = m_vomiting ? "vomit" : "wait";
    m_sound.update(*this, "koopaFigure", clip, m_frame,
            m_vomiting ? 0.0f : f32(mkw::ObjectAnimationSound::frames("koopaFigure", "wait")));
    --m_cooldown;
}
}
