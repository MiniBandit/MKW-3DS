#include "mkw/race_items.hpp"
#include "mkw/item_box.hpp"
#include "mkw/item_road_follow.hpp"
#include "mkw/item_rules.hpp"
#ifndef MKW_ITEMS_HOST_TEST
#include <game/field/CollisionDirector.hh>
#include <game/field/ObjectCollisionKart.hh>
#include <game/field/ObjectCollisionSphere.hh>
#include <game/field/ObjectDirector.hh>
#include <game/field/ObjectDrivableDirector.hh>
#include <game/field/obj/ObjectTwistedWay.hh>
#include <game/field/obj/ObjectAurora.hh>
#include <game/field/obj/ObjectSandcone.hh>
#include <game/field/obj/ObjectQuicksand.hh>
#include <game/field/obj/ObjectCasinoRoulette.hh>
#include <game/field/obj/ObjectItemboxPress.hh>
#include <game/field/RailInterpolator.hh>
#include <game/field/RailManager.hh>
#include <game/item/ItemDirector.hh>
#include <game/kart/KartAction.hh>
#include <game/kart/KartCollide.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartState.hh>
#include <game/system/CourseMap.hh>
#include <game/system/RaceManager.hh>
#include <game/system/RaceConfig.hh>
#include <game/system/ResourceManager.hh>
#endif
#include <algorithm>
#include <cmath>

