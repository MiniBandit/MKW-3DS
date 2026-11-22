#include "KReplaySystem.hh"

#include "host/Option.hh"
#include "host/SceneCreatorDynamic.hh"

#include <abstract/File.hh>
#include <egg/core/Heap.hh>

#include <game/field/ObjectDirector.hh>
#include <game/system/RaceManager.hh>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <iomanip>

namespace Kinoko {

/// @brief Initializes the system.
void KReplaySystem::init() {
    ASSERT(m_currentGhostFileName);
    ASSERT(m_currentRawGhost);
    ASSERT(m_currentGhost);

    auto *sceneCreator = EGG::egg_new<Host::SceneCreatorDynamic>();
    m_sceneMgr = EGG::egg_new<EGG::SceneManager>(sceneCreator);

    System::RaceConfig::RegisterInitCallback(OnInit, nullptr);
    Abstract::File::Remove("results.txt");

    m_sceneMgr->changeScene(0);
}

/// @brief Executes a frame.
void KReplaySystem::calc() {
    m_sceneMgr->calc();
}

/// @brief Executes a run.
/// @details A run consists of replaying a ghost.
/// @return Whether the run was successful or not.
bool KReplaySystem::run() {
    // Port check (KINOKO_REINIT_TEST=N): a course preview of N held frames,
    // then ObjectDirector::init() at the cut, must leave every object exactly
    // where a freshly built scene puts it (retail rebuilds the scene there).
    if (const char *env = std::getenv("KINOKO_REINIT_TEST")) {
        return runReinitTest(std::atoi(env));
    }

    while (!calcEnd()) {
        calc();
    }

    return success();
}

// KINOKO_REINIT_SKIP="0x191,0xe3": extra object types left out of the re-init.
static std::vector<u16> s_extraSkip;
static bool ExtraSkip(u16 id) {
    for (u16 s : s_extraSkip) {
        if (s == id) {
            return true;
        }
    }
    return false;
}

bool KReplaySystem::runReinitTest(int previewFrames) {
    if (const char *skip = std::getenv("KINOKO_REINIT_SKIP")) {
        for (const char *p = skip; *p;) {
            char *end = nullptr;
            s_extraSkip.push_back(static_cast<u16>(std::strtoul(p, &end, 0)));
            p = *end ? end + 1 : end;
        }
        Field::ObjectDirector::s_reinitExtraSkip = ExtraSkip;
    }
    if (std::getenv("KINOKO_REINIT_ALL")) {
        Field::ObjectDirector::s_reinitAll = true;
    }
    constexpr int FRAMES = 420; // the 172 intro frames, the 240 countdown, a few more
    auto snapshot = [](std::vector<f32> &out) {
        out.clear();
        for (const auto *obj : Field::ObjectDirector::Instance()->objects()) {
            const auto &p = obj->pos();
            const auto &s = obj->scale();
            const auto &m = obj->transform();
            out.insert(out.end(), {p.x, p.y, p.z, s.x, s.y, s.z});
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 4; ++c) {
                    out.push_back(m[r, c]);
                }
            }
        }
        // The race RNG last (as raw bits): it must follow the fresh scene's too.
        const u64 rng = System::RaceManager::Instance()->random().state();
        f32 words[2];
        std::memcpy(words, &rng, sizeof rng);
        out.insert(out.end(), {words[0], words[1]});
    };
    constexpr size_t PER_OBJECT = 18;
    std::vector<std::vector<f32>> fresh(FRAMES), rerun(FRAMES);
    std::vector<u32> ids;
    std::vector<const char *> names;
    for (const auto *obj : Field::ObjectDirector::Instance()->objects()) {
        ids.push_back(static_cast<u32>(obj->id()));
        const auto &flowTable = Field::ObjectDirector::Instance()->flowTable();
        const auto *set = flowTable.set(flowTable.slot(obj->id()));
        names.push_back(set ? set->name : nullptr);
    }
    if (std::getenv("KINOKO_REINIT_IDS")) {
        for (u32 id : ids) {
            std::printf("0x%x ", id);
        }
        std::printf("\n");
    }
    // Two processes, no in-process scene restart: KINOKO_REINIT_FRESH names a
    // file; the first run (file absent) records the fresh scene into it, the
    // second runs the preview + re-init and compares against it.
    const char *freshPath = std::getenv("KINOKO_REINIT_FRESH");
    if (!freshPath) {
        std::printf("set KINOKO_REINIT_FRESH\n");
        return false;
    }
    if (FILE *in = std::fopen(freshPath, "rb")) {
        for (auto &frame : fresh) {
            u32 n = 0;
            if (std::fread(&n, sizeof n, 1, in) != 1) {
                std::fclose(in);
                std::printf("fresh file short\n");
                return false;
            }
            frame.resize(n);
            if (n && std::fread(frame.data(), sizeof(f32), n, in) != n) {
                std::fclose(in);
                std::printf("fresh file short\n");
                return false;
            }
        }
        std::fclose(in);
    } else {
        FILE *out = std::fopen(freshPath, "wb");
        for (int f = 0; f < FRAMES; ++f) {
            calc();
            snapshot(fresh[f]);
            const u32 n = static_cast<u32>(fresh[f].size());
            std::fwrite(&n, sizeof n, 1, out);
            std::fwrite(fresh[f].data(), sizeof(f32), n, out);
        }
        std::fclose(out);
        std::printf("fresh recorded\n");
        return true;
    }

    System::RaceManager::Instance()->setIntroHeld(true);
    for (int f = 0; f < previewFrames; ++f) {
        calc();
    }
    Field::ObjectDirector::Instance()->reinitForRace();
    System::RaceManager::Instance()->setIntroHeld(false);
    for (int f = 0; f < FRAMES; ++f) {
        calc();
        snapshot(rerun[f]);
    }

    std::vector<int> firstBad(ids.size(), -1);
    size_t bad = 0;
    for (int f = 0; f < FRAMES; ++f) {
        if (fresh[f].size() == rerun[f].size() &&
                std::memcmp(&fresh[f][fresh[f].size() - 2], &rerun[f][rerun[f].size() - 2],
                        2 * sizeof(f32)) != 0) {
            std::printf("race RNG differs from frame %d\n", f);
            ++bad;
            break;
        }
    }
    for (int f = 0; f < FRAMES; ++f) {
        if (fresh[f].size() != rerun[f].size()) {
            std::printf("object count differs at frame %d\n", f);
            return false;
        }
        for (size_t o = 0; o < ids.size(); ++o) {
            if (firstBad[o] >= 0 ||
                    Field::ObjectDirector::ReinitSkipped(static_cast<Field::ObjectId>(ids[o]))) {
                continue;
            }
            if (std::memcmp(&fresh[f][o * PER_OBJECT], &rerun[f][o * PER_OBJECT],
                        PER_OBJECT * sizeof(f32)) != 0) {
                firstBad[o] = f;
                ++bad;
            }
        }
    }
    for (size_t o = 0; o < ids.size(); ++o) {
        if (firstBad[o] >= 0) {
            const f32 *a = &fresh[firstBad[o]][o * PER_OBJECT];
            const f32 *b = &rerun[firstBad[o]][o * PER_OBJECT];
            std::printf("object %zu id 0x%x (%s) differs from frame %d: pos %.3f %.3f %.3f vs %.3f %.3f "
                        "%.3f, scale %.3f vs %.3f, rot00 %.4f vs %.4f\n",
                    o, ids[o], names[o] ? names[o] : "?", firstBad[o], a[0], a[1], a[2], b[0], b[1], b[2], a[3], b[3], a[6],
                    b[6]);
        }
    }
    std::printf("reinit test: %zu objects, %zu differ (preview %d frames)\n", ids.size(), bad,
            previewFrames);
    return bad == 0;
}

