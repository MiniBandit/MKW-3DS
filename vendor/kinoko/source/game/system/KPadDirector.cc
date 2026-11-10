#include "KPadDirector.hh"

namespace Kinoko::System {

/// @addr{0x805238F0}
void KPadDirector::calc() {
    calcPads();
    for (size_t i = 0; i < m_playerCount; ++i) m_playerInputs[i].calc();
}

/// @addr{0x805237E8}
void KPadDirector::calcPads() {
    for (auto *ghost : m_ghostControllers) {
        if (ghost) {
            ghost->calc();
        }
    }
    for (size_t i = 0; i < m_playerCount; ++i) m_hostControllers[i]->calc();
}

/// @addr{0x80523690}
void KPadDirector::reset() {
    for (size_t i = 0; i < m_playerCount; ++i) m_playerInputs[i].reset();
}

/// @addr{0x80524580}
void KPadDirector::startGhostProxies() {
    for (size_t i = 0; i < m_playerCount; ++i) m_playerInputs[i].startGhostProxy();
}

/// @addr{0x805245DC}
void KPadDirector::endGhostProxies() {
    for (size_t i = 0; i < m_playerCount; ++i) m_playerInputs[i].endGhostProxy();
}

/// @addr{0x8052453C}
void KPadDirector::setGhostPad(const u8 *inputs, bool driftIsAuto, size_t index) {
    ASSERT(index < m_playerInputs.size());
    if (!m_ghostControllers[index]) {
        m_ghostControllers[index] = EGG::egg_new<KPadGhostController>();
    }
    m_playerInputs[index].setGhostController(m_ghostControllers[index], inputs, driftIsAuto);
}

void KPadDirector::setHostPad(bool driftIsAuto, size_t index) {
    ASSERT(index < m_playerCount);
    m_hostControllers[index]->reset(driftIsAuto);
    m_playerInputs[index].setHostController(m_hostControllers[index], driftIsAuto);
}

/// @addr{0x8052313C}
KPadDirector *KPadDirector::CreateInstance() {
    ASSERT(!s_instance);
    return s_instance = EGG::egg_new<KPadDirector>();
}

/// @addr{0x8052318C}
void KPadDirector::DestroyInstance() {
    ASSERT(s_instance);
    auto *instance = s_instance;
    s_instance = nullptr;
    EGG::egg_delete(instance);
}

/// @addr{0x805232F0}
KPadDirector::KPadDirector() {
    m_ghostControllers[0] = EGG::egg_new<KPadGhostController>();
    for (auto &controller : m_hostControllers) controller = EGG::egg_new<KPadHostController>();
}

/// @addr{0x805231DC}
KPadDirector::~KPadDirector() {
    for (auto *ghost : m_ghostControllers) {
        if (ghost) {
            EGG::egg_delete(ghost);
        }
    }
    for (auto *controller : m_hostControllers) EGG::egg_delete(controller);
    if (s_instance) {
        s_instance = nullptr;
        WARN("KPadDirector instance not explicitly handled!");
    }
}

KPadDirector *KPadDirector::s_instance = nullptr; ///< @addr{0x809BD70C}

} // namespace Kinoko::System