// The Kinoko kart as the item system's kart side, and the course-side item boxes.
namespace mkw {
#ifndef MKW_ITEMS_HOST_TEST
namespace {
namespace K=Kinoko;
Vec3 nativeVec(const K::EGG::Vector3f& v){return {v.x,v.y,v.z};}
K::EGG::Vector3f sourceVec(Vec3 v){return {v.x,v.y,v.z};}
K::Kart::KartObject* kart(unsigned player){return K::Kart::KartObjectManager::Instance()->object(player);}
ItemSurface nativeSurfaceAt(Vec3 previous,Vec3 position,float radius,unsigned timeOffset){
    K::Field::CollisionInfo info{};info.reset();K::Field::KCLTypeMask mask=0;
    // Includes item-only walls/floors and moving course collision.
    const auto flags=K::Field::KCLTypeMask(KCL_TYPE_64EBDFFF);
    if(!K::Field::CollisionDirector::Instance()->checkSphereFull(radius,sourceVec(position),sourceVec(previous),flags,&info,&mask,timeOffset))return {};
    float supportY=0;
    if(info.floorObject&&info.floorObject->id()==K::Field::ObjectId::Sandcone)
        supportY=static_cast<K::Field::ObjectSandcone*>(info.floorObject)->getUpdatedMatrix(timeOffset).base(3).y;
    const Vec3 carry=info.floorObject&&unsigned(info.floorObject->id())==0x206?nativeVec(info.roadVelocity):Vec3{};
    return {nativeVec(info.tangentOff),nativeVec(info.floorNrm),nativeVec(info.wallNrm),bool(mask&(KCL_TYPE_BIT(COL_TYPE_FALL_BOUNDARY)|KCL_TYPE_BIT(COL_TYPE_SOLID_OOB))),bool(mask&KCL_TYPE_BIT(COL_TYPE_CANNON_TRIGGER)),uint32_t(mask),true,info.floorObject,supportY,carry};
}
ItemSurface nativeSurface(Vec3 previous,Vec3 position,float radius){return nativeSurfaceAt(previous,position,radius,0);}
// The patched Kinoko KartAction/KartMove carry the retail gates; this only forwards.
class NativeKarts final:public ItemKarts{
public:
    void globalCourseItem(unsigned type)override{
        if(auto* objects=K::Field::ObjectDirector::Instance())objects->broadcastItemCollision(int16_t(type));
    }
    void courseContacts(unsigned type,unsigned owner,Vec3 position,Vec3 velocity,float radius,bool held,
            CourseItemVisitor visitor,void* context)override{
        auto* objects=K::Field::ObjectDirector::Instance();if(!objects||!visitor)return;
        const auto count=objects->checkItemObjectCollision(int16_t(type),uint8_t(owner),sourceVec(position),
                sourceVec(velocity),radius,held);
        for(unsigned i=0;i<count;++i)
            if(!visitor(context,uint8_t(objects->reaction(i)),nativeVec(objects->hitDepth(i))))break;
    }
    bool contactsInKartPass()const override{return true;}
    int startAction(unsigned player,int action,bool force,unsigned attacker,Vec3 hitDepth)override{
        auto* k=kart(player);auto* a=k->action();
        a->setHitDepth(sourceVec(hitDepth));a->setTranslation(sourceVec(hitDepth*20));
        if(!a->start(static_cast<K::Kart::Action>(action),force,uint8_t(attacker)))return KartAction::None;
        return int(a->currentAction());
    }
    bool activateStar(unsigned player)override{return kart(player)->move()->activateStar();}
    void activateMega(unsigned player)override{kart(player)->move()->activateMega();}
    void activateMushroom(unsigned player)override{kart(player)->move()->activateMushroom();}
    bool lightning(unsigned player,uint16_t frames,bool spinShrink,bool cloud)override{
        return kart(player)->move()->applyLightningEffect(frames,spinShrink,cloud);
    }
    void setThunderCloud(unsigned player,bool active)override{kart(player)->move()->setThunderCloud(active);}
    void setPowTarget(unsigned player,bool target)override{kart(player)->action()->setPowTarget(target);}
    void powHop(unsigned player,float lift)override{
        auto* k=kart(player);if(k->state()->status().onBit(K::Kart::eStatus::AirtimeOver20))return;
        k->dynamics()->setExtVel(k->dynamics()->extVel()+K::EGG::Vector3f(0.0f,lift,0.0f));
    }
    void openPowWindow()override{K::Kart::KartAction::OpenPowWindow();}
    ItemKartLoss takeLoss(unsigned player)override{
        const auto loss=K::Item::ItemDirector::Instance()->kartItem(player).takeLossRequests();
        return {loss.strong,loss.tethered,loss.trailOnly};
    }
    // Power-up timers read live, so a Star or Mega started this step shows at once.
    void overlay(unsigned player,ItemRacer& racer)const override{
        auto* k=kart(player);const auto& status=k->state()->status();using S=K::Kart::eStatus;
        racer.star=status.onBit(S::Star)?uint16_t(std::max<int>(1,k->move()->starTimer())):0;
        racer.mega=status.onBit(S::Mega)?uint16_t(std::max<int>(1,k->move()->megaTimer())):0;
        racer.shocked=status.onBit(S::Shocked);
    }
    bool touches(unsigned player,Vec3 center,float radius)const override{
        // ItemObj_checkPlayerCollision fine stage (0x8080b4b0 -> 0x8081c450): the item sphere
        // against the kart's object-collision hull from this pass.
        auto* hull=kart(player)->objectCollisionKart()->hull();
        if(!hull)return false;
        K::Field::ObjectCollisionSphere sphere(radius,sourceVec(center));
        sphere.transform(K::EGG::Matrix34f::ident,K::EGG::Vector3f(1.f,1.f,1.f));
        K::EGG::Vector3f distance;
        return hull->check(sphere,distance);
    }
};
// Our own interpolator on the rail Kinoko's RailManager built for a KMP
// route (initialised twice like retail objects, so a smooth rail moves).
std::shared_ptr<K::Field::RailInterpolator> boxRail(int path,float speed,unsigned start){
    const auto* map=K::System::CourseMap::Instance();
    if(path<0||!K::Field::RailManager::Instance()||unsigned(path)>=map->getPointInfoCount())return nullptr;
    std::shared_ptr<K::Field::RailInterpolator> r;
    if(map->getPointInfo(path)->setting(0)==0)r=std::make_shared<K::Field::RailLinearInterpolator>(speed,unsigned(path));
    else r=std::make_shared<K::Field::RailSmoothInterpolator>(speed,unsigned(path));
    r->init(0.f,start<r->pointCount()?start:0);
    return r;
}
ItemRacer snapshot(unsigned i){
    auto* k=kart(i);const auto& status=k->state()->status();const auto& pose=k->pose();
    const auto& player=K::System::RaceManager::Instance()->player(i);
    using S=K::Kart::eStatus;
    ItemRacer r{};
    r.position=nativeVec(k->pos());r.forward={pose[0,2],pose[1,2],pose[2,2]};
    r.movementDirection=nativeVec(k->move()->vel1Dir());r.smoothedUp=nativeVec(k->move()->smoothedUp());
    r.progress=player.raceCompletion();r.speed=k->move()->speed();
    // ItemObj_checkPlayerCollision reads KartCollide's bounding radius (fn_80698).
    r.radius=k->collide()->boundingRadius();
    r.grounded=status.onBit(S::TouchingGround);
    r.unavailable=status.onBit(S::BeforeRespawn,S::InRespawn,S::TriggerRespawn,S::InCannon);
    r.finished=player.finished();
    const auto& scenario=K::System::RaceConfig::Instance()->raceScenario();
    r.team=scenario.teams?RaceTeam(scenario.players[i].team):RaceTeam::None;
    r.controlLocked=status.onBit(S::InAction,S::Burnout);
    r.itemLocked=status.onBit(S::BeforeRespawn,S::InAction,S::TriggerRespawn,S::CannonStart,S::InCannon)||
        status.onBit(static_cast<S>(72),static_cast<S>(82),static_cast<S>(83),static_cast<S>(89));
    r.afterCannon=status.onBit(S::AfterCannon);
    r.inCannon=status.onBit(S::InCannon);
    r.wallCollision=status.onBit(S::WallCollision,S::Wall3Collision);
    r.up={pose[0,1],pose[1,1],pose[2,1]};r.scale=nativeVec(k->scale());
    const auto& stats=k->param()->stats();
    r.itemGeometry={stats.itemUnk170,stats.itemUnk174,stats.itemUnk178,stats.itemUnk17c};
    r.speedRatio=k->move()->speedRatioCapped();r.hop=k->move()->hopPosY();
    r.shellTargetUnavailable=status.onBit(S::TriggerRespawn,S::InRespawn,S::AfterRespawn,static_cast<S>(83));
    // Retail 7eaec/7ea0c use distinct masks. Retain unnamed source bits
    // where Kinoko has no producer yet; do not substitute unrelated flags.
    auto bit=[&](unsigned n){return status.onBit(static_cast<S>(n));};
    r.shellAcquireUnavailable=r.shellTargetUnavailable||bit(40)||bit(82)||bit(89);
    r.shellTargetProtected=r.shellAcquireUnavailable||bit(4)||bit(32)||bit(35)||bit(36)||bit(63)||bit(79)||bit(91);
    r.mushroomBoost=status.onBit(S::MushroomBoost);
    r.inkCleaningBoost=k->move()->boostActive(K::Kart::KartBoost::Type::MushroomAndBoostPanel);
    r.trickRotating=status.onBit(S::ZipperTrick,S::TrickRot);
    r.damageAction=int8_t(k->action()->currentAction());r.zipperStick=status.onBit(S::ZipperStick);
    // KartAction::start's respawn family, read for item eligibility; the kart applies the
    // gates itself when an item asks.
    r.damageProtected=status.onBit(S::BeforeRespawn,S::TriggerRespawn,S::InRespawn,S::AfterRespawn,S::InCannon,
        S::BattleDamageProtection,S::BattleVanish,S::BattleHidden);
    r.bike=k->param()->isBike();r.velocity=nativeVec(k->velocity());r.haveVelocity=true;
    r.star=status.onBit(S::Star)?uint16_t(std::max<int>(1,k->move()->starTimer())):0;
    r.mega=status.onBit(S::Mega)?uint16_t(std::max<int>(1,k->move()->megaTimer())):0;
    r.bullet=status.onBit(S::Bullet);r.shocked=status.onBit(S::Shocked);r.crushed=status.onBit(S::Crushed);
    r.inAction=status.onBit(S::InAction);
    return r;
}
}
ItemSurface queryCourseItemSurface(Vec3 previous,Vec3 position,float radius){return nativeSurface(previous,position,radius);}
ItemSurface queryCourseItemSurfaceAt(Vec3 previous,Vec3 position,float radius,unsigned timeOffset){return nativeSurfaceAt(previous,position,radius,timeOffset);}
void RaceItems::nativeKartPass(void* context,size_t player){
    auto* items=static_cast<RaceItems*>(context);
    if(!items->active_||player>=items->count_)return;
    items->racers_[player]=snapshot(unsigned(player));items->itemPointPending_&=uint16_t(~(1u<<player));
    items->kartContacts(unsigned(player));
}
void RaceItems::nativeKartBump(void* context,size_t attacker,size_t victim){
    auto* items=static_cast<RaceItems*>(context);
    if(!items->active_||attacker>=items->count_||victim>=items->count_)return;
    items->cloudBump(unsigned(attacker),unsigned(victim),snapshot(unsigned(victim)));
}
void RaceItems::unhookNative(){
    K::Kart::KartCollide::SetItemCollisionHook(nullptr,nullptr);
    K::Kart::KartCollide::SetKartBumpHook(nullptr,nullptr);
    K::Item::ItemDirector::ClearCoursePickupHook(this);
    K::Item::ItemDirector::ClearCourseDropHook(this);
}
void RaceItems::nativeCoursePickup(void* context,Vec3 position,Vec3 velocity,uint8_t owner){
    auto& items=*static_cast<RaceItems*>(context);
    if(!items.active_||owner>=items.count_||owner>=12)return;
    const auto type=ItemObjects[unsigned(ItemKind::Mushroom)].type;
    // GenerateItemDirect (USA80791120) declines when its original type pool
    // is full; a course pickup must never evict a live item to make room.
    if(items.objectCount(type)>=itemObjectCapacity(type)||
            std::none_of(items.actors_.begin(),items.actors_.end(),[](const ItemActor& a){return !a.active;}))return;
    auto& a=items.spawn(owner,ItemKind::Mushroom,false,&position);
    // Direct generation starts at .7 (808a0810). Its scatter initializer
    // 8079df68 adds35 along world up, then the caller adds the authored
    // lateral velocity. Damage-ejected inventory instead starts at .05.
    a.velocity=velocity+Vec3{0,35,0};a.scattered=true;a.up={0,1,0};
    a.yaw=std::atan2(velocity.x,velocity.z);
    a.scale=.7f;a.radius=33.f*a.scale;a.terrainRadius=27.f*a.scale;a.ownerImmunity=7;
}
namespace {
float roadBoxAngle(const K::Field::ObjectDrivable& road,float z){
    const auto* race=K::System::RaceManager::Instance();
    const unsigned frame=race->isStageReached(K::System::RaceManager::Stage::Race)?race->timer():
        static_cast<const K::Field::ObjectTwistedWay&>(road).introTimer();
    const float u=-z/7500.f,lo=u-1.f,hi=u+1.f;
    const float wave=K::EGG::Mathf::SinFIdx(((frame%120)*2.f*3.14159265358979323846f/120.f+4.f*u)*40.74366543f);
    return .2f*hi*hi*lo*lo*wave;
}
}
void RaceItems::stepMovers(){
    using Status=K::Field::RailInterpolator::Status;
    // s_itembox updates its bound road point even while its box is respawning.
    for(const auto& binding:roadBoxes_){
        auto& box=boxes_[binding.box];Vec3 local=binding.local;
        if(unsigned(binding.road->id())==0x1fe){
            const auto sc=K::EGG::Mathf::SinCosFIdx(roadBoxAngle(*binding.road,local.z)*40.74366543f);
            local=followTwistedItem(local,sc.first,sc.second);
        }else if(unsigned(binding.road->id())==0x20c){
            const auto& sand=*static_cast<const K::Field::ObjectQuicksand*>(binding.road);
            local.y=sand.surfaceOffset(std::sqrt(local.x*local.x+local.z*local.z),K::System::RaceManager::Instance()->timer());
        }else{
            const float time=float(K::System::RaceManager::Instance()->timer());
            const float mins=(time/60.f-30.f)/60.f;
            const float phase=2.f*3.14159265358979323846f*local.z/15000.f;
            local.y=phase*80.f*K::EGG::Mathf::SinFIdx((phase*std::min(1.f+mins*mins,4.f)+3.14159265358979323846f*time/50.f)*40.74366543f);
        }
        box.position=nativeVec(binding.road->pos())+local+Vec3{0,binding.lift,0};
    }
    for(auto& p:presses_){
        auto& box=boxes_[p.box];const auto* rail=p.object->railInterpolator();
        if(p.object->state()==0||!rail){p.taken=false;box.active=false;box.respawn=0;continue;}
        if(!box.active&&box.respawn){p.taken=true;box.respawn=0;}
        const bool revealed=unsigned(rail->curPointIdx())>=p.reveal;
        // Retail USA8075b6bc: a brick broken before the press cannot reveal a box.
        const bool intact=!p.object->brickBroken();
        if(revealed&&intact&&!box.active&&!p.taken){box.active=true;box.appearance=0;}
        if(!revealed||!intact)box.active=false;
        box.position=nativeVec(p.object->pos());
    }
    for(auto& m:movers_)if(m.kind==ItemBoxMover::Kind::Line){
        // A collected line box stays away until the line launches it again.
        auto& box=boxes_[m.box];if(!box.active&&box.respawn){box.respawn=0;m.riding=false;}
    }
    for(auto& line:lines_){
        if(--line.timer>0||line.movers.empty())continue;
        line.timer=line.period;
        auto& m=movers_[line.movers[line.next]];
        m.riding=true;m.rail->init(0.f,0);
        auto& box=boxes_[m.box];box.active=true;box.respawn=0;box.appearance=ItemBoxAppearanceFrames+1;
        line.next=uint16_t((line.next+1)%line.movers.size());
    }
    for(auto& m:movers_){
        auto& box=boxes_[m.box];auto& r=*m.rail;
        switch(m.kind){
        case ItemBoxMover::Kind::Route:
            // Points marked 1 send the box back to the route start.
            if(r.curPoint().setting[1]==1){r.init(0.f,0);if(box.active)box.appearance=0;}
            else r.calc();
            if(const unsigned speed=r.curPoint().setting[0])r.setCurrVel(float(speed));
            break;
        case ItemBoxMover::Kind::Sine:
            if(m.delay>=0){--m.delay;break;}
            {
                // Eases end to end: the distance per frame follows half the
                // route length times the sine of a steadily turning angle.
                m.angle+=m.step;const float s=m.half*std::sin(m.angle*0.0174533f);
                r.setCurrVel(std::abs(s-m.previous));m.previous=s;
                if(r.calc()==Status::ChangingDirection){m.angle=90;m.previous=m.half;}
            }
            break;
        case ItemBoxMover::Kind::Line:
            if(!m.riding)continue;
            if(r.calc()==Status::ChangingDirection){m.riding=false;box.active=false;box.respawn=0;}
            break;
        }
        box.position=nativeVec(r.curPos())+Vec3{0,m.lift,0};
    }
}
void RaceItems::resetFromCourse(unsigned count,uint32_t seed){
    std::vector<ItemBox> boxes;const auto* map=K::System::CourseMap::Instance();
    std::vector<ItemBoxMover> movers;std::vector<ItemBoxLine> lines;std::vector<RoadBox> roadBoxes;
    for(unsigned i=0;i<map->getGeoObjCount();++i){const auto* obj=map->getGeoObj(i);if(!(obj->presenceFlag()&1))continue;
        const unsigned id=obj->id();
        if(id==0x65){
            // KMP setting 7 is a signed extra Y offset. Retail regular boxes use a
            // fixed .9 scale and one of four shared X/Z rotation phases, ignoring
            // KMP Euler rotation/scale; arbitrary authored airborne rows stay high.
            boxes.push_back(courseItemBox(nativeVec(obj->pos()),int16_t(obj->setting(6)),i));
        }else if(id==0x76){
            // s_itembox (RMCP 80829280) drops from its placement in 50-unit
            // steps onto the floor below, then hovers like a regular box. It
            // stays where it landed; retail also rides moving roads with it.
            Vec3 at=nativeVec(obj->pos());
            for(unsigned step=0;step<1000;++step){
                K::Field::CollisionInfo info{};info.reset();K::Field::KCLTypeMask mask=0;
                if(K::Field::CollisionDirector::Instance()->checkSphereFull(ItemBoxRadius,sourceVec(at),K::EGG::Vector3f::inf,
                        K::Field::KCLTypeMask(0x6cebdfff),&info,&mask,0)){at+=nativeVec(info.tangentOff);at.y-=ItemBoxRadius;break;}
                at.y-=50;
            }
            boxes.push_back(courseItemBox(at,int16_t(obj->setting(6)),i));
            // Retail S_Itembox OnStart binds an eligible drivable surface;
            // Update then follows that surface, not the original KMP height.
            if(const auto* roads=K::Field::ObjectDrivableDirector::Instance())for(const auto* road:roads->objects()){
                const unsigned rid=unsigned(road->id());Vec3 local=at-nativeVec(road->pos());
                const bool twisted=rid==0x1fe&&std::abs(local.z)<=7500.f&&std::abs(local.x)<=2000.f;
                const bool aurora=rid==0x204&&local.z>=0&&local.z<=15000.f&&std::abs(local.x)<=2000.f;
                // Original S_Itembox ObjInfo binding follows the quicksand's
                // analytic material point, preserving X/Z and authored lift.
                const bool sand=rid==0x20c&&local.x*local.x+local.z*local.z<=9500.f*9500.f;
                if(!twisted&&!aurora&&!sand)continue;
                if(twisted){const auto sc=K::EGG::Mathf::SinCosFIdx(roadBoxAngle(*road,local.z)*40.74366543f);local=bindTwistedItem(local,sc.first,sc.second);}
                roadBoxes.push_back({road,local,uint16_t(boxes.size()-1),ItemBoxHeight+int16_t(obj->setting(6))});break;
            }
        }else if(id==0xc9||id==0xee||id==0xd4){
            const unsigned path=obj->pathId();
            const auto addMover=[&](ItemBoxMover::Kind kind,unsigned start)->bool{
                auto rail=boxRail(int(int16_t(path)),float(obj->setting(0)),start);if(!rail)return false;
                ItemBox box=courseItemBox(nativeVec(rail->curPos()),0,i);box.radius=235;
                ItemBoxMover m;m.kind=kind;m.rail=rail;m.box=uint16_t(boxes.size());
                boxes.push_back(box);movers.push_back(m);return true;
            };
            if(id==0xc9){
                // f_itembox (RMCP 80828f98): from start point setting 4, at the
                // route's point speeds where set.
                if(addMover(ItemBoxMover::Kind::Route,obj->setting(4))){
                    auto& r=*movers.back().rail;if(const unsigned speed=r.curPoint().setting[0])r.setCurrVel(float(speed));
                }
            }else if(id==0xee){
                // sin_itembox (RMCP 8077cf44): waits setting 4 frames, then
                // swings end to end once every setting 0 frames.
                if(addMover(ItemBoxMover::Kind::Sine,0)){
                    auto& m=movers.back();m.delay=int16_t(obj->setting(4));
                    m.half=m.rail->railLength()*.5f;m.angle=90;m.previous=m.half;
                    m.step=360.f/float(std::max<int16_t>(1,int16_t(obj->setting(0))));
                }
            }else{
                // w_itembox (RMCP 8076c85c): setting 6 boxes (default 5), one
                // launched from the route start every setting 5 frames (first
                // after setting 4), each gone at the route's end.
                ItemBoxLine line;line.period=int16_t(obj->setting(5));
                line.timer=obj->setting(4)?int16_t(obj->setting(4)):line.period;
                const unsigned n=obj->setting(6)?obj->setting(6):5;
                for(unsigned k=0;k<n;++k)if(addMover(ItemBoxMover::Kind::Line,0))line.movers.push_back(uint16_t(movers.size()-1));
                if(!line.movers.empty())lines.push_back(line);
            }
        }
    }
    // Toad's Factory belts: Kinoko moves each slot (time-trial rules, where
    // bricks never open); VS races reveal its box at the point marked 2.
    std::vector<ItemBoxPress> presses;
    if(const auto* director=K::Field::ObjectDirector::Instance())for(const auto* obj:director->objects()){
        if(obj->id()!=K::Field::ObjectId::Itembox||!obj->mapObj()||obj->mapObj()->id()!=0xd5)continue;
        ItemBoxPress p;p.object=static_cast<const K::Field::ObjectItemboxPress*>(obj);p.box=uint16_t(boxes.size());
        const auto points=K::Field::RailManager::Instance()->rail(unsigned(obj->mapObj()->pathId()))->points();
        for(size_t k=0;k<points.size();++k)if(points[k].setting[1]==2){p.reveal=uint16_t(k);break;}
        boxes.push_back(courseItemBox(nativeVec(obj->pos())-Vec3{0,ItemBoxHeight,0},0,unsigned(boxes.size())));presses.push_back(p);
    }
    static NativeKarts native;
    setKarts(&native);
    reset(boxes,count,seed);
    movers_=std::move(movers);lines_=std::move(lines);presses_=std::move(presses);roadBoxes_=std::move(roadBoxes);
    for(const auto& p:presses_)boxes_[p.box].active=false;
    for(const auto& m:movers_)if(m.kind==ItemBoxMover::Kind::Line)boxes_[m.box].active=false;
    size_t kmpSize=0;const void* kmp=K::System::ResourceManager::Instance()->getFile("course.kmp",&kmpSize,K::System::ArchiveId::Course);
    loadShellRoute(kmp,kmpSize,unsigned(K::System::RaceConfig::Instance()->raceScenario().course));
    size_t oddsSize=0;const auto* odds=K::System::ResourceManager::Instance()->getFile("ItemSlot.bin",&oddsSize,K::System::ArchiveId::Core);
    const unsigned itemMode=K::System::RaceConfig::Instance()->raceScenario().itemMode;
    const auto& scenario=K::System::RaceConfig::Instance()->raceScenario();
    loadOdds(odds,oddsSize,itemMode,scenario.gameMode==K::System::RaceConfig::Scenario::GameMode::Battle,scenario.battleType==1);
    if(itemMode==3){boxes_.clear();movers_.clear();lines_.clear();presses_.clear();}
    for(unsigned i=0;i<count_;++i){K::Item::ItemDirector::Instance()->kartItem(i).clear();(void)K::Item::ItemDirector::Instance()->kartItem(i).takeLossRequests();}
    K::Kart::KartCollide::SetItemCollisionHook(&RaceItems::nativeKartPass,this);
    K::Kart::KartCollide::SetKartBumpHook(&RaceItems::nativeKartBump,this);
    K::Item::ItemDirector::SetCoursePickupHook([](void* context,const K::EGG::Vector3f& position,
            const K::EGG::Vector3f& velocity,K::u8 owner){
        RaceItems::nativeCoursePickup(context,nativeVec(position),nativeVec(velocity),uint8_t(owner));
    },this);
    K::Item::ItemDirector::SetCourseDropHook([](void* context,K::u16 slot,
            const K::EGG::Vector3f& position,const K::EGG::Vector3f& velocity,K::u8 owner){
        auto& items=*static_cast<RaceItems*>(context);
        // DecideItem advances NextLimited even for a zero-weight row.
        const uint32_t word=K::System::RaceManager::Instance()->random().getU32();
        const bool tt=static_cast<unsigned>(K::System::RaceConfig::Instance()->raceScenario().gameMode)==2;
        items.createCourseDrop(slot,nativeVec(position),nativeVec(velocity),uint8_t(owner),word,tt);
    },this);
}
void RaceItems::snapshotNative(unsigned count,std::array<ItemRacer,MaxRacers>& out) const{
    for(unsigned i=0;i<std::min(count,MaxRacers);++i)out[i]=snapshot(i);
}
void RaceItems::beforeStepNative(const ItemInput* inputs,unsigned count,bool active,uint16_t bulletNoEndPlayers){
    std::array<ItemRacer,MaxRacers> states;snapshotNative(count,states);
    beforeStep(states.data(),inputs,count,active,bulletNoEndPlayers);
    // Kinoko's own time-trial item controller never holds anything in a race.
    for(unsigned i=0;i<count_;++i)K::Item::ItemDirector::Instance()->kartItem(i).inventory().clear();
}
void RaceItems::afterStepNative(unsigned count){
    // Retail Item globals read RaceManager +0x20, not the active-item clock.
    reclaimPhase_=K::System::RaceManager::Instance()->timer()&7;nativeReclaimPhase_=true;
    std::array<ItemRacer,MaxRacers> states;snapshotNative(count,states);
    for(unsigned i=0;i<std::min(count,MaxRacers);++i)
        states[i].inkCleaningBoost=kart(i)->move()->takeFloorInkClear()||states[i].inkCleaningBoost;
    afterStep(states.data(),count,nativeSurface);
}
#else
void RaceItems::stepMovers(){}
void RaceItems::unhookNative(){}
void RaceItems::nativeKartPass(void*,size_t){}
void RaceItems::nativeKartBump(void*,size_t,size_t){}
void RaceItems::nativeCoursePickup(void*,Vec3,Vec3,uint8_t){}
#endif
}
