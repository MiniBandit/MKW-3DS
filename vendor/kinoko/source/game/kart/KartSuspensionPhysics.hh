#pragma once

#include "game/field/KColData.hh"
#include "game/kart/CollisionGroup.hh"
#include "game/kart/KartObjectProxy.hh"
#include "game/kart/KartParam.hh"

#include <egg/math/Matrix.hh>

namespace Kinoko::Kart {

/// @brief Manages wheel physics and collision checks.
/// @nosubgrouping
class WheelPhysics : KartObjectProxy {
public:
    WheelPhysics(u16 wheelIdx, u16 bspWheelIdx);
    ~WheelPhysics();

    void init();
    void initBsp();
    void reset();

    void realign(const EGG::Vector3f &bottom, const EGG::Vector3f &vehicleMovement);

    void updateCollision(const EGG::Vector3f &bottom, const EGG::Vector3f &topmostPos);
    void calcSuspension(const EGG::Vector3f &forward);

    /// mkw-3ds: the course query updateCollision would make with the wheel at pos
    /// (KartSub's batched wheels); false when it would make none.
    [[nodiscard]] bool predictCollision(const EGG::Vector3f &pos, const EGG::Vector3f &bottom,
            Field::KColData::BatchSphere &out) const;

    /// @beginSetters
    void setSuspTravel(f32 suspTravel) {
        m_suspTravel = suspTravel;
    }

    void setPos(const EGG::Vector3f &pos) {
        m_pos = pos;
    }

    void setLastPos(const EGG::Vector3f &pos) {
        m_lastPos = pos;
    }

    void setLastPosDiff(const EGG::Vector3f &pos) {
        m_lastPosDiff = pos;
    }

    void setWheelEdgePos(const EGG::Vector3f &pos) {
        m_wheelEdgePos = pos;
    }

    void setColVel(const EGG::Vector3f &vec) {
        m_colVel = vec;
    }
    /// @endSetters

    /// @beginGetters
    [[nodiscard]] const EGG::Vector3f &pos() const {
        return m_pos;
    }

    [[nodiscard]] const EGG::Vector3f &lastPosDiff() const {
        return m_lastPosDiff;
    }

    [[nodiscard]] f32 suspTravel() {
        return m_suspTravel;
    }

    [[nodiscard]] const EGG::Vector3f &topmostPos() const {
        return m_topmostPos;
    }

    [[nodiscard]] CollisionGroup *hitboxGroup() {
        return m_hitboxGroup;
    }

    [[nodiscard]] const CollisionGroup *hitboxGroup() const {
        return m_hitboxGroup;
    }

    [[nodiscard]] const EGG::Vector3f &speed() const {
        return m_speed;
    }

    [[nodiscard]] const EGG::Vector3f &wheelEdgePos() const {
        return m_wheelEdgePos;
    }

    [[nodiscard]] f32 effectiveRadius() const {
        return m_effectiveRadius;
    }

    [[nodiscard]] f32 _74() const {
        return m_74;
    }
    /// @endGetters

private:
    [[nodiscard]] EGG::Vector3f collisionCenter(const EGG::Vector3f &pos,
            const EGG::Vector3f &bottom) const;

    u16 m_wheelIdx;
    u16 m_bspWheelIdx;
    const BSP::Wheel *m_bspWheel;
    CollisionGroup *m_hitboxGroup;
    EGG::Vector3f m_pos;
    EGG::Vector3f m_lastPos;
    EGG::Vector3f m_lastPosDiff;
    f32 m_suspTravel;
    EGG::Vector3f m_colVel;
    EGG::Vector3f m_speed;
    EGG::Vector3f m_wheelEdgePos;
    f32 m_effectiveRadius;
    f32 m_targetEffectiveRadius;
    f32 m_74;
    EGG::Vector3f m_topmostPos;
};

/// @brief Physics for a single wheel's suspension.
class KartSuspensionPhysics : KartObjectProxy {
public:
    /// @brief Every other kart tire is a mirror of the first. Bikes do not leverage this.
    enum class TireType {
        Kart,
        KartReflected,
        Bike,
    };

    KartSuspensionPhysics(u16 wheelIdx, TireType TireType, u16 bspWheelIdx);
    ~KartSuspensionPhysics();

    void init();
    void reset();

    void setInitialState();

    void calcCollision(f32 dt, const EGG::Vector3f &gravity, const EGG::Matrix34f &mat);
    void calcSuspension(const EGG::Vector3f &forward, const EGG::Vector3f &vehicleMovement);

    /// mkw-3ds: the course query calcCollision would make with this wheel matrix,
    /// without changing anything (same expressions); false when it would make none.
    [[nodiscard]] bool predictCollision(const EGG::Matrix34f &mat,
            Field::KColData::BatchSphere &out);

    /// mkw-3ds (Old 3DS): KartSub batches the wheels' course prism tests
    /// (KColData::batchSpheresCached).
    /// Off by default (more instructions than it saves); on: batch-wheels.txt;
    /// DIAG ab-alternate.txt bit 4.
    static bool s_batchWheels;
    /// mkw-3ds (Old 3DS): each wheel keeps its fixed rotated down vector instead of
    /// rebuilding it (makeR: three sines and cosines) from the BSP xRot every tick:
    /// same inputs, same values. Off: no-wheel-down-cache.txt; DIAG ab-alternate.txt bit 7.
    static bool s_keepWheelDown;

private:
    void wheelFrame(const EGG::Matrix34f &mat, EGG::Vector3f &topmostPos,
            EGG::Vector3f &bottomDir);

    const BSP::Wheel *m_bspWheel;
    WheelPhysics *m_tirePhysics;
    TireType m_tireType;
    u16 m_bspWheelIdx;
    u16 m_wheelIdx;
    EGG::Vector3f m_topmostPos;
    f32 m_maxTravelScaled;
    EGG::Vector3f m_bottomDir;
    EGG::Vector3f m_localBottom;      ///< mkw-3ds: makeR(xRot) applied to -y, once.
    bool m_localBottomReady = false;
};

} // namespace Kinoko::Kart
