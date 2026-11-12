#include <mkw/balloon_battle_runtime.hpp>
#include <mkw/balloon_body.hpp>
#include <mkw/renderer.hpp>
#include <mkw/race_effects.hpp>
#include <mkw/race_audio.hpp>
#include <game/system/RaceConfig.hh>
#include <vector>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartParam.hh>
#include <game/kart/KartDynamics.hh>
#include <game/kart/KartBody.hh>
#include <game/system/RaceManager.hh>

namespace mkw {
namespace K=Kinoko;
namespace {Vec3 vec(const K::EGG::Vector3f& p){return {p.x,p.y,p.z};}}
struct BalloonBattleRuntime::Impl {
    BalloonPresentation presentation;
    std::array<std::unique_ptr<Model>,2> models;
    std::array<BalloonBody,120> bodies;
    unsigned racers=0;
    std::vector<RaceAudioBalloonSound> sounds;
};
BalloonBattleRuntime::BalloonBattleRuntime()=default;
BalloonBattleRuntime::~BalloonBattleRuntime()=default;
void BalloonBattleRuntime::clear(){impl_.reset();}
bool BalloonBattleRuntime::reset(unsigned racers,const std::array<RaceTeam,12>& teams){
    if(!racers||racers>12)return false;
    if(!impl_)impl_=std::make_unique<Impl>();auto& d=*impl_;d.racers=racers;
    bool needed[2]={};for(unsigned i=0;i<racers&&i<12;++i)needed[teams[i]==RaceTeam::Blue]=true;
    for(unsigned color=0;color<2;++color)if(needed[color]&&!d.models[color]){
        d.models[color]=std::make_unique<Model>();
        if(!d.models[color]->load(color?"models/battle_balloon_blue.gmdl":"models/battle_balloon_red.gmdl"))return false;
        d.models[color]->freezeLighting();d.models[color]->enableStaticCulling();
    }
    d.sounds.clear();d.presentation.reset(racers,teams);for(auto& body:d.bodies)body=BalloonBody{};return true;
}
void BalloonBattleRuntime::lose(unsigned racer,bool audible){if(impl_)impl_->presentation.lose(racer,audible);}
void BalloonBattleRuntime::steal(unsigned thief,unsigned victim){if(impl_)impl_->presentation.steal(thief,victim);}
void BalloonBattleRuntime::replenish(unsigned racer){if(impl_)impl_->presentation.replenish(racer);}
void BalloonBattleRuntime::step(RaceEffects& effects){
    if(!impl_)return;auto& d=*impl_;
    const auto* race=K::System::RaceManager::Instance();
    // BalloonManager8086579c checks each player's finalized bit. Offline
    // endBattle finalizes every player together; read that per-player result
    // rather than using a scene-wide stage as the unit's release condition.
    for(unsigned i=0;i<d.racers;++i)if(race->player(i).finished())
        while(d.presentation.count(i))d.presentation.lose(i);
    auto* karts=K::Kart::KartObjectManager::Instance();
    std::array<BalloonAttachment,12> attachments{};
    for(unsigned i=0;i<d.racers;++i){const auto* kart=karts->object(i);const auto* param=kart->param();auto& p=attachments[i];
        const auto& pose=kart->body()->visualPose();p.position={pose[0,3],pose[1,3],pose[2,3]};
        p.right={pose[0,0],pose[1,0],pose[2,0]};p.up={pose[0,1],pose[1,1],pose[2,1]};p.front={pose[0,2],pose[1,2],pose[2,2]};
        p.scale=vec(kart->scale());p.megaScale=param->stats().megaScale;
        if(kart->isBike()){p.height=param->bikeDisp().m_balloonAnchorY;p.rear=param->bikeDisp().m_balloonAnchorZ;}
        else{p.height=param->kartDisp().m_balloonAnchorY;p.rear=param->kartDisp().m_balloonAnchorZ;}
    }
    d.presentation.step([&](unsigned i,BalloonPresentation::Event event,const BalloonPresentation::Command& command){
        auto& u=d.presentation.unit(i);auto& body=d.bodies[i];
        using S=BalloonPresentation::State;using E=BalloonPresentation::Event;
        auto sound=[&](unsigned id){d.sounds.push_back({uint8_t(i),uint16_t(id)});};
        if(event==E::Enter){
            if(u.state==S::Growing&&u.visualOwner<d.racers){
                body.init(attachments[u.visualOwner],u.visualSlot,d.presentation.count(u.visualOwner),u.scale);
                u.knot=body.knot;u.posed=true;
                // KartSound+e0 is m_isLocal (KartSound.hh); CPU inflation is silent.
                if(K::System::RaceConfig::Instance()->raceScenario().players[u.visualOwner].type==K::System::RaceConfig::Player::Type::Local)sound(0x1f5);
            }else if(u.state==S::Released&&command.audible)sound(0x1f6);
            return;
        }
        if(event==E::Pop){
            // The original effect handle uses the model position, orientation and full scale. Both roots emit for one
            // tick and recursively create their three authored children.
            if(body.initialized){EffectFrame frame;frame.origin=u.knot;frame.x=body.right;frame.y=body.up;frame.z=body.front;frame.scale=4.f;
                effects.burst(effects.find(u.color?"rk_baloonB":"rk_baloonR"),frame);}
            sound(0x1f7);return;
        }
        if(u.state==S::Hidden)return;
        const bool attached=u.visualOwner<d.racers;
        const BalloonAttachment* p=attached?&attachments[u.visualOwner]:nullptr;
        if(!body.initialized)return;
        Vec3 velocity{};bool reset=false;
        if(attached){const auto* kart=karts->object(u.visualOwner);velocity=vec(kart->dynamics()->velocity());
            reset=kart->status().onBit(K::Kart::eStatus::InRespawn,static_cast<K::Kart::eStatus>(142));
            if(reset&&!body.resetLatch)body.init(*p,u.visualSlot,d.presentation.count(u.visualOwner),u.scale);
        }
        body.resetLatch=reset;
        std::array<const BalloonBody*,5> peers{};unsigned peerCount=0;
        if(attached)for(unsigned j=0;j<d.presentation.count(u.visualOwner);++j){
            const unsigned index=d.presentation.slot(u.visualOwner,j);if(index<d.presentation.size()&&index!=i)peers[peerCount++]=&d.bodies[index];}
        // State2 calc passes r6=1; stolen state3 passes0 and stays state3.
        body.step(p,u.visualSlot,attached?d.presentation.count(u.visualOwner):0,u.scale,velocity,
            u.state==S::Attached,peers.data(),peerCount,
            []{return K::System::RaceManager::Instance()->random().getF32();});
        u.direction=body.up;u.knot=body.knot;u.posed=true;
    });
    // BalloonManager80865818 calculates all units before publishing model
    // snapshots. Peers must never observe this frame's earlier unit updates.
    for(unsigned i=0;i<d.presentation.size();++i)if(d.presentation.unit(i).state!=BalloonPresentation::State::Hidden)
        d.bodies[i].publish(d.presentation.unit(i).scale);
}
void BalloonBattleRuntime::fillAudio(RaceAudioFrame& frame){
    frame.balloons=impl_?uint8_t(impl_->presentation.size()):0;
    if(!impl_){frame.balloonSounds=0;return;}auto& d=*impl_;
    for(unsigned i=0;i<d.presentation.size();++i){const auto p=d.presentation.unit(i).knot;frame.balloon[i]={p.x,p.y,p.z};}
    unsigned moved=0;
    while(moved<d.sounds.size()&&frame.balloonSounds<frame.balloonSound.size())
        frame.balloonSound[frame.balloonSounds++]=d.sounds[moved++];
    d.sounds.erase(d.sounds.begin(),d.sounds.begin()+moved);
}
void BalloonBattleRuntime::draw(Renderer& renderer){
    if(!impl_)return;const auto& d=*impl_;
    for(unsigned i=0;i<d.presentation.size();++i){const auto& u=d.presentation.unit(i);
        using S=BalloonPresentation::State;
        if(!u.posed||u.state==S::Hidden||!d.models[u.color])continue;
        // Native8087afb4 forwards the current view's visibility to the model;
        // it does not inherit the owning kart's vanish/blink gate. In
        // particular a pending stolen balloon still has its old visual owner.
        const auto& b=d.bodies[i];C3D_Mtx transform;Mtx_Identity(&transform);
        transform.r[0]=FVec4_New(b.right.x*u.scale,b.up.x*u.scale,b.front.x*u.scale,u.knot.x);
        transform.r[1]=FVec4_New(b.right.y*u.scale,b.up.y*u.scale,b.front.y*u.scale,u.knot.y);
        transform.r[2]=FVec4_New(b.right.z*u.scale,b.up.z*u.scale,b.front.z*u.scale,u.knot.z);
        if(renderer.viewFrustum().visible(*d.models[u.color],transform))renderer.draw(*d.models[u.color],transform);
    }
}
}
