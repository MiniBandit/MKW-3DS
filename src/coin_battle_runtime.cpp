#include <mkw/coin_battle_runtime.hpp>
#include <mkw/renderer.hpp>
#include <mkw/coin_ground.hpp>
#include <mkw/coin_shadow.hpp>
#include <game/field/obj/ObjectSandcone.hh>
#include <game/field/obj/ObjectCasinoRoulette.hh>
#include <game/field/ObjectCollisionKart.hh>
#include <game/field/ObjectCollisionSphere.hh>
#include <game/field/ObjectDirector.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartMove.hh>
#include <game/system/RaceManager.hh>
#include <game/system/ResourceManager.hh>
#include <egg/math/Math.hh>

namespace mkw {
namespace K=Kinoko;
namespace {
Vec3 vec(const K::EGG::Vector3f& v){return {v.x,v.y,v.z};}
K::EGG::Vector3f native(Vec3 v){return {v.x,v.y,v.z};}
}
struct CoinBattleRuntime::Impl {
    CoinBattle coins;
    CoinPresentation presentation;
    std::unique_ptr<Model> model,shadow;
    unsigned racers=0,course=0;
    bool ready=false;
    float pickupRadius=0;
    struct Clip {Vec3 position{};bool known=false,visible=true;};
    std::vector<Clip> clips;
    std::vector<uint8_t> visible;
    std::vector<Renderer::ShadowVolume> shadowVolumes;
    Vec3 cameraEye{};
    bool racing=false;
    void syncFinished(){
        const auto* race=K::System::RaceManager::Instance();
        for(unsigned i=0;i<racers;++i)if(race->player(i).finished())coins.finish(i);
    }
    static unsigned random(void*,unsigned range){return K::System::RaceManager::Instance()->random().getU32(range);}
    static CoinBody spawn(void* context,const CoinBattle::Placement& p){
        const auto& d=*static_cast<Impl*>(context);
        CoinBody body;
        // 675d8 normal variants add150; 677a8 sky variants add10000+150
        // to the visible origin. The native model offset is kept separate.
        body.previous=body.position=p.position+Vec3{0,p.kind<2?150.f:10150.f-CoinBody::visualOffset(d.course),0};
        if(p.kind<2){
            CoinGround::place(body,queryCourseItemSurface);
        }
        return body;
    }
    static bool followGround(void* context,CoinBody& body){
        const auto& d=*static_cast<Impl*>(context);
        if(!body.floorObject)return false;
        if(d.course==34){
            const auto* floor=static_cast<const K::Field::ObjectDrivable*>(body.floorObject);
            if(unsigned(floor->id())!=0x206)return false;
            body.position+=vec(K::Field::ObjectCasinoRoulette::surfaceMotion(native(body.position)));
            body.previous=body.position;return true;
        }
        if(d.course!=36)return false;
        auto* object=static_cast<K::Field::ObjectDrivable*>(const_cast<void*>(body.floorObject));
        if(object->id()!=K::Field::ObjectId::Sandcone)return false;
        const float height=static_cast<K::Field::ObjectSandcone*>(object)->getUpdatedMatrix(0).base(3).y;
        return CoinGround::follow(body,d.course,height);
    }
    static bool canRetire(void* context,unsigned unit,const CoinBattle::Coin& c){
        const auto& d=*static_cast<Impl*>(context);
        const auto position=c.body.visualPosition(d.course);
        const auto& clip=d.clips[unit];
        // 80875104..1c0: invisible in every local view and at least8000
        // from every local camera. This port has one local view; its actual
        // renderer culls the union of the two stereo eyes. Unknown/stale
        // bounds retain the coin until it has been classified at this point.
        return clip.known&&!clip.visible&&dot(clip.position-position,clip.position-position)==0.f&&
            length(d.cameraEye-position)>=8000.f;
    }
    static CoinDropPacket capture(unsigned victim){
        const auto* kart=K::Kart::KartObjectManager::Instance()->object(victim);
        const auto direction=CoinDropPacket::direction(vec(kart->velocity()),vec(kart->componentZAxis()));
        return CoinDropPacket::capture(vec(kart->pos()),K::EGG::Mathf::atan2(direction.x,direction.z));
    }
};
CoinBattleRuntime::CoinBattleRuntime():impl_(std::make_unique<Impl>()){}
CoinBattleRuntime::~CoinBattleRuntime()=default;
void CoinBattleRuntime::clear(){*impl_=Impl{};}
bool CoinBattleRuntime::reset(const BattleRules& rules,unsigned racers,const std::array<RaceTeam,12>& teams,unsigned course){
    auto& d=*impl_;d.ready=false;d.racers=racers;d.course=course;
    size_t size=0;const auto* kmp=K::System::ResourceManager::Instance()->getFile("course.kmp",&size,K::System::ArchiveId::Course);
    if(!d.coins.load(kmp,size,rules,racers,teams,Impl::random))return false;
    d.coins.assignWaves();
    if(!d.coins.initializePickupPriority(Impl::random))return false;
    const auto& flow=K::Field::ObjectDirector::Instance()->flowTable();
    const auto* parameters=flow.set(flow.slot(static_cast<K::Field::ObjectId>(115)));
    if(!parameters||K::parse<K::s16>(parameters->mode)!=int(K::Field::CollisionMode::Sphere))return false;
    // CoinUnit's fine collision uses ObjFlow's authored sphere, not PSV200.
    const auto* radius=reinterpret_cast<const uint8_t*>(&parameters->params.sphere.radius);
    d.pickupRadius=float(unsigned(radius[0])<<8|radius[1]);
    if(d.pickupRadius<=0)return false;
    auto load=[](std::unique_ptr<Model>& model,const char* file){
        if(model)return true;
        model=std::make_unique<Model>();
        if(!model->load(file)){model.reset();return false;}
        model->freezeLighting();model->enableStaticCulling();return true;
    };
    if(!load(d.model,"models/battle_coin.gmdl")||!load(d.shadow,"models/battle_coin_shadow.gmdl"))return false;
    d.clips.assign(d.coins.coins().size(),{});d.presentation.reset(d.coins.coins().size());d.visible.assign(d.clips.size(),1);d.shadowVolumes.resize(d.clips.size());
    d.racing=false;
    d.coins.activate(0,Impl::spawn,&d);
    d.ready=true;
    return true;
}
void CoinBattleRuntime::step(bool racing,unsigned tick,Vec3 cameraEye,RaceEffects& effects){
    auto& d=*impl_;if(!d.ready)return;
    d.racing=racing;d.cameraEye=cameraEye;
    d.syncFinished();
    const auto* director=K::Field::ObjectDirector::Instance();
    const float waterHeight=director->psea()?-director->distAboveRisingWater(0):CoinBody::NoWater;
    if(racing){
    d.coins.stepWaves(Impl::spawn,&d,150);
    auto* objects=K::Kart::KartObjectManager::Instance();
    for(unsigned c=0;c<d.coins.coins().size();++c){
        const auto& coin=d.coins.coins()[c];
        if(coin.state!=CoinBattle::State::Available)continue;
        K::Field::ObjectCollisionSphere sphere(d.pickupRadius,native(coin.body.visualPosition(d.course)));
        sphere.transform(K::EGG::Matrix34f::ident,K::EGG::Vector3f(1,1,1));
        for(unsigned i=0;i<d.racers;++i){
            const auto* kart=objects->object(i);using S=K::Kart::eStatus;
            if(kart->status().onBit(S::BeforeRespawn,S::TriggerRespawn,S::InRespawn,S::AfterRespawn,
                    S::InCannon,S::BattleHidden))continue;
            auto* hull=kart->objectCollisionKart()->hull();K::EGG::Vector3f separation;
            if(hull&&hull->check(sphere,separation))d.coins.requestCollect(c,i);
        }
    }
    d.coins.stepRecycling(Impl::random,nullptr,Impl::spawn,&d,150,waterHeight,Impl::canRetire,&d);
    }
    // Accepted commands continue draining after the race timer stops;
    // new contacts/events are gated above and at their native callbacks.
    d.coins.stepRequests();
    // CoinManager publishes requests before its all-unit calc loop
    //80875af4..75c14; callbacks see the previous unit positions.
    d.coins.followResting(Impl::followGround,&d);
    d.coins.stepDropped(queryCourseItemSurface,d.course,waterHeight,queryCourseItemSurfaceAt);
    std::array<Vec3,12> owners{};
    for(unsigned i=0;i<d.racers;++i)owners[i]=vec(K::Kart::KartObjectManager::Instance()->object(i)->pos());
    for(unsigned i=0;i<d.visible.size();++i)d.visible[i]=!d.clips[i].known||d.clips[i].visible;
    d.presentation.step(d.coins.coins(),d.course,owners,d.visible,effects);
    for(unsigned i=0;i<d.coins.coins().size();++i)d.coins.setHeldReady(i,d.presentation.heldReady(i));
    if(racing)d.coins.updateReadyRanks();
    (void)tick;
}
bool CoinBattleRuntime::hit(unsigned victim,bool fall){
    auto& d=*impl_;if(!d.ready||victim>=d.racers)return false;d.syncFinished();
    return d.coins.requestHit(victim,fall,Impl::capture(victim));
}
bool CoinBattleRuntime::steal(unsigned thief,unsigned victim){
    auto& d=*impl_;if(!d.ready)return false;d.syncFinished();
    return d.coins.requestSteal(thief,victim);
}
//80534d0c..d30 copies settled state7 count+1f12 into RaceManager+22.
unsigned CoinBattleRuntime::score(unsigned racer) const{return impl_->coins.readyHeld(racer);}
unsigned CoinBattleRuntime::rank(unsigned racer) const{
    return impl_->coins.rank(racer);
}
unsigned CoinBattleRuntime::positions(Vec3* destination,unsigned capacity) const{
    unsigned n=0;if(!destination)return n;
    for(const auto& coin:impl_->coins.coins())if(coin.state==CoinBattle::State::Available){
        if(n>=capacity)break;destination[n++]=coin.body.visualPosition(impl_->course);
    }
    return n;
}
unsigned CoinBattleRuntime::targetCount()const{return impl_->ready?unsigned(impl_->coins.coins().size()):0;}
bool CoinBattleRuntime::targetPosition(unsigned unit,Vec3& position)const{
    if(!impl_->ready||unit>=impl_->coins.coins().size())return false;
    const auto& c=impl_->coins.coins()[unit];if(c.state!=CoinBattle::State::Available)return false;
    position=c.body.visualPosition(impl_->course);return true;
}
void CoinBattleRuntime::failedRegistration(unsigned unit)const{
    if(impl_->ready&&impl_->racing)impl_->coins.failedRegistration(unit);
}
uint16_t CoinBattleRuntime::takeCues(){return impl_->coins.takeCues();}
uint16_t CoinBattleRuntime::takeArrivalCues(){return impl_->presentation.takeArrivalCues();}
void CoinBattleRuntime::drawShadows(Renderer& renderer,unsigned tick){
    auto& d=*impl_;if(!d.ready||!d.shadow)return;
    unsigned count=0;
    for(unsigned unit=0;unit<d.coins.coins().size();++unit){const auto& c=d.coins.coins()[unit];
        const auto& pose=d.presentation.unit(unit);
        if(!pose.shadow||(c.state==CoinBattle::State::Retiring&&(tick&2)))continue;
        C3D_Mtx ground;Mtx_Identity(&ground);
        Mtx_Translate(&ground,pose.position.x,pose.position.y,pose.position.z,false);
        Mtx_RotateY(&ground,pose.yaw,true);
        const auto scale=CoinShadow::scale(pose.scale);Mtx_Scale(&ground,scale.x,scale.y,scale.z);
        if(renderer.viewFrustum().visible(*d.shadow,ground))d.shadowVolumes[count++]={d.shadow.get(),ground};
    }
    if(count)renderer.drawShadowVolumes(d.shadowVolumes.data(),count,CoinShadow::Darkness);
}
void CoinBattleRuntime::draw(Renderer& renderer,unsigned tick){
    auto& d=*impl_;if(!d.ready||!d.model)return;
    for(unsigned unit=0;unit<d.coins.coins().size();++unit){const auto& c=d.coins.coins()[unit];
        const auto& pose=d.presentation.unit(unit);if(!pose.model)continue;
        C3D_Mtx matrix;Mtx_Identity(&matrix);
        const auto p=pose.position;Mtx_Translate(&matrix,p.x,p.y,p.z,false);
        Mtx_RotateY(&matrix,pose.yaw,true);Mtx_RotateX(&matrix,pose.pitch,true);
        Mtx_Scale(&matrix,pose.scale,pose.scale,pose.scale);
        const bool visible=renderer.viewFrustum().visible(*d.model,matrix);
        d.clips[unit]={p,true,visible};
        if(visible&&(c.state!=CoinBattle::State::Retiring||!(tick&2)))renderer.draw(*d.model,matrix);
    }
}
} // namespace mkw
