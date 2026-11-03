#pragma once
#include "game/field/obj/ObjectCollidable.hh"
namespace Kinoko::Field {
class ObjectBGTeresa final : public ObjectCollidable {
public:
    explicit ObjectBGTeresa(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}
    void load() override;
    void init() override;
    void calc() override;
    u32 loadFlags() const override { return 1; }
    void createCollision() override {}
    void calcCollisionTransform() override {}
    u8 alpha() const { return m_alpha; }
    bool mirrorU() const { return m_mirrorU; }
private:
    void appear();
    enum class State { FadeIn, Visible, FadeOut, Hidden };
    State m_state = State::Hidden;
    EGG::Vector3f m_origin{}, m_offset{}, m_amplitude{};
    f32 m_motionAngle = 0, m_alphaAngle = 0, m_alphaSpeed = 1;
    s32 m_delay = 0, m_countdown = 0, m_lifetime = 0;
    u8 m_alpha = 0;
    bool m_mirrorU = false;
};
}
