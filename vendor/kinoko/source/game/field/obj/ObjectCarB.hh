#pragma once
#include "game/field/obj/ObjectCollidable.hh"
namespace Kinoko::Field {
class ObjectCarB final : public ObjectCollidable {
public:
    explicit ObjectCarB(const System::MapdataGeoObj &params) : ObjectCollidable(params) {}
    void init() override;
    u32 loadFlags() const override { return 1; }
    unsigned variant() const { return m_variant; }
private:
    unsigned m_variant = 0;
};
}
