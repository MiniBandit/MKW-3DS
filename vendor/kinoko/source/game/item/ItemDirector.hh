#pragma once

#include "game/item/KartItem.hh"

#include <span>

namespace Kinoko {

namespace Host {

class Context;

} // namespace Host

/// @brief Pertains to item handling.
namespace Item {

/// @addr{0x809C3618}
class ItemDirector : EGG::Disposer {
    friend class Host::Context;

public:
    void init();
    void calc();

    [[nodiscard]] KartItem &kartItem(size_t idx) {
        ASSERT(idx < m_karts.size());
        return m_karts[idx];
    }

    static ItemDirector *CreateInstance();
    static void DestroyInstance();
    // Retail course callbacks create dropped pickups directly. The port's
    // race item controller owns the physical actor, not the time-trial holder.
    using CoursePickupHook = void (*)(void *, const EGG::Vector3f &, const EGG::Vector3f &, u8);
    static void SetCoursePickupHook(CoursePickupHook hook, void *context);
    static void ClearCoursePickupHook(void *context);
    static void CreateCourseMushroom(const EGG::Vector3f &position,
            const EGG::Vector3f &velocity, u8 owner);

    using CourseDropHook = void (*)(void *, u16, const EGG::Vector3f &, const EGG::Vector3f &, u8);
    static void SetCourseDropHook(CourseDropHook hook, void *context);
    static void ClearCourseDropHook(void *context);
    static void CreateCourseDrop(u16 slot, const EGG::Vector3f &position,
            const EGG::Vector3f &velocity, u8 owner);

    [[nodiscard]] const ItemInventory &itemInventory(s16 idx) const {
        return m_karts[idx].inventory();
    }

    [[nodiscard]] static ItemDirector *Instance() {
        return s_instance;
    }

private:
    EGG_NEW_DELETE_FRIEND

    ItemDirector();
    ~ItemDirector() override;

    owning_span<KartItem> m_karts;

    static ItemDirector *s_instance; ///< @addr{0x809C3618}
    static CoursePickupHook s_pickupHook;
    static void *s_pickupContext;
    static CourseDropHook s_dropHook;
    static void *s_dropContext;
};

} // namespace Item

} // namespace Kinoko
