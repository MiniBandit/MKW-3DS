#include "MapdataJugemPoint.hh"

#include "game/system/CourseMap.hh"
#include "game/system/RaceConfig.hh"

#include <egg/math/Math.hh>
#include <egg/math/Quat.hh>

namespace Kinoko::System {

/// @addr{0x805183A8}
MapdataJugemPoint::MapdataJugemPoint(const SData *data) : m_rawData(data) {
    EGG::RamStream stream = EGG::RamStream(data, sizeof(SData));
    read(stream);

    // Retail keeps rot * DEG2RAD (+0x4) and turns -ez and -ex by setRPY of it.
    EGG::Quatf rpy;
    rpy.setRPY(DEG2RAD * m_rot.x, DEG2RAD * m_rot.y, DEG2RAD * m_rot.z);
    m_gridBack = rpy.rotateVector(-EGG::Vector3f::ez);
    m_gridSide = rpy.rotateVector(-EGG::Vector3f::ex);
}

void MapdataJugemPoint::read(EGG::Stream &stream) {
    m_pos.read(stream);
    m_rot.read(stream);
}

/// @addr{0x8051896C}
/// @brief The racer's respawn position at this point: the point itself in Time Trial and
/// Ghost Race, otherwise one of 12 grid slots (4 lanes of 150 units, 3 rows of 300).
void MapdataJugemPoint::findRespawnPos(EGG::Vector3f &pos, u8 playerIdx) const {
    // s8 pairs at 0x8088FA08: lane (x150 along rot * -ex), row (x300 along rot * -ez).
    static constexpr s8 GRID[12][2] = {{-1, 0}, {1, 0}, {-3, 0}, {3, 0}, {-1, 1}, {1, 1},
            {-3, 1}, {3, 1}, {-1, 2}, {1, 2}, {-3, 2}, {3, 2}};
    constexpr f32 LANE_WIDTH = 150.0f; // 0x8088FA84
    constexpr f32 ROW_DEPTH = 300.0f;  // 0x8088FA88

    using GameMode = RaceConfig::Scenario::GameMode;
    const GameMode mode = RaceConfig::Instance()->raceScenario().gameMode;
    if (mode == GameMode::Time_Trial || mode == GameMode::Ghost_Race) {
        playerIdx = 0xff;
    }

    if (playerIdx == 0xff) {
        pos = m_pos;
        return;
    }

    ASSERT(playerIdx < 12);
    const EGG::Vector3f lane = m_gridSide * (LANE_WIDTH * static_cast<f32>(GRID[playerIdx][0]));
    const EGG::Vector3f row = m_gridBack * (ROW_DEPTH * static_cast<f32>(GRID[playerIdx][1]));
    pos = m_pos + (lane + row);
}

/// @addr{Inlined at 0x805130C4}
MapdataJugemPointAccessor::MapdataJugemPointAccessor(const MapSectionHeader *header)
    : MapdataAccessorBase<MapdataJugemPoint, MapdataJugemPoint::SData>(header) {
    init(reinterpret_cast<const MapdataJugemPoint::SData *>(m_sectionHeader + 1),
            parse<u16>(m_sectionHeader->count));
}

MapdataJugemPointAccessor::~MapdataJugemPointAccessor() = default;

} // namespace Kinoko::System
