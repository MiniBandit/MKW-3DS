#include "ItemDirector.hh"

#include "game/system/RaceConfig.hh"

namespace Kinoko::Item {
ItemDirector::CoursePickupHook ItemDirector::s_pickupHook = nullptr;
void *ItemDirector::s_pickupContext = nullptr;

void ItemDirector::SetCoursePickupHook(CoursePickupHook hook, void *context) {
    s_pickupHook = hook; s_pickupContext = context;
}
void ItemDirector::ClearCoursePickupHook(void *context) {
    if (context == s_pickupContext) { s_pickupHook = nullptr; s_pickupContext = nullptr; }
}
void ItemDirector::CreateCourseMushroom(const EGG::Vector3f &position,
        const EGG::Vector3f &velocity, u8 owner) {
    if (s_pickupHook) s_pickupHook(s_pickupContext, position, velocity, owner);
}

ItemDirector::CourseDropHook ItemDirector::s_dropHook = nullptr;
void *ItemDirector::s_dropContext = nullptr;
void ItemDirector::SetCourseDropHook(CourseDropHook hook, void *context) {
    s_dropHook = hook; s_dropContext = context;
}
void ItemDirector::ClearCourseDropHook(void *context) {
    if (context == s_dropContext) { s_dropHook = nullptr; s_dropContext = nullptr; }
}
void ItemDirector::CreateCourseDrop(u16 slot, const EGG::Vector3f &position,
        const EGG::Vector3f &velocity, u8 owner) {
    if (s_dropHook) s_dropHook(s_dropContext, slot, position, velocity, owner);
}

/// @addr{0x80799794}
void ItemDirector::init() {
    for (auto &kart : m_karts) {
        kart.inventory().setItem(ItemId::TRIPLE_MUSHROOM);
    }
}

/// @addr{0x80799850}
void ItemDirector::calc() {
    for (auto &kart : m_karts) {
        kart.calc();
    }
}

/// @addr{0x80799138}
ItemDirector *ItemDirector::CreateInstance() {
    ASSERT(!s_instance);
    s_instance = EGG::egg_new<ItemDirector>();
    return s_instance;
}

/// @addr{0x80799188}
void ItemDirector::DestroyInstance() {
    ASSERT(s_instance);
    auto *instance = s_instance;
    s_instance = nullptr;
    EGG::egg_delete(instance);
}

/// @addr{0x807992D8}
ItemDirector::ItemDirector() {
    size_t playerCount = System::RaceConfig::Instance()->raceScenario().playerCount;
    m_karts = owning_span<KartItem>(playerCount);

    for (size_t i = 0; i < playerCount; ++i) {
        m_karts[i].init(i);
    }
}

/// @addr{0x80798F9C}
ItemDirector::~ItemDirector() {
    if (s_instance) {
        s_instance = nullptr;
        WARN("ItemDirector instance not explicitly handled!");
    }
}

ItemDirector *ItemDirector::s_instance = nullptr; ///< @addr{0x809C3618}

} // namespace Kinoko::Item
