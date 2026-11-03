#include "RaceManager.hh"
#include <mkw/battle_spawn.hpp>

#include "game/system/CourseMap.hh"
#include "game/system/KPadDirector.hh"
#include "game/system/RaceConfig.hh"
#include "game/system/map/MapdataCheckPath.hh"
#include "game/system/map/MapdataStartPoint.hh"

#include "game/kart/KartObjectManager.hh"
#include "game/kart/KartState.hh"
#include "game/field/ObjectDirector.hh"
#include <cmath>

namespace Kinoko::System {

/// @addr{0x80532F88}
void RaceManager::init() {
    for (size_t i = 0; i < m_playerCount; ++i) m_players[i].init(i);
    if (RaceConfig::Instance()->raceScenario().gameMode == RaceConfig::Scenario::GameMode::Battle)
        initBattleRespawns();
}

/// @addr{0x805362DC}
void RaceManager::findKartStartPoint(EGG::Vector3f &pos, EGG::Vector3f &angles, size_t index) {
    ASSERT(index < m_playerCount);
    u32 placement = index + 1;
    u32 playerCount = m_playerCount;
    u32 startPointIdx = 0;
    const auto &scenario = RaceConfig::Instance()->raceScenario();
    // RaceConfig::Scenario::initPlayers uses count-index on the first offline
    // race; Scenario::update carries finish positions to the next race.
    const unsigned previous = scenario.players[index].startPosition;
    if (scenario.gameMode == RaceConfig::Scenario::GameMode::Grand_Prix ||
            scenario.gameMode == RaceConfig::Scenario::GameMode::Versus)
        placement = previous >= 1 && previous <= playerCount ? previous : playerCount - index;
    // Retail USA80531908..80531938: awards start each kart at its
    // own KTPT, without ordinary starting-grid offsets. Camera mode 12
    // has six slots; other award cameras have three. This is required
    // before running the real kart simulation in winningrun_demo.
    if (scenario.gameMode == RaceConfig::Scenario::GameMode::Battle) {
        // Original Battle uses authored team slots, with no race grid offsets.
        placement = playerCount = 1;
        auto* map = CourseMap::Instance();
        if (map->getStartPointCount() > 1) {
            unsigned earlierTeammates = 0;
            const unsigned team = scenario.players[index].team;
            for (size_t i = 0; i < index; ++i) if (scenario.players[i].team == team) ++earlierTeammates;
            const unsigned slot = mkw::battleStartSlot(team,earlierTeammates,m_battleStartOffset);
            for (u16 i = 0; i < map->getStartPointCount(); ++i) {
                if (map->getStartPoint(i)->playerIndex() == static_cast<s16>(slot)) { startPointIdx = i; break; }
            }
        }
    } else if (scenario.gameMode == RaceConfig::Scenario::GameMode::Awards) {
        // Ending has twelve individually authored delayed KTPT entries.
        const u32 slots = scenario.course == Course::Ending_Demo ? 12 : scenario.cameraMode == 12 ? 6 : 3;
        startPointIdx = index < slots ? static_cast<u32>(index) : 0;
        placement = playerCount = 1;
    }

    MapdataStartPoint *kartpoint = CourseMap::Instance()->getStartPoint(startPointIdx);

    if (kartpoint) {
        kartpoint->findKartStartPoint(pos, angles, placement - 1, playerCount);
    } else {
        pos.setZero();
        angles = EGG::Vector3f::ex;
    }
}

/// @addr{0x80533C6C}
void RaceManager::endPlayerRace(u32 idx) {
    ASSERT(idx < m_playerCount);
    if (m_players[idx].finishOrder() != 0) return;
    m_players[idx].setFinishOrder(++m_finishCount);
    if (m_finishCount == m_playerCount) m_stage = Stage::FinishGlobal;
    else if (idx == 0) m_stage = Stage::FinishLocal;
}

bool RaceManager::endBattle(const std::array<u8,12>& order) {
    if(RaceConfig::Instance()->raceScenario().gameMode!=RaceConfig::Scenario::GameMode::Battle ||
            m_stage!=Stage::Race || m_playerCount>12)return false;
    u16 seen=0;
    for(size_t i=0;i<m_playerCount;++i){
        if(order[i]>=m_playerCount || (seen&(1u<<order[i])))return false;
        seen|=u16(1u<<order[i]);
    }
    const Timer finishTime=m_timerManager.currentTimer();
    for(size_t i=0;i<m_playerCount;++i)m_players[order[i]].endRace(finishTime);
    m_timerManager.setStarted(false);
    return true;
}

/// @addr{0x805331B4}
void RaceManager::calc() {
    constexpr u16 STAGE_INTRO_DURATION = 172;

    m_timerManager.calc();
    for (size_t i = 0; i < m_playerCount; ++i) m_players[i].calc();

    if (RaceConfig::Instance()->raceScenario().gameMode == RaceConfig::Scenario::GameMode::Battle)
        calcBattleRespawns();

    // Retail USA8052e8c4..e900: Awards has cutSceneMode and bypasses
    // the intro-camera wait. USA8052e9d0..ea20 gives modes 11/12 a zero
    // countdown. Keep the two separate stage transitions (not Race at init).
    const bool awards = RaceConfig::Instance()->raceScenario().gameMode ==
            RaceConfig::Scenario::GameMode::Awards;
    switch (m_stage) {
    case Stage::Intro:
        if ((awards || !m_introHeld) && (++m_introTimer >= STAGE_INTRO_DURATION || awards)) {
            m_stage = Stage::Countdown;
            KPadDirector::Instance()->startGhostProxies();
        }
        break;
    case Stage::Countdown:
        if (++m_timer >= STAGE_COUNTDOWN_DURATION || awards) {
            m_timerManager.setStarted(true);
            m_stage = Stage::Race;
        }
        break;
    case Stage::Race:
    case Stage::FinishLocal:
        ++m_timer;
        break;
    default:
        break;
    }
}

/// @addr{0x8053621C}
MapdataJugemPoint *RaceManager::jugemPoint(size_t index) {
    auto* map = CourseMap::Instance();
    if (RaceConfig::Instance()->raceScenario().gameMode == RaceConfig::Scenario::GameMode::Battle) {
        // USA8053469c: shared round-robin cursor, advancing past unavailable
        // groups for every kind of recovery (water, stuck and empty balloons).
        const u16 count = map->getJugemPointCount();
        for (u16 step = 1; step <= count; ++step) {
            const u16 candidate = (m_battleJugemCursor + step) % count;
            auto* point = map->getJugemPoint(candidate);
            const s16 group = point->battleGroup();
            if (group < 0 || (group < 32 && (m_battleRespawnGroups & (1u << group)))) {
                m_battleJugemCursor = candidate;
                return point;
            }
        }
        return map->getJugemPoint(0); // original no-eligible-point fallback
    }
    s8 jugemId = std::max<s8>(player(index).jugemId(), 0);
    return System::CourseMap::Instance()->getJugemPoint(static_cast<u16>(jugemId));
}

// GMData Battle init, USA805344a4..6690. Heights are authored JGPT positions,
// not a guessed global height offset applied to every respawn.
void RaceManager::initBattleRespawns() {
    m_battleJugemCursor = m_battleGroupMax = m_battleGroupCursor = 0;
    m_battleRespawnGroups = 1;
    m_battleWaterHeight = -1.f;
    m_battleGroupMin.fill(100000000.f);
    m_battleGroupMaxY.fill(-9999999.f);
    auto* map = CourseMap::Instance();
    for (u16 i = 0; i < map->getJugemPointCount(); ++i) {
        const auto* point = map->getJugemPoint(i);
        const s16 group = point->battleGroup();
        if (group < 0) continue;
        m_battleGroupMax = std::max(m_battleGroupMax, static_cast<u16>(group));
        m_battleGroupMin[group] = std::min(m_battleGroupMin[group], point->pos().y);
        m_battleGroupMaxY[group] = std::max(m_battleGroupMaxY[group], point->pos().y);
    }
}

void RaceManager::calcBattleRespawns() {
    // USA80534af8..4c14 / 80538bc4..8cdc: one group per tick and 15 idle
    // slots, with min/max hysteresis. Both offline Battle types share this.
    if (m_battleWaterHeight >= 0.f) {
        if (m_battleGroupCursor <= m_battleGroupMax) {
            const u16 g = m_battleGroupCursor;
            if (m_battleWaterHeight < m_battleGroupMin[g]) setBattleRespawnGroup(g, true);
            else if (m_battleWaterHeight > m_battleGroupMaxY[g]) setBattleRespawnGroup(g, false);
        }
        if (++m_battleGroupCursor > m_battleGroupMax + 15) m_battleGroupCursor = 0;
    }
    // Raceinfo caches tide AFTER the mode update (8052e6e0..e798).
    // Sample every16 ticks only after a change exceeding1.1*80 (wave size),
    // so individual waves do not repeatedly flip the respawn layer mask.
    const auto* objects = Field::ObjectDirector::Instance();
    if (RaceConfig::Instance()->raceScenario().course == Course::Delfino_Pier &&
            objects && objects->psea() && (m_timer & 15) == 15) {
        const f32 height = -objects->distAboveRisingWater(0.f);
        if (std::fabs(m_battleWaterHeight - height) > 1.1f * 80.f)
            m_battleWaterHeight = height;
    }
}

/// @addr{0x80532084}
RaceManager *RaceManager::CreateInstance() {
    ASSERT(!s_instance);
    s_instance = EGG::egg_new<RaceManager>();
    return s_instance;
}

/// @addr{0x805320D4}
void RaceManager::DestroyInstance() {
    ASSERT(s_instance);
    auto *instance = s_instance;
    s_instance = nullptr;
    EGG::egg_delete(instance);
}

/// @addr{0x805327A0}
RaceManager::RaceManager()
    : m_random(RNG_SEED), m_stage(Stage::Intro), m_introTimer(0), m_timer(0) {
    m_playerCount = RaceConfig::Instance()->raceScenario().playerCount;
    if (RaceConfig::Instance()->raceScenario().gameMode == RaceConfig::Scenario::GameMode::Battle) {
        // Raceinfo ctor USA8052dfb8..dff0 consumes these two native draws.
        m_battleStartOffset = static_cast<u8>(m_random.getU32(6));
        m_battleRespawnPoint = static_cast<u8>(m_random.getU32(CourseMap::Instance()->getStartPointCount()));
    }
    for (size_t i = 0; i < m_playerCount; ++i) m_players[i].bindInputs(i);
}

/// @addr{0x80532E3C}
RaceManager::~RaceManager() {
    if (s_instance) {
        s_instance = nullptr;
        WARN("RaceManager instance not explicitly handled!");
    }
}

/// @addr{0x80533ED8}
RaceManager::Player::Player() {
    m_checkpointId = 0;
    m_raceCompletion = 0.0f;
    m_checkpointFactor = -1.0f;
    m_checkpointStartLapCompletion = 0.0f;
    m_lapCompletion = 0.999999f;

    auto *courseMap = CourseMap::Instance();

    if (courseMap->getCheckPointCount() > 0 && courseMap->getCheckPathCount() > 0) {
        m_maxKcp = courseMap->checkPoint()->lastKcpType();
    } else {
        m_maxKcp = -1;
    }

    m_currentLap = 0;
    m_maxLap = 1;
    m_drivingWrongWay = false;
    m_inputs = &KPadDirector::Instance()->playerInput();
}

/// @addr{0x80534194}
void RaceManager::Player::bindInputs(size_t index) {
    m_playerIndex = index;
    m_inputs = &KPadDirector::Instance()->playerInput(index);
}

void RaceManager::Player::init(size_t index) {
    bindInputs(index);
    auto *courseMap = CourseMap::Instance();

    if (courseMap->getCheckPointCount() != 0 && courseMap->getCheckPathCount() != 0) {
        const EGG::Vector3f &pos = Kart::KartObjectManager::Instance()->object(m_playerIndex)->pos();
        f32 distanceRatio;
        s16 checkpointId = courseMap->findSector(pos, 0, distanceRatio);

        m_checkpointId = std::max<s16>(0, checkpointId);
        m_jugemId = courseMap->getCheckPoint(m_checkpointId)->jugemIndex();
    } else {
        m_jugemId = 0;
    }
}

/// @addr{0x80535304}
void RaceManager::Player::calc() {
    if (m_finished) return;
    auto *courseMap = CourseMap::Instance();
    const auto *kart = Kart::KartObjectManager::Instance()->object(m_playerIndex);

    if (courseMap->getCheckPointCount() == 0 || courseMap->getCheckPathCount() == 0 ||
            kart->status().onBit(Kart::eStatus::BeforeRespawn)) {
        return;
    }

    f32 distanceRatio;
    s16 checkpointId = courseMap->findSector(kart->pos(), m_checkpointId, distanceRatio);

    if (checkpointId == -1) {
        return;
    }

    System::MapdataCheckPoint *checkpoint = nullptr;

    if (m_checkpointFactor < 0.0f || m_checkpointId != checkpointId) {
        checkpoint = calcCheckpoint(checkpointId, distanceRatio);
    } else {
        checkpoint = CourseMap::Instance()->getCheckPoint(m_checkpointId);
    }

    m_raceCompletion = static_cast<f32>(m_currentLap) +
            (m_checkpointStartLapCompletion + m_checkpointFactor * distanceRatio);
    m_raceCompletion = std::min(m_raceCompletion, static_cast<f32>(m_currentLap) + 0.99999f);

    const EGG::Vector3f &bodyFront = kart->bodyFront();
    if (bodyFront.x != 0.0f && bodyFront.z != 0.0f) {
        EGG::Vector2f frontXZ = EGG::Vector2f(bodyFront.x, bodyFront.z);
        frontXZ.normalise();
        m_drivingWrongWay = checkpoint->dir().dot(frontXZ) <= -0.5f;
    }
}

/// @addr{0x8053572C}
/// @brief Gets the lap split, which is the difference between the given lap and the previous one.
/// @param lap One-indexed lap.
/// @return The split timer.
Timer RaceManager::Player::getLapSplit(size_t lap) const {
    ASSERT(lap <= m_lapTimers.size());

    if (lap < 2) {
        return m_lapTimers[0];
    }

    const Timer &currentLap = m_lapTimers[lap - 1];
    const Timer &previousLap = m_lapTimers[lap - 2];
    if (!currentLap.valid || !previousLap.valid) {
        return Timer(std::numeric_limits<u16>::max(), 0, 0);
    }

    return currentLap - previousLap;
}

/// @addr{0x80534DF8}
MapdataCheckPoint *RaceManager::Player::calcCheckpoint(u16 checkpointId, f32 distanceRatio) {
    auto *courseMap = CourseMap::Instance();

    u16 oldCheckpointId = m_checkpointId;
    m_checkpointId = checkpointId;

    f32 lapProportion = courseMap->checkPath()->lapProportion();
    MapdataCheckPath *checkPath = courseMap->checkPath()->findCheckpathForCheckpoint(checkpointId);
    m_checkpointFactor = checkPath->oneOverCount() * lapProportion;

    m_checkpointStartLapCompletion = static_cast<f32>(checkPath->depth()) * lapProportion +
            (m_checkpointFactor * static_cast<f32>(checkpointId - checkPath->start()));

    f32 newLapCompletion = m_checkpointStartLapCompletion + distanceRatio * m_checkpointFactor;
    f32 deltaLapCompletion = m_lapCompletion - newLapCompletion;

    MapdataCheckPoint *newCheckpoint = courseMap->getCheckPoint(checkpointId);
    const MapdataCheckPoint *oldCheckpoint = courseMap->getCheckPoint(oldCheckpointId);

    s8 newJugemIdx = newCheckpoint->jugemIndex();
    if (newJugemIdx >= 0) {
        m_jugemId = newJugemIdx;
    }

    if (!newCheckpoint->isNormalCheckpoint()) {
        if (newCheckpoint->checkArea() > m_maxKcp) {
            m_maxKcp = newCheckpoint->checkArea();
        } else if (m_maxKcp == courseMap->checkPoint()->lastKcpType()) {
            if ((newCheckpoint->isFinishLine() &&
                        areCheckpointsSubsequent(oldCheckpoint, checkpointId)) ||
                    deltaLapCompletion > 0.95f) {
                incrementLap();
            }
        }
    }

    if ((oldCheckpoint->isFinishLine() &&
                areCheckpointsSubsequent(newCheckpoint, oldCheckpointId)) ||
            deltaLapCompletion < -0.95f) {
        decrementLap();
    }

    m_lapCompletion = newLapCompletion;

    return newCheckpoint;
}

/// @addr{Inlined in 0x80534DF8}
bool RaceManager::Player::areCheckpointsSubsequent(const MapdataCheckPoint *checkpoint,
        u16 nextCheckpointId) const {
    for (size_t i = 0; i < checkpoint->nextCount(); ++i) {
        if (nextCheckpointId == checkpoint->nextPoint(i)->id()) {
            return true;
        }
    }

    return false;
}

/// @addr{0x80534D6C}
void RaceManager::Player::decrementLap() {
    auto *courseMap = CourseMap::Instance();

    if (courseMap->getCheckPointCount() > 0 && courseMap->getCheckPathCount() > 0) {
        m_maxKcp = courseMap->checkPoint()->lastKcpType();
    } else {
        m_maxKcp = -1;
    }

    --m_currentLap;
}

/// @addr{0x805349B8}
void RaceManager::Player::incrementLap() {
    m_maxKcp = 0;
    if (++m_currentLap <= m_maxLap) {
        return;
    }

    const auto *kart = Kart::KartObjectManager::Instance()->object(m_playerIndex);
    u16 addMs = CourseMap::Instance()->getCheckPointEntryOffsetMs(m_checkpointId, kart->pos(),
            kart->prevPos());

    const Timer &currentTimer = RaceManager::Instance()->timerManager().currentTimer();
    Timer timer = currentTimer + static_cast<f32>(addMs);

    // TODO: Handle this case more gracefully
    ASSERT(static_cast<size_t>(m_maxLap - 1) < m_lapTimers.size());
    m_lapTimers[m_maxLap - 1] = timer;

    if (m_maxLap >= 3) {
        endRace(timer);
    } else {
        m_maxLap = m_currentLap;
    }
}

/// @addr{0x805347F4}
void RaceManager::Player::endRace(const Timer &finishTime) {
    m_finished = true;
    m_raceTimer = finishTime;
    RaceManager::Instance()->endPlayerRace(m_playerIndex);
}

RaceManager *RaceManager::s_instance = nullptr; ///< @addr{0x809BD730}

} // namespace Kinoko::System
