#pragma once

#include <array>

#include "game/field/obj/ObjectCollidable.hh"

namespace Kinoko::Field {

class ObjectCarTGE;

class ObjectHighwayManager : public ObjectCollidable {
public:
    ObjectHighwayManager();
    ~ObjectHighwayManager() override;

    void init() override;
    void calc() override;

    /// @addr{0x806D5C6C}
    [[nodiscard]] u32 loadFlags() const override {
        return 1;
    }

    /// @addr{0x806D5C68}
    void loadGraphics() override {}

    /// @addr{0x806D5C60}
    void createCollision() override {}

    /// @addr{0x806D5C64}
    void loadRail() override {}

    /// Frames since this player was last squashed by a car (retail +0x120, one per player).
    [[nodiscard]] u32 squashTimer(size_t playerIdx) const {
        return playerIdx < m_squashTimer.size() ? m_squashTimer[playerIdx] : SQUASH_MAX;
    }

    static constexpr size_t MAX_PLAYERS = 12;

private:
    void calcSquash();

    owning_span<ObjectCarTGE *> m_cars;
    std::array<u32, MAX_PLAYERS> m_squashTimer; ///< One per player, as retail.

    static constexpr u32 SQUASH_MAX = 600;
};

} // namespace Kinoko::Field
