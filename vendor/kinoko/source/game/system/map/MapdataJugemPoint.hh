#pragma once

#include "game/system/map/MapdataAccessorBase.hh"

#include <egg/math/Vector.hh>

namespace Kinoko::System {

/// @brief This class represents the course's respawn positions.
class MapdataJugemPoint {
public:
    struct SData {
        EGG::Vector3f pos;
        EGG::Vector3f rot;
        u8 _18[0x1c - 0x18];
    };
    static_assert(sizeof(SData) == 0x1c);

    MapdataJugemPoint(const SData *data);
    void read(EGG::Stream &stream);

    /// @beginGetters
    const EGG::Vector3f &pos() const {
        return m_pos;
    }

    const EGG::Vector3f &rot() const {
        return m_rot;
    }

    /// @brief The sound zone a respawn here applies, plus one (0: none): the hundreds of
    /// the big-endian s16 at +0x1a (KMP::Holder<JGPT> ctor, USA 0x80513ff4).
    s16 soundZone() const {
        const s16 range = static_cast<s16>((m_rawData->_18[2] << 8) | m_rawData->_18[3]);
        return range < 0 ? 0 : static_cast<s16>(range / 100);
    }
    // Holder<JGPT> USA80513ff4: low two decimal digits are Battle's
    // availability group; 99 and negative values are always available.
    s16 battleGroup() const {
        const s16 raw = static_cast<s16>((m_rawData->_18[2] << 8) | m_rawData->_18[3]);
        return raw < 0 || raw % 100 == 99 ? -1 : raw % 100;
    }

    /// @brief Retail +0x2a (ctor 0x805183A8): the respawn area, the s16 at +0x1a modulo 100,
    /// -1 when that s16 is negative or its remainder is 99. Battle respawns skip points whose
    /// area is disabled (fn_1_28E30, 0x805391E4).
    s16 areaId() const {
        const s16 range = static_cast<s16>((m_rawData->_18[2] << 8) | m_rawData->_18[3]);
        if (range < 0) {
            return -1;
        }
        const s16 area = static_cast<s16>(range % 100);
        return area == 99 ? -1 : area;
    }

    /// @brief Retail +0x10: rot * -ez, the respawn grid's row axis (fn_1_85B8, 0x8051896C).
    const EGG::Vector3f &gridBack() const {
        return m_gridBack;
    }

    /// @brief Retail +0x1c: rot * -ex, the respawn grid's lane axis (fn_1_85B8, 0x8051896C).
    const EGG::Vector3f &gridSide() const {
        return m_gridSide;
    }
    /// @endGetters

    void findRespawnPos(EGG::Vector3f &pos, u8 playerIdx) const;

private:
    [[maybe_unused]] const SData *m_rawData;
    EGG::Vector3f m_pos;
    EGG::Vector3f m_rot;
    EGG::Vector3f m_gridBack;
    EGG::Vector3f m_gridSide;
};

class MapdataJugemPointAccessor
    : public MapdataAccessorBase<MapdataJugemPoint, MapdataJugemPoint::SData> {
public:
    MapdataJugemPointAccessor(const MapSectionHeader *header);
    ~MapdataJugemPointAccessor() override;
};

} // namespace Kinoko::System
