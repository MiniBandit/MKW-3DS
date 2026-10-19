#pragma once
#include "mkw/math.hpp"
#include "mkw/wheel_motion.hpp"
#include "mkw/luma_motion.hpp"
#include <citro3d.h>
#include <algorithm>
#include <array>
namespace Kinoko::Kart { class KartObject; }
namespace mkw {
// Presentation-only, sampled AFTER each fixed 60 Hz race step. Never advances
// or writes Kinoko physics. Original chassis pose already includes lean/wheelie.
// step() keeps the per-tick wheel roll and snapshots its kart inputs; the
// matrices are built from that snapshot on first use, so racers that are not
// drawn (or ticks that are not presented) cost no matrix work.
class VehicleAnimation {
public:
    VehicleAnimation(){reset();}
    void reset();
    void step(Kinoko::Kart::KartObject& kart);
    // Complete visual input, without retaining a KartObject or race heap.
    // Positions and wheel frames are world-space; display is the original
    // kartPartsDispParam/bikePartsDispParam row, decoded to host floats.
    struct Snapshot {
        C3D_Mtx body{},handle{};
        std::array<C3D_Mtx,4> wheelFrames{};
        std::array<Vec3,4> tirePositions{};
        std::array<float,4> radius{},sourceRadius{},travel{},roll{};
        std::array<float,84> display{};
        Vec3 scale{1,1,1};float steering=0;
        unsigned vehicle=0,count=0;bool bike=false;
    };
    Snapshot snapshot() const;
    // Source initial suspension state. Chassis already includes original
    // floor snap and BSP initialHeight; this does not guess collision height.
    static bool initialSnapshot(unsigned vehicle,const C3D_Mtx& chassis,Snapshot& result);
    static float initialHeight(unsigned vehicle);
    // Call after the preceding GPU frame has retired, before posing/drawing.
    bool setSnapshot(const Snapshot& pose);
    const C3D_Mtx& body() const {evaluate();return body_;}
    // The kart's position and largest scale this tick, without building any matrix.
    Vec3 position() const {return {base_.r[0].w,base_.r[1].w,base_.r[2].w};}
    float maxScale() const {return std::max({kartScale_.x,kartScale_.y,kartScale_.z});}
    const LumaMotion& lumaMotion() const {return luma_;}
    Vec3 visualScale() const {return kartScale_;}
    unsigned lumaFrame() const {return lumaFrame_;}
    const C3D_Mtx& handle() const {evaluate();return handle_;}
    const C3D_Mtx& handleBase() const {evaluate();return handleBase_;}
    const C3D_Mtx& wheel(unsigned i) const {evaluate();return wheels_[i<4?i:0];}
    unsigned wheelCount() const {return count_;}
    unsigned modelIndex(unsigned i) const {return bike_?i%2:count_==3?i+1:i;}
    float roll(unsigned i) const {return roll_[i<4?i:0];}
    float suspensionTravel(unsigned i) const {return travel_[i<4?i:0];}
    float effectiveRadius(unsigned i) const {return radii_[i<4?i:0];}
    float driveFrame() const {evaluate();return driveFrame_;}
    bool accessorySupported(const char* name) const;
    unsigned accessoryCount(const char* name) const;
    // sourceAnimated must be true when applying handle_drive CHR0: that clip
    // contains steering, so the attachment must omit the same steering angle.
    C3D_Mtx accessory(const char* name,bool sourceAnimated=false,unsigned instance=0) const;
    bool finite() const;
    static float advanceRoll(float phase,float signedDistance,float radius);
private:
    void readParameters(unsigned vehicle);
    void evaluate() const;
    void updateAccessories() const;
    // Per-tick snapshot of the kart (step) and the derived matrices (evaluate).
    C3D_Mtx base_{},handleSource_{};
    std::array<C3D_Mtx,4> wheelSources_{};
    std::array<Vec3,4> tirePositions_{};
    std::array<float,4> bspRadii_{};
    Vec3 kartScale_{1,1,1};
    float steering_=0;
    WheelMotion wheelMotion_{};
    LumaMotion luma_{};
    unsigned lumaFrame_=0;
    mutable bool dirty_=false;
    mutable C3D_Mtx body_{},handle_{},handleBase_{};
    mutable std::array<C3D_Mtx,4> wheels_{},wheelFrames_{};
    mutable std::array<C3D_Mtx,9*4> accessories_{};
    mutable uint64_t accessorySet_=0;
    mutable std::array<Vec3,4> wheelLocal_{};
    std::array<float,4> roll_{},travel_{},radii_{};
    std::array<float,84> display_{};
    unsigned count_=0,vehicle_=~0u;
    bool bike_=false;
    mutable float driveFrame_=15;
    Vec3 displayVec(unsigned offset) const;
};
}
