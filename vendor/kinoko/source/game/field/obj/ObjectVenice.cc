#include "ObjectVenice.hh"
#include "game/field/ObjectDirector.hh"
#include "game/system/RaceManager.hh"
#include "game/system/ResourceManager.hh"
#include <egg/math/Math.hh>
#include <algorithm>

namespace Kinoko::Field {
ObjectVeniceWater::ObjectVeniceWater(const System::MapdataGeoObj &p):ObjectCollidable(p),
    m_baseY(pos().y),m_rate(static_cast<s16>(p.setting(0))/100.f),
    m_drift(static_cast<s16>(p.setting(6))/100.f),m_first(static_cast<s16>(p.setting(2))),
    m_second(static_cast<s16>(p.setting(4))),m_final(static_cast<s16>(p.setting(1))),
    m_firstFrames(m_rate!=0.f?s32(m_first/m_rate):0),
    m_firstDrift(static_cast<s16>(p.setting(3))),
    m_secondFrames(m_rate!=0.f?s32((m_second-(m_first+m_firstDrift*m_drift))/m_rate):0),
    m_secondDrift(static_cast<s16>(p.setting(5))),
    m_finalFrames(m_rate!=0.f?s32((m_final-(m_second+m_secondDrift*m_drift))/m_rate):0) {
    ObjectDirector::Instance()->setPsea(this);
}
f32 ObjectVeniceWater::baseHeight(u32 frame) const {
    // 8081b320..b4e4: five finite intervals, then the final level; no loop.
    s32 t=static_cast<s32>(frame);
    if(t<m_firstFrames)return m_baseY+t*m_rate;
    t-=m_firstFrames;
    if(t<m_firstDrift)return m_baseY+m_first+t*m_drift;
    t-=m_firstDrift;
    if(t<m_secondFrames)return m_baseY+m_first+m_firstDrift*m_drift+t*m_rate;
    t-=m_secondFrames;
    if(t<m_secondDrift)return m_baseY+m_second+t*m_drift;
    t-=m_secondDrift;
    if(t<m_finalFrames)return m_baseY+m_second+m_secondDrift*m_drift+t*m_rate;
    return m_baseY+m_final;
}
f32 ObjectVeniceWater::layerHeight(unsigned layer,u32 frame) const {
    const f32 base=baseHeight(frame);
    const f32 wave=80.f*EGG::Mathf::sin(F_TAU*static_cast<f32>(frame%400)/400.f);
    if(layer==1)return base+wave-260.f; // venice_dark
    if(layer==2)return base+wave+10.f;  // venice_sand
    if(layer==3){ // 8081b018..b0f4: second crest sheet, original28-frame phase.
        u32 phase=frame%400;if(phase>100)phase=(frame+400-28)%400;
        const f32 crest=80.f*EGG::Mathf::sin(F_TAU*static_cast<f32>(phase*2)/400.f);
        return base+std::max(wave+10.f,crest);
    }
    return base+wave;
}
void ObjectVeniceWater::calc(){
    const auto t=System::RaceManager::Instance()->timer();
    setPos(EGG::Vector3f(pos().x,layerHeight(0,t),pos().z));
}
ObjectVeniceGate::ObjectVeniceGate(const System::MapdataGeoObj &p):ObjectKCL(p),
    m_origin(pos()),m_angles(rot()),m_start(static_cast<s16>(p.setting(0))),
    m_rise(static_cast<s16>(p.setting(1))){}
f32 ObjectVeniceGate::height(u32 time)const{
    const s32 t=static_cast<s32>(time);
    if(t<=m_start)return m_origin.y;
    if(t>=m_start+120)return m_origin.y+m_rise;
    // 80804d78..e00: original120-frame half-cosine, half-tick sample offset.
    const f32 angle=F_PI*(static_cast<f32>((t-m_start)%120)-.5f)/120.f;
    return m_origin.y+.5f*m_rise*(1.f-EGG::Mathf::cos(angle));
}
const EGG::Matrix34f &ObjectVeniceGate::getUpdatedMatrix(u32 offset){
    const u32 t=System::RaceManager::Instance()->timer()-offset;
    m_matrix.makeRT(m_angles,EGG::Vector3f(m_origin.x,height(t),m_origin.z));return m_matrix;
}
void ObjectVeniceGate::init(){setTransform(getUpdatedMatrix(0));}
void ObjectVeniceGate::calc(){
    const s32 t=static_cast<s32>(System::RaceManager::Instance()->timer());
    if(t>m_start&&t<m_start+120)cueSound(0x307,SoundCall::Hold);
    if(t>=m_start+120&&!m_stopped){cueSound(0x308,SoundCall::Start);m_stopped=true;}
    setTransform(getUpdatedMatrix(0));
}
ObjectVeniceBridge::ObjectVeniceBridge(const System::MapdataGeoObj &p):ObjectKCL(p),
    m_origin(pos()),m_angles(rot()),m_start(static_cast<s16>(p.setting(0))),
    m_frameTime(static_cast<s16>(p.setting(1))){
    System::RaceManager::Instance()->setBattleRespawnGroup(2, false);
}
ObjectVeniceBridge::~ObjectVeniceBridge(){m_objColMgr=m_main;EGG::egg_delete(m_before);}
void ObjectVeniceBridge::createCollision(){
    ObjectKCL::createCollision();m_main=m_objColMgr;
    auto *file=System::ResourceManager::Instance()->getFile("venice_hasi_wall.kcl",nullptr,System::ArchiveId::Course);
    ASSERT(file);m_before=EGG::egg_new<ObjColMgr>(file);
}
const EGG::Matrix34f &ObjectVeniceBridge::getUpdatedMatrix(u32 offset){
    const auto* race=System::RaceManager::Instance();
    const s32 t=race->isStageReached(System::RaceManager::Stage::Race)?static_cast<s32>(race->timer()-offset):0;
    const f32 duration=30.f*m_frameTime;
    f32 x=m_angles.x;
    if(t>m_start)x=t<m_start+duration?x*(1.f-(t-m_start)/duration):0.f;
    m_matrix.makeRT(EGG::Vector3f(x,m_angles.y,m_angles.z),m_origin);
    m_objColMgr=t<m_start?m_before:m_main;
    return m_matrix;
}
f32 ObjectVeniceBridge::animationFrame()const{
    const auto* race=System::RaceManager::Instance();
    const s32 t=race->isStageReached(System::RaceManager::Stage::Race)?static_cast<s32>(race->timer()):0;
    return t<m_start?0.f:m_frameTime>0.f?std::min((t-m_start)/m_frameTime,29.f):29.f;
}
void ObjectVeniceBridge::init(){setTransform(getUpdatedMatrix(0));}
void ObjectVeniceBridge::calc(){
    const s32 t=static_cast<s32>(System::RaceManager::Instance()->timer());
    if(t==m_start-300)cueSound(0x304,SoundCall::Start);
    if(t>=m_start&&t<m_start+30.f*m_frameTime)cueSound(0x305,SoundCall::Hold);
    if(t>=m_start+30.f*m_frameTime&&!m_ended){
        cueSound(0x306,SoundCall::Start);m_ended=true;
        System::RaceManager::Instance()->setBattleRespawnGroup(2, true);
    }
    setTransform(getUpdatedMatrix(0));
}
}
