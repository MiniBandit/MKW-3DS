#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mkw {

enum class BattleMode : uint8_t { Balloon, Coin };

// Original Race/Common.szs/minigame.kmg. Course ids are the native 32..41
// order (RMCU KmgFile::getBattleCourseId table at 8088bdc8), independently
// of the menu's Wii/retro arena order. Columns are playerCount - 2.
struct BattleRules {
    static constexpr unsigned FirstArena = 32, ArenaCount = 10;
    static constexpr unsigned InitialBalloons = 3, MaxBalloons = 5;
    static constexpr unsigned RecoveryTicks = 150; // RMCU 808b1154

    uint32_t durationTicks = 180 * 60;
    uint16_t initialCoins = 0, maxCoins = 0;
    uint16_t coinWaveSize = 0;
    uint16_t coinReplenishThreshold = 0;
    int16_t enemyHit = 1, enemyDefeat = 0, allBalloonsLost = -1;
    int16_t hitByEnemy = 0, teammateHit = 0, hitByTeammate = 0;
    uint16_t fallCoinLoss = 3, hitCoinMinimum = 3, hitCoinPercent = 50;
    uint16_t coinStealCount = 3;

    bool load(const void* bytes, size_t size, unsigned nativeCourse,
              unsigned racers, BattleMode mode) {
        if (!bytes || size < 0x718 || nativeCourse < FirstArena ||
            nativeCourse >= FirstArena + ArenaCount || racers < 2 || racers > 12)
            return false;
        const auto* b = static_cast<const uint8_t*>(bytes);
        auto u16 = [&](size_t p) { return unsigned(b[p]) << 8 | b[p+1]; };
        auto s16 = [&](size_t p) { return int16_t(u16(p)); };
        const uint32_t declared = uint32_t(b[4]) << 24 | uint32_t(b[5]) << 16 |
                                  uint32_t(b[6]) << 8 | b[7];
        if (std::memcmp(b, "RKMG", 4) || declared != 0x718 || declared > size)
            return false;
        const size_t row = (nativeCourse - FirstArena) * 22 + (racers - 2) * 2;
        const int seconds = s16((mode == BattleMode::Coin ? 0x1dc : 0x10) + row);
        BattleRules next;
        next.durationTicks = seconds > 0 ? unsigned(seconds) * 60 : 0;
        next.initialCoins = u16(0x394 + row);
        next.maxCoins = u16(0x470 + row);
        next.coinWaveSize = u16(0x54c + row); // CoinManager +3, reserved-slot wave size
        next.coinReplenishThreshold = u16(0x628 + row); // +4, replenish when available count <= this
        next.enemyHit = s16(0x1c8);
        next.enemyDefeat = s16(0x1ca);
        next.allBalloonsLost = s16(0x1cc);
        next.hitByEnemy = s16(0x1ce);
        next.teammateHit = s16(0x1d0);
        next.hitByTeammate = s16(0x1d2);
        // Constructor 8086e91c..e950: +6 is hit minimum (712), +7
        // environment/fall and +8 transfer are both KMG 714. Stock values
        // coincide, so testing only the unmodified file hides swapped fields.
        next.hitCoinMinimum = u16(0x712);
        next.fallCoinLoss = u16(0x714);
        next.coinStealCount = u16(0x714);
        next.hitCoinPercent = u16(0x716);
        // CoinManager keeps these in byte fields. Reject a malformed asset
        // instead of publishing a half-loaded rule table or inventing values.
        if (!next.durationTicks || next.initialCoins > next.maxCoins ||
            next.maxCoins > 255 || !next.coinWaveSize || next.coinWaveSize > 255 ||
            next.coinReplenishThreshold > 255 || next.fallCoinLoss > 255 ||
            next.hitCoinMinimum > 255 || next.hitCoinPercent > 100 || next.coinStealCount > 255)
            return false;
        *this = next;
        return true;
    }

    unsigned coinsLost(unsigned held, bool fall = false) const {
        // RMCU CoinBattleRaceMode::onRemoveHit 80534f48..80534fd0:
        // ceil(percent * held / 100), at least the configured minimum.
        const unsigned request = fall ? fallCoinLoss :
            std::max<unsigned>(hitCoinMinimum, (held * hitCoinPercent + 99) / 100);
        return std::min(held, request);
    }
};

} // namespace mkw
