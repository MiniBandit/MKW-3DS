#pragma once

#include "game/kart/KartObject.hh"

#include <abstract/g3d/ResAnmChr.hh>

namespace Kinoko {

namespace Host {

class Context;

} // namespace Host

namespace Kart {

/// @brief Responsible for the lifecycle and calculation of KartObjects.
/// @addr{0x809C18F8}
/// @nosubgrouping
class KartObjectManager : EGG::Disposer {
    friend class Host::Context;

public:
    void init();
    void calc();

    /// @beginGetters
    /// @addr{0x80590100}
    [[nodiscard]] KartObject *object(size_t i) const {
        ASSERT(i < m_count);
        return m_objects[i];
    }
    /// @endGetters

    [[nodiscard]] size_t count() const { return m_count; }

    /// mkw-3ds: pass 0 phase-major (default on; DIAG ab-alternate.txt switches it).
    static bool s_phaseMajor;
    /// mkw-3ds (Old 3DS): with s_phaseMajor, pass 0 in four parts instead of two
    /// (KartMove's first half split after calcDirs, the second half after the pull
    /// path), each run for every kart of a group in turn. Off: no-pass0-split.txt;
    /// DIAG ab-alternate.txt bit 6.
    static bool s_pass0Split;
    /// mkw-3ds (Old 3DS): pass 1 grouped like pass 0: the serial part (box search,
    /// racer pairs, objects, items) for every kart in order, then the local part
    /// (body and wheel collision, suspension, model) for every kart in order.
    /// Off: no-pass1-group.txt; DIAG ab-alternate.txt bit 8.
    static bool s_pass1Grouped;
    /// mkw-3ds (Old 3DS): each kart's parts in one heap block (EGG::BlockArena), applied when
    /// the karts are created. Off: no-kart-arena.txt (no DIAG ab-alternate bit: it cannot
    /// change mid-race).
    static bool s_kartArena;
    /// Block taken per kart (a kart holds 5.4-6.6 KB on the 3DS); what is not used goes back
    /// to the heap once the kart is built. A part that does not fit comes from the heap.
    static constexpr size_t KART_ARENA_CAPACITY = 0x4000;
    /// Room kept after the model for initImpl's object collision hull.
    static constexpr size_t KART_ARENA_HULL_RESERVE = 0x200;

    static KartObjectManager *CreateInstance();
    static void DestroyInstance();

    [[nodiscard]] static const Abstract::g3d::ResAnmChr *ThunderScaleUpAnmChr() {
        return s_thunderScaleUpAnmChr;
    }

    [[nodiscard]] static const Abstract::g3d::ResAnmChr *ThunderScaleDownAnmChr() {
        return s_thunderScaleDownAnmChr;
    }

    [[nodiscard]] static const Abstract::g3d::ResAnmChr *MegaScaleUpAnmChr() {
        return s_megaScaleUpAnmChr;
    }

    [[nodiscard]] static const Abstract::g3d::ResAnmChr *MegaScaleDownAnmChr() {
        return s_megaScaleDownAnmChr;
    }

    [[nodiscard]] static const Abstract::g3d::ResAnmChr *PressScaleUpAnmChr() {
        return s_pressScaleUpAnmChr;
    }

    [[nodiscard]] static KartObjectManager *Instance() {
        return s_instance;
    }

private:
    EGG_NEW_DELETE_FRIEND

    KartObjectManager();
    ~KartObjectManager() override;

    void loadScaleAnimations();

    size_t m_count;
    KartObject **m_objects;

    static Abstract::g3d::ResAnmChr *s_thunderScaleUpAnmChr;   ///< @addr{0x809C18A0}
    static Abstract::g3d::ResAnmChr *s_thunderScaleDownAnmChr; ///< @addr{0x809C18A4}
    static Abstract::g3d::ResAnmChr *s_megaScaleUpAnmChr;
    static Abstract::g3d::ResAnmChr *s_megaScaleDownAnmChr;
    static Abstract::g3d::ResAnmChr *s_pressScaleUpAnmChr;     ///< @addr{0x809C18B0}
    static KartObjectManager *s_instance;                      ///< @addr{0x809C18F8}
};

} // namespace Kart

} // namespace Kinoko
