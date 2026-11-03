#pragma once
#include "game/field/obj/ObjectCollidable.hh"
#include "game/field/obj/ObjectKCL.hh"

namespace Kinoko::Field {
// Delfino Pier: retail USA8081aa2c/8081b320, not Peach Beach's Psea cycle.
class ObjectVeniceWater final : public ObjectCollidable {
public:
    explicit ObjectVeniceWater(const System::MapdataGeoObj &params);
    void calc() override;
    u32 loadFlags() const override { return 1; }
    void createCollision() override {}
    f32 baseHeight(u32 time) const;
    f32 layerHeight(unsigned layer,u32 time) const;
private:
    f32 m_baseY,m_rate,m_drift,m_first,m_second,m_final;
    s32 m_firstFrames,m_firstDrift,m_secondFrames,m_secondDrift,m_finalFrames;
};
class ObjectVeniceGate final : public ObjectKCL {
public:
    explicit ObjectVeniceGate(const System::MapdataGeoObj &params);
    void init() override;
    void calc() override;
    u32 loadFlags() const override { return 1; }
    const EGG::Matrix34f &getUpdatedMatrix(u32 timeOffset) override;
    f32 height(u32 time) const;
private:
    EGG::Vector3f m_origin,m_angles;
    s32 m_start;
    f32 m_rise;
    bool m_stopped=false;
    EGG::Matrix34f m_matrix;
};
class ObjectVeniceBridge final : public ObjectKCL {
public:
    explicit ObjectVeniceBridge(const System::MapdataGeoObj &params);
    ~ObjectVeniceBridge() override;
    void createCollision() override;
    void init() override;
    void calc() override;
    u32 loadFlags() const override { return 1; }
    const EGG::Matrix34f &getUpdatedMatrix(u32 timeOffset) override;
    f32 animationFrame() const;
private:
    EGG::Vector3f m_origin,m_angles;
    s32 m_start;
    f32 m_frameTime;
    ObjColMgr *m_main=nullptr,*m_before=nullptr;
    EGG::Matrix34f m_matrix;
    bool m_ended=false;
};
}
