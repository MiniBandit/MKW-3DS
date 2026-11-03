#include "ObjectBGTeresa.hh"
#include "game/field/ObjectDirector.hh"
#include "game/system/RaceManager.hh"
#include <cmath>
namespace Kinoko::Field {
void ObjectBGTeresa::load() { ObjectDirector::Instance()->addObject(this); }
void ObjectBGTeresa::init() {
    // USA8076e354: random initial/repeat delay and independent texture mirroring.
    auto &random = System::RaceManager::Instance()->random();
    m_origin = pos();
    m_motionAngle = m_alphaAngle = 0;
    m_state = State::Hidden;
    m_alpha = 0;
    m_delay = m_countdown = random.getU32(180);
    m_mirrorU = random.getU32(2) == 0;
}
void ObjectBGTeresa::appear() {
    // USA8076ede0: all six offsets are independent; the motion phase continues.
    auto &random = System::RaceManager::Instance()->random();
    m_offset = {random.getF32(2000) - 1000, random.getF32(2000) - 1000,
            random.getF32(2000) - 1000};
    m_amplitude = {random.getF32(2000) - 1000, random.getF32(2000) - 1000,
            random.getF32(2000) - 1000};
    m_lifetime = 180 + random.getU32(240);
    m_state = State::FadeIn;
}
void ObjectBGTeresa::calc() {
    // USA8076e4d8: update once per simulation tick, independent of stereo draws.
    if (m_state == State::Hidden) {
        if (--m_countdown == 0) appear();
        return;
    }
    switch (m_state) {
    case State::FadeIn:
        m_alpha += 5;
        if (m_alpha > 250) {
            m_alpha = 255;
            m_state = State::Visible;
            m_alphaAngle = 0;
            m_alphaSpeed = 1 + System::RaceManager::Instance()->random().getF32(2);
        }
        break;
    case State::Visible:
        m_alphaAngle += m_alphaSpeed;
        if (m_alphaAngle > 360) m_alphaAngle = 0;
        m_alpha = u8(191 + 64 * std::sin(m_alphaAngle * 0.01745329252f));
        break;
    case State::FadeOut:
        m_alpha -= 5;
        if (m_alpha < 5) {
            m_alpha = 0;
            m_state = State::Hidden;
            m_countdown = m_delay;
        }
        break;
    case State::Hidden:
        break;
    }
    m_motionAngle += 1;
    if (m_motionAngle > 360) m_motionAngle = 0;
    setPos(m_origin + m_offset + m_amplitude * std::sin(m_motionAngle * 0.01745329252f));
    if (--m_lifetime == 0) m_state = State::FadeOut;
}
}
