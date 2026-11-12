#pragma once

#include "game/system/KPadController.hh"
#include "game/system/Random.hh"
#include "game/system/map/MapdataCheckPoint.hh"
#include "game/system/map/MapdataJugemPoint.hh"

#include <egg/math/Vector.hh>

namespace Kinoko {

namespace Host {

class Context;

} // namespace Host

namespace System {

/// @addr{0x809BD730}
/// @brief Manages the timers that track the stages of a race.
/// Also acts as the interface between the physics engine and CourseMap.
/// @details The physics engine leverages the RaceManager in order to determine what stage of the
/// race we're in, as that affects several things like acceleration. This class also retrieves the
/// player start position from CourseMap and communicates it to the physics engine.
/// @nosubgrouping
class RaceManager : EGG::Disposer {
    friend class Host::Context;

public:
    class Player {
        friend class RaceManager;
    public:
        Player();
        virtual ~Player() {}

        void init(size_t index);
        void bindInputs(size_t index);
        void calc();

        [[nodiscard]] Timer getLapSplit(size_t idx) const;

        /// @beginGetters
        [[nodiscard]] bool finished() const { return m_finished; }
        [[nodiscard]] s16 currentLap() const { return m_currentLap; }
        [[nodiscard]] u8 finishOrder() const { return m_finishOrder; }
        void setFinishOrder(u8 order) { m_finishOrder = order; }

        [[nodiscard]] u16 checkpointId() const {
            return m_checkpointId;
        }

        [[nodiscard]] f32 raceCompletion() const {
            return m_raceCompletion;
        }

        [[nodiscard]] s8 jugemId() const {
            return m_jugemId;
        }

        [[nodiscard]] bool drivingWrongWay() const {
            return m_drivingWrongWay;
        }

        [[nodiscard]] const std::array<Timer, 3> &lapTimers() const {
            return m_lapTimers;
        }

        [[nodiscard]] const Timer &lapTimer(size_t idx) const {
            ASSERT(idx < m_lapTimers.size());
            return m_lapTimers[idx];
        }

        [[nodiscard]] const Timer &raceTimer() const {
            return m_raceTimer;
        }

        [[nodiscard]] const KPad *inputs() const {
            return m_inputs;
        }
        /// @endGetters

    private:
        MapdataCheckPoint *calcCheckpoint(u16 checkpointId, f32 distanceRatio);
        [[nodiscard]] bool areCheckpointsSubsequent(const MapdataCheckPoint *checkpoint,
                u16 nextCheckpointId) const;

        void decrementLap();
        void incrementLap();
        void endRace(const Timer &finishTime);

        size_t m_playerIndex = 0;
        bool m_finished = false;
        u8 m_finishOrder = 0;
        u16 m_checkpointId;
        f32 m_raceCompletion;
        f32 m_checkpointFactor; ///< The proportion of a lap for the current checkpoint
        f32 m_checkpointStartLapCompletion;
        f32 m_lapCompletion;
        s8 m_jugemId;
        s16 m_currentLap;
        s8 m_maxLap;
        s8 m_maxKcp;
        bool m_drivingWrongWay;
        std::array<Timer, 3> m_lapTimers;
        Timer m_raceTimer;
        const KPad *m_inputs;
    };

    enum class Stage {
        Intro = 0,
        Countdown = 1,
        Race = 2,
        FinishLocal = 3,
        FinishGlobal = 4,
    };

    void init();

    void findKartStartPoint(EGG::Vector3f &pos, EGG::Vector3f &angles, size_t index = 0);
    void endPlayerRace(u32 idx);
    // Battle ends all racers together, with the score ordering supplied by
    // its race-mode controller; reuse the native finish-time/finish hook.
    bool endBattle(const std::array<u8,12>& order);

    void calc();

    /// @addr{0x80536230}
    [[nodiscard]] bool isStageReached(Stage stage) const {
        return static_cast<std::underlying_type_t<Stage>>(m_stage) >=
                static_cast<std::underlying_type_t<Stage>>(stage);
    }

    [[nodiscard]] MapdataJugemPoint *jugemPoint(size_t index = 0);
    // Original bridge updates the same Battle respawn mask as rising water.
    void setBattleRespawnGroup(u16 group, bool enabled) {
        if (group >= 32) return;
        if (enabled) m_battleRespawnGroups |= 1u << group;
        else m_battleRespawnGroups &= ~(1u << group);
    }

    /// @beginGetters
    /// @addr{0x80533090}
    [[nodiscard]] int getCountdownTimer() const {
        return STAGE_COUNTDOWN_DURATION - m_timer;
    }

    [[nodiscard]] Random &random() {
        return m_random;
    }

    [[nodiscard]] const Player &player(size_t index = 0) const {
        ASSERT(index < m_playerCount);
        return m_players[index];
    }

    [[nodiscard]] const TimerManager &timerManager() const {
        return m_timerManager;
    }

    /// Port hook: while held, the intro stage waits (karts and objects still
    /// simulate). Retail ends the intro on the opening camera instead of the
    /// fixed time-trial duration; the frontend releases it when the pan ends.
    void setIntroHeld(bool held) {
        m_introHeld = held;
    }

    [[nodiscard]] Stage stage() const {
        return m_stage;
    }

    [[nodiscard]] u32 timer() const {
        return m_timer;
    }
    /// @endGetters

    static RaceManager *CreateInstance();
    static void DestroyInstance();

    [[nodiscard]] static RaceManager *Instance() {
        return s_instance;
    }

private:
    EGG_NEW_DELETE_FRIEND

    RaceManager();
    ~RaceManager() override;

    void initBattleRespawns();
    void calcBattleRespawns();
    u16 m_battleJugemCursor = 0;
    u16 m_battleGroupMax = 0;
    u16 m_battleGroupCursor = 0;
    u32 m_battleRespawnGroups = 1;
    f32 m_battleWaterHeight = -1.f;
    std::array<f32,99> m_battleGroupMin{}, m_battleGroupMaxY{};

    Random m_random;
    std::array<Player, 12> m_players;
    size_t m_playerCount = 1;
    u8 m_finishCount = 0;
    u8 m_battleStartOffset = 0;
    [[maybe_unused]] u8 m_battleRespawnPoint = 0;
    TimerManager m_timerManager;
    Stage m_stage;
    u16 m_introTimer;
    bool m_introHeld = false;
    u32 m_timer;

    static constexpr u16 STAGE_COUNTDOWN_DURATION = 240;
    static constexpr u32 RNG_SEED = 0x74A1B095;

    static RaceManager *s_instance; ///< @addr{0x809BD730}
};

} // namespace System

} // namespace Kinoko
