#pragma once

#include "game/item/ItemInventory.hh"

#include "game/kart/KartObjectProxy.hh"

#include <egg/core/BitFlag.hh>

namespace Kinoko::Item {

/// @brief State management for item usage
/// @nosubgrouping
class KartItem : Kart::KartObjectProxy {
public:
    KartItem();
    ~KartItem();

    void init(size_t playerIdx);
    void calc();
    void clear();

    void activateMushroom();
    void useMushroom();

    /// @brief Retail ItemHolderPlayer damage hooks, called from kart action starts and Lightning.
    /// Strong loss (RMCU fn_28867C) drops the inventory, roulette and tethered items; weak loss
    /// (fn_288774) drops tethered items, only a single trailing one when `trailOnly` is set.
    /// Time trials hold only mushrooms, so strong loss clears the inventory as before and weak
    /// loss has nothing to drop. The race item controller consumes the recorded requests.
    void loseFromDamage(u8 attacker);
    void loseTethered(bool trailOnly, u8 attacker);

    struct LossRequests {
        bool strong = false;     ///< Inventory, roulette and every tethered item.
        bool tethered = false;   ///< Every tethered item (action 5).
        bool trailOnly = false;  ///< A single trailing item only.
        u8 attacker = 0xc;
    };
    [[nodiscard]] LossRequests takeLossRequests() {
        LossRequests out = m_loss;
        m_loss = {};
        return out;
    }

    /// @beginGetters
    [[nodiscard]] ItemInventory &inventory() {
        return m_inventory;
    }

    [[nodiscard]] const ItemInventory &inventory() const {
        return m_inventory;
    }
    /// @endGetters

private:
    enum class eFlags {
        Lockout = 10,
        ItemButtonHold = 12,
        ItemButtonActivation = 14,
    };
    typedef EGG::TBitFlag<u16, eFlags> Flags;

    Flags m_flags;
    ItemInventory m_inventory;
    LossRequests m_loss;
};

} // namespace Kinoko::Item