/// @brief Parses non-generic command line options.
/// @details The only currently accepted option is the ghost flag.
/// @param argc The number of arguments.
/// @param argv The arguments.
void KReplaySystem::parseOptions(int argc, char **argv) {
    if (argc < 2) {
        PANIC("Expected ghost argument!");
    }

    for (int i = 0; i < argc; ++i) {
        std::optional<Host::EOption> flag = Host::Option::CheckFlag(argv[i]);
        if (!flag || *flag == Host::EOption::Invalid) {
            WARN("Expected a flag! Got: %s", argv[i]);
            continue;
        }

        switch (*flag) {
        case Host::EOption::Ghost: {
            ASSERT(i + 1 < argc);

            m_currentGhostFileName = argv[++i];
            m_currentRawGhost =
                    Abstract::File::LoadHost(m_currentGhostFileName, m_currentRawGhostSize);

            if (m_currentRawGhostSize < System::RKG_HEADER_SIZE ||
                    m_currentRawGhostSize > sizeof(System::RawGhostFile)) {
                PANIC("File cannot be a ghost! Check the file size.");
            }

            // Creating the raw ghost file validates it
            System::RawGhostFile file = System::RawGhostFile(m_currentRawGhost);

            m_currentGhost = EGG::egg_new<System::GhostFile>(file);
            ASSERT(m_currentGhost);
        } break;
        case Host::EOption::Invalid:
        default:
            PANIC("Invalid flag!");
            break;
        }
    }
}

KReplaySystem *KReplaySystem::CreateInstance() {
    ASSERT(!s_instance);
    s_instance = EGG::egg_new<KReplaySystem>();
    return static_cast<KReplaySystem *>(s_instance);
}

void KReplaySystem::DestroyInstance() {
    ASSERT(s_instance);
    auto *instance = s_instance;
    s_instance = nullptr;
    EGG::egg_delete(instance);
}

