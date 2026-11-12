#include "ObjectBlockStatue.hh"
#include "game/field/ObjectDirector.hh"
#include "game/field/CollisionDirector.hh"
#include "game/field/obj/ObjectVolcanoPiece.hh"
#include <limits>
namespace Kinoko::Field {
void ObjectBlockStatue::load() { ObjectDirector::Instance()->addObject(this); }
void ObjectBlockStatue::init() {
    m_anchor=mapObj()->pos();setPos(m_anchor);
    m_support=nullptr;m_supportY=0;m_bound=false;
}
void ObjectBlockStatue::bindFloor() {
    // USA80818090..181b4: 100-radius sphere, descend50 until first KCL
    // contact, snapY by tangentOffY-radius, bind the returned moving floor.
    // Bind on the first simulation tick, after all drivable KCL init calls.
    auto point=m_anchor;
    const f32 infinity=std::numeric_limits<f32>::infinity();
    while(point.y>=0.f){
        CollisionInfo info;info.reset();KCLTypeMask mask=KCL_NONE;
        const bool hit=CollisionDirector::Instance()->checkSphereFull(100.f,point,
                {infinity,infinity,infinity},0x6cebdfff,&info,&mask,0);
        if(hit){
            point.y+=info.tangentOff.y-100.f;
            m_support=info.floorObject&&unsigned(info.floorObject->id())==0x20d?
                    static_cast<ObjectVolcanoPiece*>(info.floorObject):nullptr;
            if(m_support)m_supportY=m_support->getUpdatedMatrix(0).translation().y;
            break;
        }
        point.y-=50.f;
    }
    m_anchor=point;m_bound=true;
}
void ObjectBlockStatue::calc() {
    if(!m_bound)bindFloor();
    auto point=m_anchor;
    // USA808181d4..182e4: preserve authored X/Z, follow the bound material
    // point. Bblock's native transform is translation-only, so this Y delta
    // is the exact specialization of its ObjInfo matrix attachment.
    if(m_support)point.y+=m_support->getUpdatedMatrix(0).translation().y-m_supportY;
    setPos(point);
}
}
