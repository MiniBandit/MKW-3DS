#include <mkw/racer_visuals.hpp>
#include <mkw/race_items.hpp>
#include <mkw/star_power.hpp>
#include <mkw/animation.hpp>
#include <mkw/driver_controller.hpp>
#include <mkw/garage.hpp>
#include <mkw/vehicle_animation.hpp>
#include <mkw/job_worker.hpp>
#include <mkw/renderer.hpp>
#include <mkw/render_quality.hpp>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/Status.hh>
#include <game/system/RaceConfig.hh>
#include <array>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace K=Kinoko;
struct RacerVisuals::Impl {
    struct Racer {
        const GarageChoice* choice=nullptr;
        std::unique_ptr<Model> body,driver,shadow;
        std::array<std::unique_ptr<Model>,4> wheels;
        std::array<std::unique_ptr<Model>,9> extras;
        std::unique_ptr<DriverAnimation> animation,bodyAnimation;
        std::array<std::unique_ptr<DriverAnimation>,9> extraAnimation;
        DriverController controller;
        DriverPose pose;
        VehicleAnimation vehicle;
        struct Clock {int clip=-1;unsigned start=0;} bodyClock;
        std::array<Clock,9> extraClocks;
    };
    std::array<std::unique_ptr<Racer>,11> racers;
    unsigned count=0;
    // One frame's pose and visibility per racer (prepareRacer), drawn in order.
    struct Prepared {
        bool draw=false,bodyVisible=false,driverVisible=false;
        uint32_t additive=0;
        C3D_Mtx body{},seated{};
        unsigned wheels=0;std::array<bool,4> wheelVisible{};std::array<C3D_Mtx,4> wheel{};
        struct Extra {uint8_t part=0;bool visible=false;C3D_Mtx matrix{};};
        unsigned extras=0;std::array<Extra,9*4> extra{};
    };
    std::array<Prepared,11> prepared;
    std::array<Job,11> jobs;
    std::array<bool,11> queued{};
    struct Frame {Impl* impl=nullptr;ViewFrustum view{};const RaceItems* items=nullptr;Vec3 viewer{};unsigned tick=0;bool open=false;Vec3 camera{};ClipFrustum world{};} frame;
    void openFrame(const Renderer& renderer,const RaceItems& items,Vec3 viewer,unsigned tick){
        C3D_Mtx identity;Mtx_Identity(&identity);
        frame={this,renderer.viewFrustum(),&items,viewer,tick,true,renderer.cameraPosition()};frame.world=frame.view.model(identity);
    }
    std::array<std::pair<Impl*,unsigned>,11> jobContext{};
    void prepareRacer(unsigned i);
};
RacerVisuals::RacerVisuals():impl_(std::make_unique<Impl>()){}
RacerVisuals::~RacerVisuals()=default;
void RacerVisuals::reset(){for(auto& r:impl_->racers)r.reset();impl_->count=0;}
unsigned RacerVisuals::count() const{return impl_->count;}
bool RacerVisuals::load(){
    reset();const auto& config=K::System::RaceConfig::Instance()->raceScenario();
    bool valid=true;
    for(unsigned i=1;i<config.playerCount&&i<=11;++i){
        auto r=std::make_unique<Impl::Racer>();const auto& player=config.players[i];
        for(const auto& choice:GarageChoices)if(choice.character==unsigned(player.character)&&choice.vehicle==unsigned(player.vehicle)){r->choice=&choice;break;}
        if(!r->choice){valid=false;continue;}
        auto model=[&](const char* suffix){auto p=std::make_unique<Model>();char path[160];std::snprintf(path,sizeof path,"models/%s%s.gmdl",r->choice->prefix,suffix);if(!p->load(path))p.reset();return p;};
        auto bank=[&](const char* suffix,Model* model){auto p=std::make_unique<DriverAnimation>();char path[160];std::snprintf(path,sizeof path,"models/%s%s.dskm",r->choice->prefix,suffix);if(!model||!p->load(path,*model))p.reset();else p->prepareLitSkinning(*model);return p;};
        r->body=model("body");r->driver=model("driver");r->shadow=model("shadow");r->animation=bank("driver",r->driver.get());
        const char* wheelNames[]={"tire_fl","tire_fr","tire_rl","tire_rr"};
        for(unsigned w=0;w<(r->choice->bike?2u:4u);++w){r->wheels[w]=model(wheelNames[w]);valid&=bool(r->wheels[w]);}
        if(r->choice->bodyAnimation){r->bodyAnimation=bank("body",r->body.get());valid&=bool(r->bodyAnimation);}
        for(unsigned j=0;j<9;++j)if(r->choice->extrasMask&(1u<<j)){
            r->extras[j]=model(GarageExtraNames[j]);valid&=bool(r->extras[j]);
            if(r->choice->extraAnimationMask&(1u<<j)){r->extraAnimation[j]=bank(GarageExtraNames[j],r->extras[j].get());valid&=bool(r->extraAnimation[j]);}
        }
        valid&=bool(r->body)&&bool(r->driver)&&bool(r->animation)&&bool(r->shadow);
        if(r->animation)for(unsigned c=0;c<r->animation->count();++c)r->controller.setDuration(r->animation->name(c),r->animation->duration(c));
        impl_->racers[i-1]=std::move(r);++impl_->count;
    }
    resetAnimation();return valid;
}
void RacerVisuals::resetAnimation(){
    for(unsigned i=0;i<11;++i)if(auto* r=impl_->racers[i].get()){
        r->controller.reset();r->pose={};r->bodyClock={};r->extraClocks={};
        r->vehicle.reset();r->vehicle.step(*K::Kart::KartObjectManager::Instance()->object(i+1));
    }
}
void RacerVisuals::step(unsigned tick){
    for(unsigned i=0;i<11;++i)if(auto* r=impl_->racers[i].get()){
        auto* kart=K::Kart::KartObjectManager::Instance()->object(i+1);auto& status=kart->status();
        r->vehicle.step(*kart);DriverSignals s;
        s.bike=kart->param()->isBike();s.wheelie=status.onBit(K::Kart::eStatus::Wheelie);
        s.airborne=status.offBit(K::Kart::eStatus::TouchingGround);s.hopping=status.onBit(K::Kart::eStatus::Hop);
        s.inCannon=status.onBit(K::Kart::eStatus::InCannon);
        s.steering=kart->inputs()->currentState().stick.x;s.speed=kart->move()->speed();
        s.drift=status.onBit(K::Kart::eStatus::DriftManual,K::Kart::eStatus::DriftAuto);s.driftDirection=kart->move()->hopStickX();
        s.boost=status.onBit(K::Kart::eStatus::Boost);s.trick=status.onBit(K::Kart::eStatus::InATrick,K::Kart::eStatus::ZipperTrick);
        s.hit=status.onBit(K::Kart::eStatus::InAction,K::Kart::eStatus::Burnout,K::Kart::eStatus::Shocked);
        s.accelerating=status.onBit(K::Kart::eStatus::Accelerate);s.braking=status.onBit(K::Kart::eStatus::Brake);
        s.blink=(tick+i*13)%180==0;r->pose=r->controller.step(s);
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
    auto& out=prepared[i];out=Prepared{};
    auto* r=racers[i].get();if(!r)return;
    if(!r->body||!r->driver||!r->animation)return;
    const auto& items=*frame.items;const unsigned tick=frame.tick;const auto& view=frame.view;
    if(items.hud(i+1).bullet)return;
    // Reduced detail: a racer whose generous envelope (1,000 units, scaled) is
    // wholly outside the view is neither evaluated, posed nor drawn this frame.
    if(renderQuality().reduced){
        const Vec3 p=r->vehicle.position();const float e=1000.f*r->vehicle.maxScale();
        if(!frame.world.intersects({p-Vec3{e,e,e},p+Vec3{e,e,e}}))return;
    }
    const auto& transform=r->vehicle.body();Vec3 position{transform.r[0].w,transform.r[1].w,transform.r[2].w};
    if(dot(position-frame.viewer,position-frame.viewer)>25000.f*25000.f)return;
    const auto& quality=renderQuality();const Vec3 camera=frame.camera;
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
    if(r->choice->bike&&!std::strncmp(name,"sjump_",6)&&std::strlen(name)==7){const char* bikeNames[]={"sjump_ma","sjump_mb","sjump_mc"};if(name[6]>='a'&&name[6]<='c')name=bikeNames[name[6]-'a'];}
    if(!std::strcmp(name,"jump")&&r->animation->find(name)<0){name="jump_st";int c=r->animation->find(name);frame=c>=0?r->animation->duration(c)-1:0;}
    int expression=r->animation->face(name);float faceFrame=frame;
    if(expression<0){expression=r->animation->face(r->pose.faceName);faceFrame=r->pose.faceFrame;}
    C3D_Mtx seated=transform;Mtx_Translate(&seated,0,r->choice->seatY,r->choice->seatZ,true);
    auto frustum=view.model(seated);
    out.draw=true;out.body=transform;out.seated=seated;
    out.driverVisible=!tiny(*r->driver,seated)&&r->animation->apply(*r->driver,r->animation->find(name),frame,tick,expression,faceFrame,&frustum);
    out.additive=starMaterialColor(items.hud(i+1).star);
    out.bodyVisible=animate(r->bodyAnimation.get(),*r->body,r->bodyClock,false,transform);
    out.wheels=r->vehicle.wheelCount();
    for(unsigned w=0;w<out.wheels;++w){
        out.wheel[w]=r->vehicle.wheel(w);const auto& model=r->wheels[r->vehicle.modelIndex(w)];
        out.wheelVisible[w]=model&&!tiny(*model,out.wheel[w])&&view.visible(*model,out.wheel[w]);
    }
    for(unsigned j=0;j<9;++j)if(r->extras[j]){
        for(unsigned n=0;n<r->vehicle.accessoryCount(GarageExtraNames[j]);++n)
        {
            auto& e=out.extra[out.extras++];e.part=uint8_t(j);
            e.matrix=r->vehicle.accessory(GarageExtraNames[j],r->extraAnimation[j]&&r->extraAnimation[j]->handleDriveClip()>=0,n);
            e.visible=animate(r->extraAnimation[j].get(),*r->extras[j],r->extraClocks[j],j==0,e.matrix);
        }
    }
}
void RacerVisuals::prepare(const Renderer& renderer,const RaceItems& items,Vec3 viewer,unsigned tick){
    auto& d=*impl_;
    d.openFrame(renderer,items,viewer,tick);
    auto& worker=jobWorker();
    for(unsigned i=0;i<11;++i){
        d.queued[i]=false;auto* r=d.racers[i].get();if(!r)continue;
        // CPU-skinned banks share one scratch buffer: those racers stay on this thread.
        bool gpu=r->animation&&r->animation->gpuSkinned()&&(!r->bodyAnimation||r->bodyAnimation->gpuSkinned());
        for(const auto& a:r->extraAnimation)gpu&=!a||a->gpuSkinned();
        if(!gpu||!worker.running())continue;
        d.jobContext[i]={&d,i};
        d.jobs[i].reset([](void* c){auto* p=static_cast<std::pair<Impl*,unsigned>*>(c);p->first->prepareRacer(p->second);},&d.jobContext[i]);
        worker.submit(d.jobs[i]);d.queued[i]=true;
    }
}
void RacerVisuals::draw(Renderer& renderer,const RaceItems& items,Vec3 viewer,unsigned tick,const Model* playerShadow,const C3D_Mtx* playerTransform){
    auto& d=*impl_;
    if(!d.frame.open){d.openFrame(renderer,items,viewer,tick);d.queued.fill(false);}
    auto drawSkinned=[&](const DriverAnimation* bank,const Model& model,const C3D_Mtx& matrix){
        if(bank&&bank->gpuSkinned())renderer.setSkinPalette(bank->palette(),bank->matrixCount(),21);
        renderer.draw(model,matrix);
        renderer.setSkinPalette(nullptr,0,12);
    };
    // Run the racers the worker has not reached yet here, from the back of its
    // queue (it takes them from the front), so this thread waits only for jobs
    // already running. Draws keep their order below.
    for(unsigned i=11;i-->0;)if(d.queued[i])jobWorker().help(d.jobs[i]);
    std::array<Renderer::ShadowVolume,12> shadows{};unsigned shadowCount=0;
    if(playerShadow&&playerTransform)shadows[shadowCount++]={playerShadow,*playerTransform};
    for(unsigned i=0;i<11;++i)if(auto* r=d.racers[i].get()){
        if(d.queued[i])jobWorker().finish(d.jobs[i]);else d.prepareRacer(i);
        const auto& out=d.prepared[i];
        if(out.draw&&r->shadow&&d.frame.view.visible(*r->shadow,out.body))shadows[shadowCount++]={r->shadow.get(),out.body};
    }
    // Submit all racers in one stencil batch: overlaps darken once. Resolve
    // before the racer models, so their own volumes cannot shade the casters.
    renderer.drawShadowVolumes(shadows.data(),shadowCount,.5f);
    for(unsigned i=0;i<11;++i)if(auto* r=d.racers[i].get()){
        const auto& out=d.prepared[i];
        if(!out.draw)continue;
        renderer.additiveColor=out.additive;
        if(out.bodyVisible)drawSkinned(r->bodyAnimation.get(),*r->body,out.body);
        if(out.driverVisible)drawSkinned(r->animation.get(),*r->driver,out.seated);
        for(unsigned w=0;w<out.wheels;++w)if(out.wheelVisible[w])renderer.draw(*r->wheels[r->vehicle.modelIndex(w)],out.wheel[w]);
        for(unsigned e=0;e<out.extras;++e){const auto& x=out.extra[e];if(x.visible)drawSkinned(r->extraAnimation[x.part].get(),*r->extras[x.part],x.matrix);}
        renderer.additiveColor=0;
    }
    d.frame.open=false;
}
}
