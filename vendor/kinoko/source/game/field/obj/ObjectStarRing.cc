#include "ObjectStarRing.hh"

#include "game/kart/KartObject.hh"
#include "game/kart/KartObjectManager.hh"

#include "game/system/RaceConfig.hh"

namespace Kinoko::Field {

/// @addr{0x806BFB14}
ObjectStarRing::ObjectStarRing(const System::MapdataGeoObj &params)
    : ObjectCollidable(params), m_index(s_instanceCount++), m_shotLength(0.0f), m_shot(false),
      m_frame(0.0f), m_window(0), m_count(0) {}

/// @addr{0x806BFD90}
ObjectStarRing::~ObjectStarRing() {
    s_instanceCount = 0;
}

/// @addr{0x806BFDDC}
/// @brief The shot rests on frame 0 and StarRing_wait plays; the StarRing SRT0 plays from load.
void ObjectStarRing::init() {
    auto *anmMgr = m_drawMdl ? m_drawMdl->anmMgr() : nullptr;
    if (anmMgr) {
        anmMgr->playAnim(0.0f, 1.0f, 0);
        m_shotLength = anmMgr->activeAnim(Render::AnmType::Chr)->frameCount();
    }

    m_shot = false;
    m_frame = 0.0f;
}

/// @addr{0x806BFED0}
void ObjectStarRing::calc() {
    constexpr u16 COUNT_MAX = 3;
    constexpr u16 WINDOW = 0x50;

    m_soundCalls = SoundCalls{};

    const auto &scenario = System::RaceConfig::Instance()->raceScenario();
    auto *karts = Kart::KartObjectManager::Instance();
    bool cannonStart = false;

    for (size_t i = 0; i < scenario.playerCount && i < m_soundCalls.kartCount.size(); ++i) {
        bool started = karts->object(i)->status().onBit(Kart::eStatus::CannonStart);

        // KartActor +0xE0, the local racer (as race_audio_frame.cpp's RaceAudioKart::local: the
        // ghost driving player 0 in Watch the Replay).
        const auto type = scenario.players[i].type;
        bool local = type == System::RaceConfig::Player::Type::Local ||
                (i == 0 && type == System::RaceConfig::Player::Type::Ghost);

        if (started && local) {
            m_soundCalls.karts |= static_cast<u16>(1u << i);
            m_soundCalls.kartCount[i] = static_cast<u8>(m_count);

            if (m_count < COUNT_MAX) {
                ++m_count;
            }

            m_window = WINDOW;
        }

        cannonStart |= started;
    }

    // Only the first cannon-class object built plays its shot (+0xE8 == 0).
    bool resting = m_index == 0 && (!m_shot || m_frame == 0.0f);
    if (cannonStart && resting) {
        if (!m_shot) {
            m_shot = true;
            m_frame = 0.0f;
        }

        // Retail skips the sound in sections 0x32-0x34 (Audio +0x10), none of them a race.
        m_soundCalls.ring = true;
        m_soundCalls.ringCount = static_cast<u8>(m_count);

        if (m_count < COUNT_MAX) {
            ++m_count;
        }

        m_window = WINDOW;
    }

    if (m_shot && m_frame >= m_shotLength - 1.0f) {
        m_shot = false;
        m_frame = 0.0f;
    }

    if (m_window == 0) {
        m_count = 0;
    } else {
        --m_window;
    }

    m_frame += 1.0f;
}

/// @addr{0x806BFC48}
void ObjectStarRing::loadAnims() {
    std::array<const char *, 2> names = {{
            "StarRing_shot",
            "StarRing_wait",
    }};

    std::array<Render::AnmType, 2> types = {{
            Render::AnmType::Chr,
            Render::AnmType::Chr,
    }};

    linkAnims(names, types);
}

u8 ObjectStarRing::s_instanceCount = 0;

} // namespace Kinoko::Field
