#include "RaceScene.hh"
#include <mkw/sim_profile.hpp>
#include <mkw/load_profile.hpp>

#include "game/field/BoxColManager.hh"
#include "game/field/CollisionDirector.hh"
#include "game/field/ObjectDirector.hh"
#include "game/field/RailManager.hh"
#include "game/field/jugem/JugemDirector.hh"
#include "game/item/ItemDirector.hh"
#include "game/kart/KartObjectManager.hh"
#include "game/render/KartCamera.hh"
#include "game/system/CourseMap.hh"
#include "game/system/KPadDirector.hh"
#include "game/system/RaceConfig.hh"
#include "game/system/RaceManager.hh"
#include "game/system/ResourceManager.hh"

#include <ScopeLock.hh>

#ifdef MKW_KINOKO_PGO_GENERATE
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
extern "C" void __gcov_dump();
extern "C" void __gcov_reset();
#endif

namespace Kinoko::Scene {

/// @addr{0x80553B88}
RaceScene::RaceScene() {
    m_heap->setName("RaceSceneHeap");
}

/// @addr{0x80553BD4}
RaceScene::~RaceScene() = default;

/// @addr{0x80554208}
void RaceScene::createEngines() {
    {
        ScopeLock<GroupID> lock(GroupID::Gfx);
        MKW_LOAD_SCOPE(CreateCamera);
        Render::KartCamera::CreateInstance();
    }

    {
        ScopeLock<GroupID> lock(GroupID::Course);
        MKW_LOAD_SCOPE(CreateCourseMap);
        System::CourseMap::CreateInstance()->init();
    }

    {
        ScopeLock<GroupID> lock(GroupID::Race);
        MKW_LOAD_SCOPE(CreateRaceManager);
        System::RaceManager::CreateInstance();
    }

    {
        ScopeLock<GroupID> lock(GroupID::Object);
        MKW_LOAD_SCOPE(CreateBoxCol);
        Field::BoxColManager::CreateInstance();
        Field::JugemDirector::CreateInstance();
    }

    {
        ScopeLock<GroupID> lock(GroupID::Kart);
        MKW_LOAD_SCOPE(CreateKarts);
        Kart::KartObjectManager::CreateInstance();
    }

    {
        ScopeLock<GroupID> lock(GroupID::Course);
        MKW_LOAD_SCOPE(CreateCollision);
        Field::CollisionDirector::CreateInstance();
    }

    {
        ScopeLock<GroupID> lock(GroupID::Item);
        MKW_LOAD_SCOPE(CreateItems);
        Item::ItemDirector::CreateInstance();
    }

    {
        ScopeLock<GroupID> lock(GroupID::Object);
        MKW_LOAD_SCOPE(CreateObjects);
        Field::RailManager::CreateInstance();
        Field::ObjectDirector::CreateInstance();
    }
}

/// @addr{0x8055472C}
void RaceScene::initEngines() {
    {
        ScopeLock<GroupID> lock(GroupID::Kart);
        MKW_LOAD_SCOPE(InitKarts);
        Kart::KartObjectManager::Instance()->init();
    }

    {
        ScopeLock<GroupID> lock(GroupID::Gfx);
        MKW_LOAD_SCOPE(InitCamera);
        initCamera();
    }

    {
        ScopeLock<GroupID> lock(GroupID::Race);
        MKW_LOAD_SCOPE(InitRaceManager);
        System::RaceManager::Instance()->init();
    }

    {
        ScopeLock<GroupID> lock(GroupID::Item);
        MKW_LOAD_SCOPE(InitItems);
        Item::ItemDirector::Instance()->init();
    }

    {
        ScopeLock<GroupID> lock(GroupID::Object);
        MKW_LOAD_SCOPE(InitObjects);
        Field::ObjectDirector::Instance()->init();
        Field::JugemDirector::Instance()->init();
    }

    m_heap->disableAllocation();
}

/// @addr{0x80554E6C}
/// @details In Kinoko, it is not possible to pause the race scene, so Kinoko's implementation for
/// this function is really the base game's `calcEnginesUnpaused` located at `0x80554AD4`.
void RaceScene::calcEngines() {
    auto *raceMgr = System::RaceManager::Instance();
    // DIAG timing per engine (include/mkw/sim_profile.hpp); off by default.
    {MKW_SIM_SCOPE(RaceMgr);raceMgr->calc();}
    {MKW_SIM_SCOPE(BoxCol);Field::BoxColManager::Instance()->calc();}
    {MKW_SIM_SCOPE(Objects);Field::ObjectDirector::Instance()->calc();}
    {MKW_SIM_SCOPE(Karts);Kart::KartObjectManager::Instance()->calc();}
    {MKW_SIM_SCOPE(Jugem);Field::JugemDirector::Instance()->calc();}
    {MKW_SIM_SCOPE(Items);Item::ItemDirector::Instance()->calc();}
    raceMgr->random().next();
}

#ifdef MKW_KINOKO_PGO_GENERATE
namespace {
/// mkw-3ds PGO training build (tools/diag/compile.py, MKW_KINOKO_PGO=generate): at
/// each race end, the instrumented code's profile counters (Kinoko, and the port's src/
/// in the Old 3DS variants) go to sdmc:/3ds/mkw-3ds/pgo/<path below objects/>.gcda (the
/// object path with its first MKW_KINOKO_PGO_GENERATE components cut), merged with the
/// races before. libgcov does not create the folders itself on the SD card.
void dumpKinokoProfile() {
    static const char *const dirs[] = {"", "/src", "/vendor", "/vendor/kinoko", "/vendor/kinoko/source"};
    static const char *const kinokoDirs[] = {"/abstract", "/abstract/g3d", "/abstract/memory", "/egg",
            "/egg/core", "/egg/util", "/game", "/game/field", "/game/field/jugem",
            "/game/field/obj", "/game/item", "/game/kart", "/game/render", "/game/scene",
            "/game/system", "/game/system/map"};
    char path[96];
    for (const char *dir : dirs) {
        std::snprintf(path, sizeof path, "sdmc:/3ds/mkw-3ds/pgo%s", dir);
        mkdir(path, 0777);
    }
    for (const char *dir : kinokoDirs) {
        std::snprintf(path, sizeof path, "sdmc:/3ds/mkw-3ds/pgo/vendor/kinoko/source%s", dir);
        mkdir(path, 0777);
    }
    std::snprintf(path, sizeof path, "%d", MKW_KINOKO_PGO_GENERATE);
    setenv("GCOV_PREFIX", "sdmc:/3ds/mkw-3ds/pgo", 1);
    setenv("GCOV_PREFIX_STRIP", path, 1);
    __gcov_dump();
    __gcov_reset();
}
} // namespace
#endif

/// @addr{0x805549B0}
void RaceScene::destroyEngines() {
#ifdef MKW_KINOKO_PGO_GENERATE
    dumpKinokoProfile();
#endif
    System::KPadDirector::Instance()->endGhostProxies();
    Render::KartCamera::DestroyInstance();
    Kart::KartObjectManager::DestroyInstance();
    Field::JugemDirector::DestroyInstance();
    Field::ObjectDirector::DestroyInstance();
    Field::RailManager::DestroyInstance();
    Field::CollisionDirector::DestroyInstance();
    Item::ItemDirector::DestroyInstance();
    Field::BoxColManager::DestroyInstance();
    System::RaceManager::DestroyInstance();
    System::CourseMap::DestroyInstance();
}

/// @brief Retrieves Common.szs and the course archive.
/// @addr{0x80553C50}
void RaceScene::configure() {
    auto *raceCfg = System::RaceConfig::Instance();
    auto *resMgr = System::ResourceManager::Instance();

    raceCfg->initRace();

    // Normally, the resources are loaded into a specific archive heap
    // For now, tag them with the resource group ID
    {
        ScopeLock<GroupID> lock(GroupID::Resource);
        auto *commonArc = resMgr->load(0, nullptr);
        appendResource(commonArc, 0);

        auto *courseArc = resMgr->load(raceCfg->raceScenario().course);
        appendResource(courseArc, 1);
    }
}

/// @brief This is called on race shutdown in order to prep for the next race.
/// @addr{0x80554A94}
void RaceScene::onReinit() {
    configure();
}

} // namespace Kinoko::Scene
