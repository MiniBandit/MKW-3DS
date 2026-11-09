#include <mkw/racer_visuals.hpp>
#include <mkw/sim_profile.hpp>
#include <mkw/asset_pack.hpp>
#include <3ds.h>
#include <malloc.h>
#include <mkw/diag.hpp>
#include <mkw/racer_lod.hpp>
#include <mkw/vehicle_lod_layout.hpp>
#include <mkw/race_items.hpp>
#include <mkw/star_power.hpp>
#include <mkw/animation.hpp>
#include <mkw/driver_controller.hpp>
#include <mkw/driver_item_signals.hpp>
#include <mkw/driver_look_data.hpp>
#include <mkw/driver_look_gate.hpp>
#include <game/system/RaceManager.hh>
#include <mkw/garage.hpp>
#include <mkw/vehicle_visual_assets.hpp>
#include <mkw/vehicle_animation.hpp>
#include <mkw/luma_visual.hpp>
#include <mkw/job_worker.hpp>
#include <mkw/race_teams.hpp>
#include <mkw/renderer.hpp>
#include <mkw/render_quality.hpp>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartPhysics.hh>
#include <game/kart/Status.hh>
#include <game/system/RaceConfig.hh>
#include <array>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace K=Kinoko;
struct RacerVisuals::Impl {
    struct Racer : VehicleVisualAssets {
        std::unique_ptr<LumaVisual> luma;
        std::unique_ptr<Model> driverLod,bodyLod,bodyLodRear,handleLod,handleLodRear;RacerLod lod;
        DriverController controller;
        DriverPose pose;
        VehicleAnimation vehicle;
        struct Clock {int clip=-1;unsigned start=0;} bodyClock;
        std::array<Clock,9> extraClocks;
        bool ghost=false;
        // Set by the last drawn frame (prepareRacer): the animated driver (LOD 1)
        // was in view. Reduced quality (Old 3DS) aims heads only for those.
        bool headShown=true;
        unsigned presentedTick=0; // last tick step() presented it (thinHidden)
        // One-tick driver events seen on ticks it was not presented (thinHidden).
        int pendingThrow=0;bool pendingCelebrate=false;
    };
    std::array<std::unique_ptr<Racer>,11> racers;
    unsigned count=0;
    // One frame's pose and visibility per racer (prepareRacer), drawn in order.
    struct Prepared {
        bool draw=false,bodyVisible=false,driverVisible=false;
        unsigned lod=1;
        uint32_t additive=0;
        C3D_Mtx body{},seated{},lowHandle{};bool lowHandleVisible=false;
        unsigned wheels=0;std::array<bool,4> wheelVisible{};std::array<C3D_Mtx,4> wheel{};
        struct Extra {uint8_t part=0;bool visible=false;C3D_Mtx matrix{};};
        unsigned extras=0;std::array<Extra,9*4> extra{};
    };
    std::array<Prepared,11> prepared;
    std::array<Job,11> jobs;
    std::array<bool,11> queued{};
    struct Frame {Impl* impl=nullptr;ViewFrustum view{};const RaceItems* items=nullptr;Vec3 viewer{};unsigned tick=0;bool open=false;Vec3 camera{};ClipFrustum world{};float fov=60;bool fullDetail=true;} frame;
    void openFrame(const Renderer& renderer,const RaceItems& items,Vec3 viewer,unsigned tick,bool fullDetail=true){
        C3D_Mtx identity;Mtx_Identity(&identity);
        frame={this,renderer.viewFrustum(),&items,viewer,tick,true,renderer.cameraPosition()};frame.world=frame.view.model(identity);frame.fov=renderer.cameraFov();frame.fullDetail=fullDetail;
    }
    std::array<std::pair<Impl*,unsigned>,11> jobContext{};
    void prepareRacer(unsigned i);
    void drawPrepared(Renderer& renderer,unsigned i);
};
RacerVisuals::RacerVisuals():impl_(std::make_unique<Impl>()){}
RacerVisuals::~RacerVisuals()=default;
void RacerVisuals::reset(){for(auto& r:impl_->racers)r.reset();impl_->count=0;}
unsigned RacerVisuals::count() const{return impl_->count;}
bool RacerVisuals::load(){
    reset();const auto& config=K::System::RaceConfig::Instance()->raceScenario();
    // Banks kept from the last race (DriverAnimation::keepReleased) that this
    // lineup doesn't use go now, before its own banks are allocated.
    {std::vector<std::string> banks;
     for(unsigned i=1;i<config.playerCount&&i<=11;++i){const auto& player=config.players[i];
      VehicleVisualAssets::bankPaths(unsigned(player.character),unsigned(player.vehicle),config.teams?RaceTeam(player.team):RaceTeam::None,diag::unlitBanks,banks);}
     DriverAnimation::keepOnly(banks);}
    // The course's G3D lights are in view space: the Wii lights a racer from
    // the camera's side. Opponents keep baked light (no per-vertex cost), now
    // as the chase camera sees them: from behind, looking 12 degrees down,
    // instead of from the front.
    static const float RearView[3][3]={{-1,0,0},{0,.9781476f,.2079117f},{0,.2079117f,-.9781476f}};
    struct BakeView {BakeView(){Model::setBakeView(diag::rearBake?RearView:nullptr);}~BakeView(){Model::setBakeView(nullptr);}} bakeViewScope;
    for(unsigned i=1;i<config.playerCount&&i<=11;++i){
        auto r=std::make_unique<Impl::Racer>();const auto& player=config.players[i];
        std::string failure;
        if(!r->load(unsigned(player.character),unsigned(player.vehicle),config.teams?RaceTeam(player.team):RaceTeam::None,&failure,diag::unlitBanks)){
            if(auto* log=std::fopen("sdmc:/3ds/mkw-3ds/opponent-load-error.txt","w")){
                // Free heap and the largest LINEAR block (a free total says nothing about fragmentation).
                const struct mallinfo m=mallinfo();const size_t heapTotal=envGetHeapSize();
                const size_t heapFree=(heapTotal>size_t(m.arena)?heapTotal-size_t(m.arena):0)+size_t(m.fordblks);
                size_t lo=0,hi=linearSpaceFree();
                while(hi-lo>4096){const size_t mid=lo+(hi-lo)/2;if(void* p=linearAlloc(mid)){linearFree(p);lo=mid;}else hi=mid;}
                std::fprintf(log,"slot=%u character=%u vehicle=%u team=%u\nasset=%s\nbank_failure=%s\npack_failure=%s\nlinear=%lu\nlinear_largest=%lu\nheap_free=%lu\n",i,unsigned(player.character),unsigned(player.vehicle),unsigned(player.team),failure.c_str(),driverBankFailure,pack::lastReadFailure,static_cast<unsigned long>(linearSpaceFree()),static_cast<unsigned long>(lo),static_cast<unsigned long>(heapFree));
                std::fclose(log);
            }
            reset();return false;
        }
        auto freeze=[](auto& model,auto& bank){if(!model)return;
            model->freezeLighting(bank&&bank->gpuSkinned()?bank->palette():nullptr);
            if(bank&&bank->gpuSkinned()){
                bank->shareDrawVertices(*model);
                model->enableSkinCulling(bank->matrixCount());
            }else if(!bank)model->enableStaticCulling();
        };
        char lowName[160];std::snprintf(lowName,sizeof lowName,"models/%sdriver_lod.gmdl",r->choice->prefix);
        auto low=std::make_unique<Model>();
        if(low->load(lowName)){low->freezeLighting();low->enableStaticCulling();r->driverLod=std::move(low);}
        auto loadLow=[&](const char* part){char name[160];std::snprintf(name,sizeof name,"models/%s%s%s.gmdl",r->choice->prefix,teamVehicleSuffix(unsigned(player.vehicle),config.teams?RaceTeam(player.team):RaceTeam::None),part);auto model=std::make_unique<Model>();if(!model->load(name))return std::unique_ptr<Model>{};model->freezeLighting();model->enableStaticCulling();return model;};
        r->bodyLod=loadLow("body_lod");r->bodyLodRear=loadLow("body_lod_rear");
        if(VehicleLodLayout[r->choice->vehicle].handle){r->handleLod=loadLow("handle_lod");r->handleLodRear=loadLow("handle_lod_rear");}
        freeze(r->driver,r->animation);freeze(r->body,r->bodyAnimation);
        for(unsigned n=0;n<r->extras.size();++n)freeze(r->extras[n],r->extraAnimation[n]);
        for(auto& wheel:r->wheels)if(wheel){wheel->freezeLighting();wheel->enableStaticCulling();}
        if(unsigned(player.character)==23){
            r->luma=std::make_unique<LumaVisual>();
            if(!r->luma->load()){reset();return false;}
        }
        if(r->animation)for(unsigned c=0;c<r->animation->count();++c)r->controller.setDuration(r->animation->name(c),r->animation->duration(c));
        if(r->animation&&r->choice->bike)for(const char* name:{"sjump_a","sjump_b","sjump_c"}){
            const int c=r->animation->find(driverVehicleClipName(name,true));
            if(c>=0)r->controller.setDuration(name,r->animation->duration(c));
        }
        r->ghost=player.type==K::System::RaceConfig::Player::Type::Ghost;
        impl_->racers[i-1]=std::move(r);++impl_->count;
    }
    resetAnimation();return true;
}
void RacerVisuals::resetAnimation(){
    for(unsigned i=0;i<11;++i)if(auto* r=impl_->racers[i].get()){
        if(r->animation)r->animation->resetLook();
        r->controller.reset();r->lod={};r->pose={};r->bodyClock={};r->extraClocks={};
        r->vehicle.reset();r->vehicle.step(*K::Kart::KartObjectManager::Instance()->object(i+1));
    }
}
void RacerVisuals::step(unsigned tick,const RaceItems& items,bool drawNext,std::span<const DriverCollisionLook> collisions){
    for(unsigned i=0;i<11;++i)if(auto* r=impl_->racers[i].get()){
        auto* kart=K::Kart::KartObjectManager::Instance()->object(i+1);auto& status=kart->status();
        const bool hidden=thinHidden&&!r->headShown;
        if(hidden&&!drawNext){
            for(const auto& event:items.sounds())if(event.player==i+1){
                if(event.cue==ItemSoundCue::HitSuccess)r->pendingCelebrate=true;
                else if(event.cue==ItemSoundCue::Release)r->pendingThrow=event.flag?1:-1;
            }
            continue;
        }
        const unsigned frames=hidden?std::clamp(tick-r->presentedTick,1u,8u):1u;r->presentedTick=tick;
        {MKW_SIM_SCOPE(OppVehicle);r->vehicle.step(*kart,drawNext||r->headShown,frames);}DriverSignals s;
        s.bike=kart->param()->isBike();s.handleRelativeBike=kart->param()->stats().body==K::Kart::KartParam::Stats::Body::Handle_Relative_Bike;s.wheelie=status.onBit(K::Kart::eStatus::Wheelie);
        s.airborne=status.offBit(K::Kart::eStatus::TouchingGround);s.hopping=status.onBit(K::Kart::eStatus::Hop);
        s.steering=kart->move()->cpuSteer()?kart->move()->cpuStick():kart->inputs()->currentState().stick.x;
                s.burnout=status.onBit(K::Kart::eStatus::Burnout);
                s.burnoutPitch=kart->move()->burnout().pitch();
                s.inCannon=status.onBit(K::Kart::eStatus::InCannon);
                s.vehicleId=unsigned(K::System::RaceConfig::Instance()->raceScenario().players[i+1].vehicle);
                s.frozenInIce=K::System::RaceConfig::Instance()->raceScenario().course==K::Course::N64_Sherbet_Land&&status.onBit(K::Kart::eStatus::InRespawn,K::Kart::eStatus::AfterRespawn);s.speed=kart->move()->speed();
        s.drift=status.onBit(K::Kart::eStatus::DriftManual,K::Kart::eStatus::DriftAuto);s.driftDirection=kart->move()->hopStickX();
        s.boost=status.onBit(K::Kart::eStatus::Boost);s.trick=status.onBit(K::Kart::eStatus::InATrick,K::Kart::eStatus::ZipperTrick);
        s.hit=status.onBit(K::Kart::eStatus::InAction,K::Kart::eStatus::Burnout);
        s.accelerating=status.onBit(K::Kart::eStatus::Accelerate);s.braking=status.onBit(K::Kart::eStatus::Brake);
        const auto* race=K::System::RaceManager::Instance();
        const auto& player=race->player(i+1);
        s.finished=player.finished();
        const unsigned rank=player.finishOrder();
        s.finishType=driverFinishType(rank,K::System::RaceConfig::Instance()->raceScenario().playerCount);
        s.countdown=unsigned(race->stage())<=1;
        driverItemSignals(s,items,i+1);
        if(!s.itemThrowDirection)s.itemThrowDirection=r->pendingThrow;
        s.celebrate|=r->pendingCelebrate;r->pendingThrow=0;r->pendingCelebrate=false;
        if(i+1<collisions.size())collisions[i+1].apply(s,unsigned(r->choice->character));
        // A step standing for several ticks blinks if any of them would have.
        s.blink=(tick+i*13)%180<frames;
        {MKW_SIM_SCOPE(OppController);r->pose=r->controller.step(s,frames);}
        // Reduced quality: a racer drawn with its static low-detail driver, or not
        // drawn at all, shows no head aim; its look filters resume (from where they
        // stopped) once it is drawn up close again. ~0.18 ms a racer a tick on Old 3DS.
        if(r->animation&&(r->headShown||!renderQuality().reduced)){MKW_SIM_SCOPE(OppLook);
            DriverLookParameters parameters;
            const bool available=driverLookParameters(r->choice->character,r->choice->vehicle,parameters);
            C3D_Mtx seated=r->vehicle.body();Mtx_Translate(&seated,0,r->choice->seatY,r->choice->seatZ,true);
            r->animation->lookPhase=i;
            r->animation->stepLook(r->animation->find(driverVehicleClipName(r->pose.clipName,r->choice->bike)),r->pose.frame,tick,
                available&&s.hasLookTarget&&driverLookAllowed(r->pose.clipName,
                    status.onBit(K::Kart::eStatus::InATrick),status.onBit(K::Kart::eStatus::OverZipper)),
                s.lookTarget,seated,false,parameters,r->pose,s.handleRelativeBike,kart->physics()->pose()[1,0],kart->physics()->pose()[1,2],s.lookResponse);
        }
        auto advance=[&](DriverAnimation* bank,Impl::Racer::Clock& clock,bool handle){
            if(!bank)return;
            const int clip=handle&&bank->handleDriveClip()>=0?bank->handleDriveClip():!std::strcmp(r->pose.clipName,"dash")&&bank->dashClip()>=0?bank->dashClip():bank->shakeClip();
            if(clip>=0&&clock.clip!=clip){clock.clip=clip;clock.start=tick;}
        };
        advance(r->bodyAnimation.get(),r->bodyClock,false);
        for(unsigned j=0;j<9;++j)advance(r->extraAnimation[j].get(),r->extraClocks[j],j==0);
    }
}
void RacerVisuals::Impl::prepareRacer(unsigned i){
    // Flags and counts only (not out=Prepared{}, ~3 KB a racer a frame): every matrix a
    // reader uses is written before its flag or count says so.
    auto& out=prepared[i];out.draw=out.bodyVisible=out.driverVisible=out.lowHandleVisible=false;
    out.lod=1;out.additive=0;out.wheels=0;out.wheelVisible={};out.extras=0;
    auto* r=racers[i].get();if(!r)return;
    r->headShown=false;
    if(!r->body||!r->driver||!r->animation)return;
    const auto& items=*frame.items;const unsigned tick=frame.tick;const auto& view=frame.view;
    if(items.hud(i+1).bullet)return;
    // A racer whose generous envelope (1,000 units, scaled) is
    // wholly outside the view is neither evaluated, posed nor drawn this frame.
    {
        const Vec3 p=r->vehicle.position();const float e=1000.f*r->vehicle.maxScale();
        if(!frame.world.intersects({p-Vec3{e,e,e},p+Vec3{e,e,e}}))return;
    }
    const auto& transform=r->vehicle.body();Vec3 position{transform.r[0].w,transform.r[1].w,transform.r[2].w};
    if(dot(position-frame.viewer,position-frame.viewer)>25000.f*25000.f)return;
    const auto& quality=renderQuality();const Vec3 camera=frame.camera;
    const float lodScale=std::max(.01f,r->vehicle.maxScale());
    out.lod=r->lod.update(dot(position-camera,position-camera)/(lodScale*lodScale),frame.fov,frame.fullDetail||!r->driverLod||!r->bodyLod||!r->bodyLodRear||(VehicleLodLayout[r->choice->vehicle].handle&&(!r->handleLod||!r->handleLodRear)));
    // Reduced detail: parts too small to see are neither posed nor drawn.
    auto tiny=[&](const Model& model,const C3D_Mtx& matrix){return quality.tooSmall({model.minimum,model.maximum},matrix,camera);};
    auto animate=[&](DriverAnimation* bank,Model& model,Racer::Clock& clock,bool handle,const C3D_Mtx& matrix){
        if(tiny(model,matrix))return false;
        if(!bank)return view.visible(model,matrix);
        const int c=handle&&bank->handleDriveClip()>=0?bank->handleDriveClip():!std::strcmp(r->pose.clipName,"dash")&&bank->dashClip()>=0?bank->dashClip():bank->shakeClip();
        if(c<0)return true;
        if(c!=clock.clip){clock.clip=c;clock.start=tick;}
        float f=handle?r->vehicle.driveFrame()*(bank->duration(c)-1)/30.f:float(tick-clock.start);
        auto frustum=view.model(matrix);
        return bank->apply(model,c,f,tick,-1,0,&frustum);
    };
    const char* name=r->pose.clipName;float frame=r->pose.frame;
    name=driverVehicleClipName(name,r->choice->bike);
    if(!std::strcmp(name,"jump")&&r->animation->find(name)<0){name="jump_st";int c=r->animation->find(name);frame=c>=0?r->animation->duration(c)-1:0;}
    int expression=r->animation->face(name);float faceFrame=frame;
    if(expression<0){expression=r->animation->face(r->pose.faceName);faceFrame=r->pose.faceFrame;}
    C3D_Mtx seated=transform;Mtx_Translate(&seated,0,r->choice->seatY,r->choice->seatZ,true);
    auto frustum=view.model(seated);
    out.draw=K::Kart::KartObjectManager::Instance()->object(i+1)->move()->battleVisible();out.body=transform;out.seated=seated;
    if(r->luma&&out.lod<4)r->luma->prepare(view,r->vehicle,tick);
    r->headShown=out.lod==1;
    if(out.lod==1){r->animation->setBraidWorld(seated);
    out.driverVisible=!tiny(*r->driver,seated)&&r->animation->apply(*r->driver,r->animation->find(name),frame,tick,expression,faceFrame,&frustum);
    }else if(out.lod<4)out.driverVisible=view.visible(*r->driverLod,seated);
    out.additive=starMaterialColor(items.hud(i+1).star)|
        uint32_t(items.hud(i+1).inkShade);
    if(out.lod>=2){
        const Model& body=*(out.lod>=3?r->bodyLodRear:r->bodyLod);
        out.bodyVisible=view.visible(body,transform);
        if(r->handleLod){out.lowHandle=r->vehicle.accessory("handle",false);out.lowHandleVisible=view.visible(*(out.lod>=3?r->handleLodRear:r->handleLod),out.lowHandle);}
        if(!VehicleLodLayout[r->choice->vehicle].wheels)return;
        // Some authored low bodies keep separate tires. Reuse their existing
        // physical wheel transforms; no new simulation or floor tests.
    }else{
        out.bodyVisible=animate(r->bodyAnimation.get(),*r->body,r->bodyClock,false,transform);
    }
    out.wheels=r->vehicle.wheelCount();
    for(unsigned w=0;w<out.wheels;++w){
        // Native modelIndex maps a three-wheel chassis to slots 1,2,3.
        const unsigned slot=r->vehicle.modelIndex(w);
        if(out.lod>=3&&slot<(r->choice->bike?1u:2u))continue;
        out.wheel[w]=r->vehicle.wheel(w);const auto& model=r->wheels[r->vehicle.modelIndex(w)];
        out.wheelVisible[w]=model&&!tiny(*model,out.wheel[w])&&view.visible(*model,out.wheel[w]);
    }
    if(out.lod>=2)return;
    for(unsigned j=0;j<9;++j)if(r->extras[j]){
        for(unsigned n=0;n<r->vehicle.accessoryCount(GarageExtraNames[j]);++n)
        {
            auto& e=out.extra[out.extras++];e.part=uint8_t(j);
            e.matrix=r->vehicle.accessory(GarageExtraNames[j],r->extraAnimation[j]&&r->extraAnimation[j]->handleDriveClip()>=0,n);
            e.visible=animate(r->extraAnimation[j].get(),*r->extras[j],r->extraClocks[j],j==0,e.matrix);
        }
    }
}
void RacerVisuals::prepare(const Renderer& renderer,const RaceItems& items,Vec3 viewer,unsigned tick,bool fullDetail){
    auto& d=*impl_;
    d.openFrame(renderer,items,viewer,tick,fullDetail);
    auto& worker=jobWorker();
    for(unsigned i=0;i<11;++i){
        d.queued[i]=false;auto* r=d.racers[i].get();if(!r)continue;
        // CPU-skinned banks share one scratch buffer: those racers stay on this thread.
        bool gpu=r->animation&&r->animation->gpuSkinned()&&(!r->bodyAnimation||r->bodyAnimation->gpuSkinned());
        for(const auto& a:r->extraAnimation)gpu&=!a||a->gpuSkinned();
        if(!gpu||!worker.running())continue;
        d.jobContext[i]={&d,i};
        d.jobs[i].reset([](void* c){auto* p=static_cast<std::pair<Impl*,unsigned>*>(c);p->first->prepareRacer(p->second);},&d.jobContext[i]);
        worker.submit(d.jobs[i],false);d.queued[i]=true;
    }
    worker.wake();
}
void RacerVisuals::draw(Renderer& renderer,const RaceItems& items,Vec3 viewer,unsigned tick,const Model* playerShadow,const C3D_Mtx* playerTransform){
    auto& d=*impl_;
    if(!d.frame.open){d.openFrame(renderer,items,viewer,tick);d.queued.fill(false);}
    // Run the racers the worker has not reached yet here, from the back of its
    // queue (it takes them from the front), so this thread waits only for jobs
    // already running. Draws keep their order below.
    for(unsigned i=11;i-->0;)if(d.queued[i])jobWorker().help(d.jobs[i]);
    std::array<Renderer::ShadowVolume,12> shadows{};unsigned shadowCount=0;
    if(playerShadow&&playerTransform)shadows[shadowCount++]={playerShadow,*playerTransform};
    for(unsigned i=0;i<11;++i)if(auto* r=d.racers[i].get()){
        if(d.queued[i])jobWorker().finish(d.jobs[i]);else d.prepareRacer(i);
        const auto& out=d.prepared[i];
        if(out.draw&&!r->ghost&&r->shadow&&d.frame.view.visible(*r->shadow,out.body))shadows[shadowCount++]={r->shadow.get(),out.body};
    }
    // Submit all racers in one stencil batch: overlaps darken once. Resolve
    // before the racer models, so their own volumes cannot shade the casters.
    renderer.drawShadowVolumes(shadows.data(),shadowCount,.5f);
    for(unsigned i=0;i<11;++i)if(d.racers[i]&&!d.racers[i]->ghost)d.drawPrepared(renderer,i);
    d.frame.open=false;
}
void RacerVisuals::drawGhosts(Renderer& renderer){
    // Retail ghosts are see-through but hide their own far side: depth first.
    for(unsigned i=0;i<11;++i)if(impl_->racers[i]&&impl_->racers[i]->ghost){
        for(uint8_t pass=1;pass<=2;++pass){renderer.ghostPass=pass;impl_->drawPrepared(renderer,i);}
        renderer.ghostPass=0;
    }
}
void RacerVisuals::Impl::drawPrepared(Renderer& renderer,unsigned i){
    auto drawSkinned=[&](const DriverAnimation* bank,const Model& model,const C3D_Mtx& matrix){
        if(bank&&bank->gpuSkinned())renderer.setSkinPalette(bank->palette(),bank->matrixCount(),21);
        renderer.draw(model,matrix);
        renderer.setSkinPalette(nullptr,0,12);
    };
    auto* r=racers[i].get();const auto& out=prepared[i];
    if(!r||!out.draw)return;
    {
        renderer.additiveColor=out.additive;
        if(out.bodyVisible){if(out.lod==1)drawSkinned(r->bodyAnimation.get(),*r->body,out.body);else renderer.draw(*(out.lod>=3?r->bodyLodRear:r->bodyLod),out.body);}
        if(out.lowHandleVisible)renderer.draw(*(out.lod>=3?r->handleLodRear:r->handleLod),out.lowHandle);
        if(out.driverVisible){if(out.lod==1)drawSkinned(r->animation.get(),*r->driver,out.seated);else renderer.draw(*r->driverLod,out.seated);}
        if(r->luma&&out.lod<4)r->luma->draw(renderer);
        for(unsigned w=0;w<out.wheels;++w)if(out.wheelVisible[w])renderer.draw(*r->wheels[r->vehicle.modelIndex(w)],out.wheel[w]);
        for(unsigned e=0;e<out.extras;++e){const auto& x=out.extra[e];if(x.visible)drawSkinned(r->extraAnimation[x.part].get(),*r->extras[x.part],x.matrix);}
        renderer.additiveColor=0;
    }
}
}
