#pragma once
#include "game/field/obj/ObjectKCL.hh"
namespace Kinoko::Field {
// RMCU808770c0..80877cec: independently rotating arena deck and transport.
class ObjectCasinoRoulette final : public ObjectKCL {
public:
    explicit ObjectCasinoRoulette(const System::MapdataGeoObj& p):ObjectKCL(p){}
    ~ObjectCasinoRoulette() override;
    void init() override;
    static void carryCourseContact(CollisionInfo*,KCLTypeMask,const EGG::Vector3f&);
    void calc() override;
    u32 loadFlags() const override {return 1;}
    const EGG::Matrix34f& getUpdatedMatrix(u32 offset) override;
    static EGG::Vector3f surfaceMotion(const EGG::Vector3f& point);
    bool checkSphereFull(f32,const EGG::Vector3f&,const EGG::Vector3f&,KCLTypeMask,CollisionInfo*,KCLTypeMask*,u32) override;
    bool checkSphereCachedFull(f32,const EGG::Vector3f&,const EGG::Vector3f&,KCLTypeMask,CollisionInfo*,KCLTypeMask*,u32) override;
    bool checkCollision(f32,const EGG::Vector3f&,const EGG::Vector3f&,KCLTypeMask,CollisionInfo*,KCLTypeMask*,u32) override;
    bool checkCollisionCached(f32,const EGG::Vector3f&,const EGG::Vector3f&,KCLTypeMask,CollisionInfo*,KCLTypeMask*,u32) override;
private:
    void transport(CollisionInfo*,f32 previousMovingFloor,const EGG::Vector3f&);
    EGG::Matrix34f m_rotation;
    static ObjectCasinoRoulette* s_instance;
};
}
