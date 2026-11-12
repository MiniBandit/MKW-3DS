#pragma once
#include "game/field/obj/ObjectCollidable.hh"
#include <mkw/object_animation_sound.hpp>

namespace Kinoko::Field {
// Bowser's Castle statue, distinct from the N64 fire-breathing decoration.
class ObjectKoopaFigure final : public ObjectCollidable {
public:
    explicit ObjectKoopaFigure(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}
    void load() override;
    void init() override;
    void calc() override;
    u32 loadFlags() const override { return 1; }
    void createCollision() override {}
    void calcCollisionTransform() override {}
    bool vomiting() const { return m_vomiting; }
    f32 animationFrame() const { return m_frame; }
    u32 breathSerial() const { return m_breathSerial; }
    static s32 cycleFrames();
private:
    u32 m_breathSerial = 0;
    s32 m_cooldown = 179;
    bool m_vomiting = false;
    f32 m_frame = -1.0f;
    mkw::ObjectAnimationSound m_sound;
};
}
