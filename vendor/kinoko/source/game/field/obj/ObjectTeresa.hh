#pragma once
#include "game/field/obj/ObjectCollidable.hh"
#include <array>
namespace Kinoko::Field {
class ObjectTeresa final : public ObjectCollidable {
public:
    explicit ObjectTeresa(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}
    void load() override;
    void init() override;
    void calc() override;
    u32 loadFlags() const override { return 1; }
    void createCollision() override {}
    void calcCollisionTransform() override {}
    struct Ghost {
        EGG::Vector3f offset{}, amplitude{}, displacement{};
        ObjectBase *soundSource = nullptr;
        unsigned variant = 0;
        s32 life = 0;
        f32 phase = 0, alphaPhase = 0, alphaSpeed = 1;
        u8 state = 3, alpha = 0;
        bool mirrorU = false;
    };
    const std::array<Ghost, 6> &ghosts() const { return m_ghosts; }
private:
    void appear(Ghost &ghost);
    std::array<Ghost, 6> m_ghosts{};
    // ObjectDirector owns these children and destroys them with the race.
    std::array<ObjectBase *, 6> m_soundSources{};
    unsigned m_next = 0;
    s32 m_delay = 30;
};
}
