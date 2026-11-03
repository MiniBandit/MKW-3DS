#pragma once
#include "game/field/obj/ObjectCollidable.hh"
namespace Kinoko::Field {
class ObjectVolcanoPiece;
// Only Block Plaza's four statues need the original moving-floor attachment.
class ObjectBlockStatue final : public ObjectCollidable {
public:
    explicit ObjectBlockStatue(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}
    void load() override;
    void init() override;
    void calc() override;
    void createCollision() override {}
    void calcCollisionTransform() override {}
    u32 loadFlags() const override { return 1; }
    bool shown() const { return pos().y >= -25.5f; }
    static bool isStatue(unsigned id) { return id==0x166||id==0x186||id==0x187||id==0x188; }
private:
    void bindFloor();
    EGG::Vector3f m_anchor{};
    ObjectVolcanoPiece *m_support = nullptr;
    f32 m_supportY = 0;
    bool m_bound = false;
};
}
