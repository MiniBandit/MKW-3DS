#pragma once

#include "game/field/ObjectCollisionConvexHull.hh"

namespace Kinoko {

namespace Kart {

class KartObject;

} // namespace Kart

namespace Field {

/// @brief Relates a KartObject with its convex hull representation.
class ObjectCollisionKart {
public:
    ObjectCollisionKart();
    ~ObjectCollisionKart();

    void init(u32 idx);

    size_t checkCollision(const EGG::Matrix34f &mat, const EGG::Vector3f &v);

    [[nodiscard]] static EGG::Vector3f GetHitDirection(u16 objKartHit);
    [[nodiscard]] static constexpr std::span<const EGG::Vector3f> GetVehicleVertices(
            Vehicle vehicle);
    [[nodiscard]] static const EGG::Vector3f &translation(size_t idx);

    /// The hull as last transformed by checkCollision (item contacts reuse it in the same pass).
    [[nodiscard]] ObjectCollisionConvexHull *hull() const {
        return m_hull;
    }

private:
    ObjectCollisionConvexHull *m_hull;
    Kart::KartObject *m_kartObject;
    u32 m_playerIdx;
};

} // namespace Field

} // namespace Kinoko
