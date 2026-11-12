#pragma once

#include "game/system/GhostFile.hh"

#include <functional>

namespace Kinoko {

namespace Host {

class Context;

} // namespace Host

namespace System {

/// @addr{0x809BD728}
/// @brief Initializes the player with parameters specified in the provided ghost file.
/// @details In the base game, this class is responsible for managing the race and menu scenarios.
/// The menu scenario mostly pertains to character and vehicle selection in the menus prior to
/// starting a race. In Kinoko, we don't have these menus, so we initialize the race directly
/// through this class.
class RaceConfig : EGG::Disposer {
    friend class Host::Context;

public:
    struct Player {
    public:
        enum class Type {
            Local = 0, // Inputs managed by native controls or host
            CPU = 1, // Native route controller; original kart simulation
            Ghost = 3, // Inputs managed by ghost
            None = 5,
        };

        Character character;
        Vehicle vehicle;
        Type type;
        bool driftIsAuto;
        u8 startPosition = 0; // One-based previous finish position; zero uses offline first-race order.
        u8 team = 2; // Red=0, Blue=1, non-team=2.
    };

    struct Scenario {
    public:
        enum class GameMode {
            Grand_Prix = 0,
            Versus = 1,
            Time_Trial = 2,
            Battle = 3,
            Ghost_Race = 5,
            Awards = 11, // Retail ceremony; uses authored individual KTPT slots.
        };

        void init();

        std::array<Player, 12> players;
        u8 playerCount;
        Course course;
        u8 engineClass = 2; // Retail settings: 0=50cc, 1=100cc, 2=150cc.
        GameMode gameMode = GameMode::Time_Trial;
        u8 cameraMode = 0; // Retail awards mode 12 uses six places instead of three.
        bool mirror = false;
        bool teams = false;
        u8 battleType = 0; // Original: 0 Balloon Battle, 1 Coin Runners.
        u8 humanTeam = 0;
        u8 cpuDifficulty = 1, cpuVehicles = 0, itemMode = 0;
        [[nodiscard]] f32 engineClassRate() const {
            // USA Kart::Movement constructor 80571840; table 808b1414.
            constexpr f32 rates[] = {0.8f, 0.9f, 1.0f};
            return rates[engineClass < 3 ? engineClass : 2];
        }
    };

    typedef std::function<void(RaceConfig *, void *)> InitCallback;

    void init();
    void initRace();
    void initControllers();
    void initGhost();

    [[nodiscard]] const Scenario &raceScenario() const {
        return m_raceScenario;
    }

    [[nodiscard]] Scenario &raceScenario() {
        return m_raceScenario;
    }

    void setGhost(const u8 *rkg) {
        m_ghost = rkg;
    }

    // A scene-owned configuration is consumed by the next initRace only.
    // Keep the frontend's registered callback intact for subsequent races.
    void prepareNextScenario(const Scenario &scenario) {
        m_raceScenario = scenario;
        m_explicitNextScenario = true;
    }

    static void RegisterInitCallback(const InitCallback &callback, void *arg) {
        s_onInitCallback = callback;
        s_onInitCallbackArg = arg;
    }

    static RaceConfig *CreateInstance();
    static void DestroyInstance();

    [[nodiscard]] static RaceConfig *Instance() {
        return s_instance;
    }

private:
    EGG_NEW_DELETE_FRIEND

    RaceConfig();
    ~RaceConfig() override;

    Scenario m_raceScenario;
    bool m_explicitNextScenario = false;
    RawGhostFile m_ghost;

    static RaceConfig *s_instance; ///< @addr{0x809BD728}
    static InitCallback s_onInitCallback;
    static void *s_onInitCallbackArg;
};

} // namespace System

} // namespace Kinoko