KReplaySystem::KReplaySystem()
    : m_currentGhostFileName(nullptr), m_currentGhost(nullptr), m_currentRawGhost(nullptr),
      m_currentRawGhostSize(0) {}

KReplaySystem::~KReplaySystem() {
    if (s_instance) {
        s_instance = nullptr;
        WARN("KReplaySystem instance not explicitly handled!");
    }

    EGG::egg_delete(m_sceneMgr);
    EGG::egg_delete(m_currentGhost);
    delete[] m_currentRawGhost;
}

/// @brief Determines whether or not the ghost simulation should end.
/// @return Whether the ghost should end or not.
bool KReplaySystem::calcEnd() const {
    constexpr u16 MAX_MINUTE_COUNT = 10;

    const auto *raceManager = System::RaceManager::Instance();
    if (raceManager->stage() == System::RaceManager::Stage::FinishGlobal) {
        return true;
    }

    if (raceManager->timerManager().currentTimer().min >= MAX_MINUTE_COUNT) {
        return true;
    }

    return false;
}

/// @brief Reports failure to file.
/// @param msg The message to report.
void KReplaySystem::reportFail(const std::string &msg) const {
    std::string report(m_currentGhostFileName);
    report += "\n" + std::string(msg);
    Abstract::File::Append("results.txt", report.c_str(), report.size());
}

/// @brief Determines whether the simulation was a success or not.
/// @return Whether the simulation was a success or not.
bool KReplaySystem::success() const {
    auto format = [](const System::Timer &timer) {
        std::ostringstream oss;
        oss << std::setw(2) << std::setfill('0') << timer.min << ":" << std::setw(2)
            << std::setfill('0') << timer.sec << "." << std::setw(3) << std::setfill('0')
            << timer.mil;
        return oss.str();
    };

    const auto *raceManager = System::RaceManager::Instance();
    if (raceManager->stage() != System::RaceManager::Stage::FinishGlobal) {
        m_sceneMgr->currentScene()->heap()->enableAllocation();
        reportFail("Race didn't finish");
        return false;
    }

    s32 desyncingTimerIdx = getDesyncingTimerIdx();
    if (desyncingTimerIdx != -1) {
        m_sceneMgr->currentScene()->heap()->enableAllocation();
        std::string msg;

        const auto [correct, incorrect] = getDesyncingTimer(desyncingTimerIdx);
        if (desyncingTimerIdx == 0) {
            msg = "Final timer desync!";
        } else {
            msg = "Lap " + std::to_string(desyncingTimerIdx) + " timer desync!";
        }

        msg += " Expected " + format(correct) + ", got " + format(incorrect);
        reportFail(msg);
        return false;
    }

    return true;
}

/// @brief Finds the desyncing timer index, if one exists.
/// @return -1 if there's no desync, 0 if the final timer desyncs, and 1+ if a lap timer desyncs.
s32 KReplaySystem::getDesyncingTimerIdx() const {
    const auto &player = System::RaceManager::Instance()->player();
    if (m_currentGhost->raceTimer() != player.raceTimer()) {
        return 0;
    }

    for (size_t i = 0; i < 3; ++i) {
        if (m_currentGhost->lapTimer(i) != player.getLapSplit(i + 1)) {
            return i + 1;
        }
    }

    return -1;
}

/// @brief Gets the desyncing timer according to the index.
/// @param i Index to the desyncing timer. Cannot be -1.
/// @return The pair of timers. The first is the correct one, and the second is the incorrect one.
KReplaySystem::DesyncingTimerPair KReplaySystem::getDesyncingTimer(s32 i) const {
    auto cond = i <=> 0;
    ASSERT(cond != std::strong_ordering::less);

    if (cond == std::strong_ordering::equal) {
        const auto &correct = m_currentGhost->raceTimer();
        const auto &incorrect = System::RaceManager::Instance()->player().raceTimer();
        ASSERT(correct != incorrect);
        return DesyncingTimerPair(correct, incorrect);
    } else if (cond == std::strong_ordering::greater) {
        const auto &correct = m_currentGhost->lapTimer(i - 1);
        const auto &incorrect = System::RaceManager::Instance()->player().lapTimer(i - 1);
        ASSERT(correct != incorrect);
        return DesyncingTimerPair(correct, incorrect);
    }

    // This is unreachable
    return DesyncingTimerPair(System::Timer(), System::Timer());
}

/// @brief Initializes the race configuration as needed for replays.
/// @param config The race configuration instance.
/// @param arg Unused optional argument.
void KReplaySystem::OnInit(System::RaceConfig *config, void * /* arg */) {
    config->setGhost(Instance()->m_currentRawGhost);
    config->raceScenario().players[0].type = System::RaceConfig::Player::Type::Ghost;
}

} // namespace Kinoko
