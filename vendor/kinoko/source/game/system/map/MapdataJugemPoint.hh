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
    /// @endGetters

private:
    [[maybe_unused]] const SData *m_rawData;
    EGG::Vector3f m_pos;
    EGG::Vector3f m_rot;
};

class MapdataJugemPointAccessor
    : public MapdataAccessorBase<MapdataJugemPoint, MapdataJugemPoint::SData> {
public:
    MapdataJugemPointAccessor(const MapSectionHeader *header);
    ~MapdataJugemPointAccessor() override;
};

} // namespace Kinoko::System
