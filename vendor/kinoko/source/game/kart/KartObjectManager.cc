#include "KartObjectManager.hh"

#include "game/kart/KartAction.hh"
#include "game/kart/KartCollide.hh"
#include "game/kart/KartParamFileManager.hh"
#include "game/system/RaceConfig.hh"
#include "game/system/RaceManager.hh"

#include <abstract/g3d/ResFile.hh>

namespace Kinoko::Kart {

/// @addr{0x8058FEE0}
void KartObjectManager::init() {
    KartAction::ResetPowWindow(); // RMCU PlayerHolder_init 0x805896bc
    for (size_t i = 0; i < m_count; ++i) {
        m_objects[i]->initImpl();
        m_objects[i]->prepare();
    }
}

/// @addr{0x8058FFE8}
/// Retail PlayerHolder_update (USA 0x805897C4) runs pass zero for every racer
/// before any pass one, so racer-to-racer collision sees this frame's poses.
void KartObjectManager::calc() {
    for (size_t i = 0; i < m_count; ++i) {
        KartObject *object = m_objects[i];
        object->collide()->resetKartCollision();
        object->collide()->setTangentOff(EGG::Vector3f::zero);
        object->collide()->setMovement(EGG::Vector3f::zero);
    }

    if (s_phaseMajor && m_count >= 2) {
        // mkw-3ds: pass 0 phase-major. Part A (state, input, KartMove up to the
        // slipstream check) for each kart of a group, then part B (the rest of
        // KartMove, action, pull path, physics, hitboxes): each part's code stays in
        // the instruction cache across the group instead of being fetched again per
        // kart (the Old 3DS has 16 KB of L1 and no L2; after Codiak540's
        // o3ds-performance Kinoko patch). Pass 0 reads and writes only its own kart,
        // except the slipstream check of the kart whose turn it is (timer % count),
        // which reads the others: it runs alone, after every lower kart's whole pass 0
        // and before any higher kart's, as in the retail order. Pass 0 does not narrow
        // the KCL prism cache (pass 1 does), so no query depends on the kart order.
        const size_t slip = System::RaceManager::Instance()->timer() % m_count;
        // With s_pass0Split, four parts: A1 (state, input, KartMove to calcDirs),
        // A2 (the rest of KartMove's first half, slipstream included), B1 (its second
        // half, action, pull path), B2 (physics, hitboxes). Each part reads and writes
        // only its own kart, as A and B do, so the finer split keeps the result.
        auto group = [&](size_t begin, size_t end) {
            if (s_pass0Split) {
                for (size_t i = begin; i < end; ++i) {
                    m_objects[i]->calcSubA1();
                }
                for (size_t i = begin; i < end; ++i) {
                    m_objects[i]->calcSubA2();
                }
                for (size_t i = begin; i < end; ++i) {
                    m_objects[i]->calcSubB1();
                }
                for (size_t i = begin; i < end; ++i) {
                    m_objects[i]->calcSubB2();
                }
                return;
            }
            for (size_t i = begin; i < end; ++i) {
                m_objects[i]->calcSubA();
            }
            for (size_t i = begin; i < end; ++i) {
                m_objects[i]->calcSubB();
            }
        };
        group(0, slip);
        group(slip, slip + 1);
        group(slip + 1, m_count);
    } else {
        for (size_t i = 0; i < m_count; ++i) {
            m_objects[i]->calcSub();
        }
    }

    if (s_pass1Grouped && m_count >= 2) {
        // mkw-3ds: pass 1 grouped, like pass 0: the serial part of every kart in
        // order, then each local part (the code of each part stays in the instruction
        // cache across the karts). Retail runs serial(i) local(i) per kart; moving
        // local(i) after serial(j), j > i, keeps the result because:
        // - the racer pair (i, j) is resolved once, in serial(min(i, j)), before
        //   either local part, and reads and writes only those two karts;
        // - serial(j)'s objects and items involve only kart j (item snapshots of other
        //   racers are taken in their own serial parts), and local parts change no
        //   collidable object, item or random state;
        // - local(i) starts its own box search and prism cache narrowing
        //   (checkCourseColNarrScLocal) before any query, so serial(j)'s search does
        //   not reach it, and serial(j) starts its own search.
        // One exception: a pair skipped because one kart was exempt (respawn, cannon)
        // is checked again by the other kart's serial part, which in retail sees the
        // exempt kart after its local part. So before serial(i), if a kart whose
        // local part is still to run is exempt, the waiting local parts run first.
        size_t pending = 0;
        auto runLocal = [&](size_t end) {
            for (; pending < end; ++pending) {
                m_objects[pending]->calcLocal();
            }
        };
        for (size_t i = 0; i < m_count; ++i) {
            for (size_t k = pending; k < i; ++k) {
                if (KartCollide::KartCollisionStatusExempt(m_objects[k])) {
                    runLocal(i);
                    break;
                }
            }
            m_objects[i]->calcSerial();
        }
        runLocal(m_count);
    } else {
        for (size_t i = 0; i < m_count; ++i) {
            m_objects[i]->calc();
        }
    }

    // RMCU PlayerHolder_update 0x805897c4: the POW hit window ticks after every kart.
    KartAction::CalcPowWindow();
}

/// @addr{0x8058FAA8}
KartObjectManager *KartObjectManager::CreateInstance() {
    ASSERT(!s_instance);
    s_instance = EGG::egg_new<KartObjectManager>();
    return s_instance;
}

/// @addr{0x8058FAF8}
void KartObjectManager::DestroyInstance() {
    ASSERT(s_instance);
    auto *instance = s_instance;
    s_instance = nullptr;
    EGG::egg_delete(instance);
}

/// @addr{0x8058FB2C}
KartObjectManager::KartObjectManager() {
    const auto &raceScenario = System::RaceConfig::Instance()->raceScenario();
    m_count = raceScenario.playerCount;
    m_objects = static_cast<KartObject **>(EGG::egg_alloc(m_count * sizeof(KartObject *)));
    KartParamFileManager::CreateInstance();

    loadScaleAnimations();

    // mkw-3ds (Old 3DS locality): with s_kartArena, each kart's parts come from one block of
    // the heap (an EGG::BlockArena), back to back in creation order and without heap block
    // headers between them, instead of ~42 heap blocks with proxy list nodes between them.
    // Creation order roughly follows the passes: KartParam and KartObject (the accessor every
    // part reads through), the wheel pointer arrays, KartSub, KartMove, KartAction, KartMove's
    // jump, half-pipe and scale, KartState, KartCollide, KartPhysics with its dynamics and body
    // hitboxes, KartBody, each wheel's suspension, tire and their physics, ObjectCollisionKart,
    // the wheels' hitboxes, the model, and (in initImpl) the object collision hull. Only
    // addresses change: every allocation is still zeroed and constructed in the same order,
    // and nothing orders by address. After the model the block keeps room for the hull and
    // gives the rest back; initImpl trims it to size.
    // Previous scenes' arenas died with their heap.
    EGG::BlockArena::ForgetAll();
    for (size_t i = 0; i < m_count; ++i) {
        const auto &player = raceScenario.players[i];
        EGG::BlockArena *arena = s_kartArena ? EGG::BlockArena::Open(KART_ARENA_CAPACITY) : nullptr;
        if (arena) {
            arena->enter();
        }
        KartObject *object = KartObject::Create(player.character, player.vehicle, i);
        object->createModel();
        if (arena) {
            EGG::BlockArena::Leave();
            arena->trim(KART_ARENA_HULL_RESERVE);
        }
        m_objects[i] = object;
    }
}

/// @addr{0x8058FDD4}
KartObjectManager::~KartObjectManager() {
    if (s_instance) {
        s_instance = nullptr;
        WARN("KartObjectManager instance not explicitly handled!");
    }

    KartParamFileManager::DestroyInstance();

    for (size_t i = 0; i < m_count; ++i) {
        // mkw-3ds: the kart's parts ignore their frees inside its arena; the block goes last.
        EGG::BlockArena *arena = EGG::BlockArena::Containing(m_objects[i]);
        EGG::egg_delete(m_objects[i]);
        if (arena) {
            arena->release();
        }
    }

    EGG::egg_free(m_objects);

    EGG::egg_delete(s_thunderScaleUpAnmChr);
    EGG::egg_delete(s_thunderScaleDownAnmChr);
    EGG::egg_delete(s_megaScaleUpAnmChr);
    EGG::egg_delete(s_megaScaleDownAnmChr);
    EGG::egg_delete(s_pressScaleUpAnmChr);

    // If the proxy list is not cleared when we're done with the KartObjectManager, the list's
    // destructor calls delete on all of the links remaining in the list. Since the heaps are
    // gone by that point, this results in a segmentation fault. So, we clear the links here.
    KartObjectProxy::proxyList().clear();
}

/// @addr{0x8056AB6C}
void KartObjectManager::loadScaleAnimations() {
    auto *resMgr = System::ResourceManager::Instance();
    const void *file = resMgr->getFile("driver.brres", nullptr, System::ArchiveId::Core);
    ASSERT(file);

    // Copy construct onto the heap
    auto resAnmChr = Abstract::g3d::ResFile(file).resAnmChr("thunder_scale_up");
    s_thunderScaleUpAnmChr = EGG::egg_new<Abstract::g3d::ResAnmChr>(resAnmChr);
    resAnmChr = Abstract::g3d::ResFile(file).resAnmChr("thunder_scale_down");
    s_thunderScaleDownAnmChr = EGG::egg_new<Abstract::g3d::ResAnmChr>(resAnmChr);
    resAnmChr = Abstract::g3d::ResFile(file).resAnmChr("kinoko_scale_up");
    s_megaScaleUpAnmChr = EGG::egg_new<Abstract::g3d::ResAnmChr>(resAnmChr);
    resAnmChr = Abstract::g3d::ResFile(file).resAnmChr("kinoko_scale_down");
    s_megaScaleDownAnmChr = EGG::egg_new<Abstract::g3d::ResAnmChr>(resAnmChr);
    resAnmChr = Abstract::g3d::ResFile(file).resAnmChr("press_scale_up");
    s_pressScaleUpAnmChr = EGG::egg_new<Abstract::g3d::ResAnmChr>(resAnmChr);
}

Abstract::g3d::ResAnmChr *KartObjectManager::s_thunderScaleUpAnmChr = nullptr;
Abstract::g3d::ResAnmChr *KartObjectManager::s_thunderScaleDownAnmChr = nullptr;
Abstract::g3d::ResAnmChr *KartObjectManager::s_megaScaleUpAnmChr = nullptr;
Abstract::g3d::ResAnmChr *KartObjectManager::s_megaScaleDownAnmChr = nullptr;
Abstract::g3d::ResAnmChr *KartObjectManager::s_pressScaleUpAnmChr = nullptr;
KartObjectManager *KartObjectManager::s_instance = nullptr;
bool KartObjectManager::s_phaseMajor = true;
bool KartObjectManager::s_pass0Split = true;
bool KartObjectManager::s_pass1Grouped = true;
bool KartObjectManager::s_kartArena = true;

} // namespace Kinoko::Kart
