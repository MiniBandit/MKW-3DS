#pragma once
#include "game/field/obj/ObjectCollidable.hh"
#include <mkw/object_animation_sound.hpp>

namespace Kinoko::Field {
class ObjectCannon final : public ObjectCollidable {
public:
    explicit ObjectCannon(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}
    static bool matches(const char *name);
    static void activateFirst();
    void load() override;
    void init() override;
    void calc() override;
    u32 loadFlags() const override { return 1; }
    void createCollision() override {}
    void calcCollisionTransform() override {}
    f32 animationFrame() const { return m_frame; }
    bool firing() const { return m_playing; }
    bool starRing() const { return m_starRing; }
    u32 launchSerial() const { return m_launchSerial; }
private:
    void activate();
    f32 m_frame = 0.0f;
    bool m_playing = false, m_pending = false, m_starRing = false;
    u32 m_launchSerial = 0;
    mkw::ObjectAnimationSound m_sound;
};
}
