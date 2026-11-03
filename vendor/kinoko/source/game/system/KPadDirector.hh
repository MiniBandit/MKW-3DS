#pragma once

#include "game/system/KPadController.hh"

namespace Kinoko {

namespace Host {

class Context;

} // namespace Host

namespace System {

/// @brief The highest level abstraction for controller processing.
/// @addr{0x809BD70C}
class KPadDirector : EGG::Disposer {
    friend class Host::Context;

public:
    void calc();
    void calcPads();

    /// @addr{0x80523724}
    void clear() {}

    void reset();
    void startGhostProxies();
    void endGhostProxies();

    [[nodiscard]] const KPadPlayer &playerInput(size_t index = 0) const {
        ASSERT(index < m_playerCount);
        return m_playerInputs[index];
    }

    [[nodiscard]] KPadHostController *hostController(size_t index = 0) {
        ASSERT(index < m_playerCount);
        return m_hostControllers[index];
    }

    void setGhostPad(const u8 *inputs, bool driftIsAuto);
    void setHostPad(bool driftIsAuto, size_t index = 0);
    void setPlayerCount(size_t count) {
        ASSERT(count >= 1 && count <= m_playerInputs.size());
        m_playerCount = count;
    }

    static KPadDirector *CreateInstance();
    static void DestroyInstance();

    [[nodiscard]] static KPadDirector *Instance() {
        return s_instance;
    }

private:
    EGG_NEW_DELETE_FRIEND

    KPadDirector();
    ~KPadDirector() override;

    std::array<KPadPlayer, 12> m_playerInputs;
    size_t m_playerCount = 1;
    KPadGhostController *m_ghostController;
    std::array<KPadHostController *, 12> m_hostControllers;

    static KPadDirector *s_instance; ///< @addr{0x809BD70C}
};

} // namespace System

} // namespace Kinoko
