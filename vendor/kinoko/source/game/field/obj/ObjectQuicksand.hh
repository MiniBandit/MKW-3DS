#pragma once
#include "game/field/obj/ObjectDrivable.hh"
#include "game/field/obj/ObjectDossunNormal.hh"
namespace Kinoko::Field {
class ObjectQuicksand final : public ObjectDrivable {
public:
    explicit ObjectQuicksand(const System::MapdataGeoObj& params);
    void init() override {}
    void calc() override;
    u32 loadFlags() const override {return 1;}
    void createCollision() override {}
    void calcCollisionTransform() override {}
    f32 getCollisionRadius() const override {return 9500.f;}
    const ObjectDossunNormal* thwomp() const {return m_thwomp;}
    f32 surfaceOffset(f32 radius,u32 frame) const;
    EGG::Vector3f flowVelocity(const EGG::Vector3f& local,u32 frame) const;
    static unsigned state(u32 frame);
    static unsigned stateFrame(u32 frame);
    f32 waveAmplitude(u32 frame) const;
    f32 animationFrame() const {return m_scrollFrame;}
    template<class T> bool collide(f32 radius,const EGG::Vector3f& p,KCLTypeMask mask,T* info,KCLTypeMask* out,u32 offset,bool push);
    bool checkPointPartial(const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfoPartial* info,KCLTypeMask* out) override {return collide(0.f,p,mask,info,out,0,false);}
    bool checkPointPartialPush(const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfoPartial* info,KCLTypeMask* out) override {return collide(0.f,p,mask,info,out,0,true);}
    bool checkPointFull(const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfo* info,KCLTypeMask* out) override {return collide(0.f,p,mask,info,out,0,false);}
    bool checkPointFullPush(const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfo* info,KCLTypeMask* out) override {return collide(0.f,p,mask,info,out,0,true);}
    bool checkSpherePartial(f32 radius,const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfoPartial* info,KCLTypeMask* out,u32 offset) override {return collide(radius,p,mask,info,out,offset,false);}
    bool checkSpherePartialPush(f32 radius,const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfoPartial* info,KCLTypeMask* out,u32 offset) override {return collide(radius,p,mask,info,out,offset,true);}
    bool checkSphereFull(f32 radius,const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfo* info,KCLTypeMask* out,u32 offset) override {return collide(radius,p,mask,info,out,offset,false);}
    bool checkSphereFullPush(f32 radius,const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfo* info,KCLTypeMask* out,u32 offset) override {return collide(radius,p,mask,info,out,offset,true);}
    bool checkPointCachedPartial(const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfoPartial* info,KCLTypeMask* out) override {return collide(0.f,p,mask,info,out,0,false);}
    bool checkPointCachedPartialPush(const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfoPartial* info,KCLTypeMask* out) override {return collide(0.f,p,mask,info,out,0,true);}
    bool checkPointCachedFull(const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfo* info,KCLTypeMask* out) override {return collide(0.f,p,mask,info,out,0,false);}
    bool checkPointCachedFullPush(const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfo* info,KCLTypeMask* out) override {return collide(0.f,p,mask,info,out,0,true);}
    bool checkSphereCachedPartial(f32 radius,const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfoPartial* info,KCLTypeMask* out,u32 offset) override {return collide(radius,p,mask,info,out,offset,false);}
    bool checkSphereCachedPartialPush(f32 radius,const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfoPartial* info,KCLTypeMask* out,u32 offset) override {return collide(radius,p,mask,info,out,offset,true);}
    bool checkSphereCachedFull(f32 radius,const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfo* info,KCLTypeMask* out,u32 offset) override {return collide(radius,p,mask,info,out,offset,false);}
    bool checkSphereCachedFullPush(f32 radius,const EGG::Vector3f& p,const EGG::Vector3f&,KCLTypeMask mask,CollisionInfo* info,KCLTypeMask* out,u32 offset) override {return collide(radius,p,mask,info,out,offset,true);}
private:
    f32 m_scrollFrame=0.f;
    EGG::Vector3f m_origin;
    ObjectDossunNormal* m_thwomp=nullptr;
};
class ObjectQuicksandThwomp final : public ObjectDossunNormal {
public:
    explicit ObjectQuicksandThwomp(const System::MapdataGeoObj& params);
    void init() override;
    void calc() override;
    Kart::Reaction onCollision(Kart::KartObject*,Kart::Reaction,Kart::Reaction,EGG::Vector3f&) override;
};
}
