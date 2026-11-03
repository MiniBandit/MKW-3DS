#include <mkw/mii_garage.hpp>
#include <mkw/menu_unlock_model.hpp>
#include "mkw/menu_models.hpp"
#include "mkw/menu_model_params.hpp"
#include "mkw/menu_model_motion.hpp"
#include "mkw/menu_lighting.hpp"
#include "mkw/native_mii_head.hpp"
#include "mkw/loading_screen.hpp"
#include "mkw/mii_body.hpp"
#include "mkw/menu_mii_materials.hpp"
#include "mkw/render_metrics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace mkw {
namespace {
constexpr float EyeDistance=1000.f, Fov=45.f, ModelDepth=EyeDistance+15.f;
enum Kind {Driver,Background,Machine};
// Resident entries per kind: browsing neighbours stay warm, LINEAR stays bounded.
constexpr unsigned Capacity[]={6,2,12};
constexpr std::uint32_t LinearReserve=3u<<20;
bool machinePage(UiPage page) {
    switch(page){
    case UiPage::Vehicle:case UiPage::Drift:case UiPage::DriftHelp:case UiPage::Cup:case UiPage::Course:
    case UiPage::BattleCup:case UiPage::BattleStage:case UiPage::Confirm:return true;
    default:return false;
    }
}
bool moviePreviewPage(UiPage page) {
    switch(page){
    case UiPage::Cup:case UiPage::Course:case UiPage::BattleCup:case UiPage::BattleStage:
    case UiPage::Confirm:return true;
    default:return false;
    }
}
// Native upper-LCD framing. Keep the retail control's transition displacement
// and timing, but move its resting point out of the old left-hand UI column.
// The model-space lift below leaves the name strip at Y190 clear of the tyres.
UiModelPlacement topModelPlacement(UiPage page,const UiTransitionView& motion) {
    return uiTopMenuModelPlacement(page,motion);
}
const char* seatedClip(unsigned weight,unsigned choice) {
    // DriverModelAnmCalculator::GetPrefix, original USA REL 0x8084e9f8.
    constexpr unsigned table[]={0,1,2,6,7,8,3,4,5,9,10,11};
    const unsigned vehicle=table[choice]*3+weight;
    if(vehicle<18)return "kart-wait";
    if(weight==0)return "sdf_bike-wait";
    if(weight==1)return vehicle==22||vehicle==31||vehicle==34?"ma_bike-wait":"mdf_bike-wait";
    return vehicle==26||vehicle==29||vehicle==35?"lb_bike-wait":"ldf_bike-wait";
}
struct Wanted {int driver=-1,machine=-1,background=-1;};
Wanted wanted(const MenuState& state){
    Wanted w;
    w.driver=state.page==UiPage::Character?int(state.characterDriver(state.cursor)):
             machinePage(state.page)?int(state.selection.driver):-1;
    const unsigned choice=state.page==UiPage::Vehicle?state.vehicleChoice(state.cursor):state.selection.vehicle;
    w.machine=machinePage(state.page)&&w.driver>=0&&w.driver<24&&choice<12?w.driver*12+int(choice):-1;
    if((w.driver>=24&&(w.driver>=26||(state.page!=UiPage::Character&&!state.selection.miiSelected)))||(w.driver>=0&&!state.driverUnlocked(unsigned(w.driver)))||(state.page==UiPage::Vehicle&&!state.vehicleUnlocked(choice))){w.driver=-1;w.machine=-1;}
    if(machinePage(state.page)&&w.driver>=24&&w.driver<26&&state.selection.miiSelected)w.machine=int(GarageChoices.size()+miiGarageOffset(state.selection.miiCharacter)+choice);
    if(w.driver>=24)w.driver=24+int(std::min(state.selection.license,3u))*2+(w.driver-24);
    w.background=uiBackdropModelIndex(state);
    if(state.selection.mode==UiMode::TimeTrials&&state.page==UiPage::Confirm){w.driver=-1;w.machine=-1;}
    return w;
}
void environment(Model& model,const std::vector<uint16_t>& vertices,float yaw) {
    if(vertices.empty())return;
    const float c=std::cos(yaw),s=std::sin(yaw);
    for(const auto index:vertices) {
        auto& vertex=model.vertices[index];
        const auto normal=normalized(vertex.normal);
        vertex.uv1[0]=.5f+.5f*(c*normal.x+s*normal.z);
        vertex.uv1[1]=.5f+.5f*normal.y;
    }
    flushGpuData(model.vertices,model.vertexCount*sizeof(Vertex));
}
// Unique vertices of the environment-mapped (reflective) submeshes.
std::vector<uint16_t> environmentVertices(const Model& model) {
    std::vector<bool> seen(model.vertexCount,false);std::vector<uint16_t> result;
    for(const auto& range:model.submeshes) {
        if(!(range.flags&(1u<<25))||((range.flags>>12)&3)!=3)continue;
        for(unsigned i=range.first;i<range.first+range.count;++i){const auto v=model.indices[i];if(!seen[v]){seen[v]=true;result.push_back(v);}}
    }
    return result;
}
struct ModelJob final:AssetJob {
    int kind,id;std::string modelPath,animationPath;
    NativeMii identity{};MiiBodySpec body{};
    std::unique_ptr<Model> model;std::unique_ptr<DriverAnimation> animation;std::vector<uint16_t> environment;
    ModelJob(int k,int i,const NativeMii& native={}):kind(k),id(i),identity(native){
        label=k==Driver?"driver model":k==Background?"background model":"machine model";
        char path[96];
        if(k==Driver){std::snprintf(path,sizeof path,"models/ui_driver_%d_000.gmdl",i);modelPath=path;std::snprintf(path,sizeof path,"models/ui_driver_%d.dskm",i);animationPath=path;}
        if(k==Background){std::snprintf(path,sizeof path,"models/ui_back_%d_000.gmdl",i);modelPath=path;std::snprintf(path,sizeof path,"models/ui_back_%d.dskm",i);animationPath=path;}
        if(k==Machine){if(unsigned(i)<GarageChoices.size())std::snprintf(path,sizeof path,"models/ui_machine_%d_%u_000.gmdl",i/12,unsigned(i%12));else std::snprintf(path,sizeof path,"models/%smenu_000.gmdl",raceGarageChoice(unsigned(i)).prefix);modelPath=path;}
    }
    void run()override{
        Model::setLightingCourse(&MenuLightings[0]);
        const bool mii=kind==Driver&&id>=24;
        if(mii){
            MiiAppearance appearance;
            if(!decodeMiiAppearance(identity,appearance)||!miiBodySpec(appearance,unsigned(id-24)%2,body))return;
            char path[128];
            std::snprintf(path,sizeof path,"models/ui_mii_%s_000.gmdl",body.stem);modelPath=path;
            std::snprintf(path,sizeof path,"models/ui_mii_%s_seated.dskm",body.stem);animationPath=path;
        }
        if(cancelled())return;
        auto loaded=std::make_unique<Model>();if(!loaded->load(modelPath.c_str(),kind==Background))return;
        if(!animationPath.empty()){
            auto bank=std::make_unique<DriverAnimation>();
            if(bank->load(animationPath.c_str(),*loaded)){
                bank->skinNormals(!loaded->staticLightingBaked);
                // A newly selected driver first shows sel_wait frame zero.
                if(kind==Driver)bank->apply(*loaded,bank->find("sel_wait"),0,0,bank->face("sel_wait"),0);
                animation=std::move(bank);
            }
        }
        if(mii){
            C3D_Mtx anchor;
            if(cancelled()||!animation||!animation->headTransform(anchor))return;
            // CFL expression masks need GPU baking. Publish the body first;
            // the main thread builds its native head between frames below.

        }
        // Rigid backgrounds (one bone per triangle) skin and environment-map
        // on the GPU; deforming ones keep the CPU path.
        if(kind==Background){
            const bool rigid=menuRigidModels&&animation&&animation->makeRigid(*loaded);
            if(!rigid&&!loaded->staticLightingBaked)environment=environmentVertices(*loaded);
        }
        model=std::move(loaded);
    }
};
}
struct MenuModels::Entry {
    int kind=-1,id=-1;
    std::shared_ptr<ModelJob> job;
    std::unique_ptr<NativeMiiHead> head;MiiBodySpec body{};
    std::unique_ptr<Model> model;std::unique_ptr<DriverAnimation> animation;
    std::vector<uint16_t> environment;
    std::uint32_t used=0,pinned=0;bool failed=false;
    AssetPriority priority=AssetPriority::Prefetch;bool visibleRequest=false;
    bool resident()const{return model!=nullptr;}
};
namespace {
bool publishModel(Renderer& renderer,MenuModels::Entry& entry,ModelJob& job){
    if(!job.model||job.cancelled())return false;
    std::unique_ptr<NativeMiiHead> head;
    if(job.kind==Driver&&job.id>=24){
        renderer.waitForGpu();LoadingScope loading;
        head=std::make_unique<NativeMiiHead>();
        if(!head->load(job.identity))return false;
        const auto rgba=job.body.clothingRGBA;
        const auto clothing=(rgba>>24)|((rgba>>8)&0xff00)|((rgba<<8)&0xff0000)|((rgba&255)<<24);
        for(const auto& role:MenuMiiMaterials){
            if(std::strcmp(role.stem,job.body.stem))continue;
            auto sub=std::find_if(job.model->submeshes.begin(),job.model->submeshes.end(),
                [&](const auto& v){return v.first==role.first&&v.count==role.count;});
            if(sub==job.model->submeshes.end()||!(sub->lighting&retail::AlbedoTint))return false;
            sub->constant=role.skin?head->skinConstant():clothing;
        }
    }
    entry.head=std::move(head);entry.body=job.body;entry.model=std::move(job.model);
    entry.animation=std::move(job.animation);entry.environment=std::move(job.environment);return true;
}
}
MenuModels::MenuModels()=default;
MenuModels::~MenuModels()=default;
void MenuModels::setIdentity(Renderer& renderer,unsigned slot,const NativeMii& identity){
    if(slot>=identities_.size()||identities_[slot]==identity)return;
    identities_[slot]=identity;
    for(auto it=entries_.begin();it!=entries_.end();){
        auto& entry=**it;
        if(entry.kind!=Driver||entry.id<24||unsigned(entry.id-24)/2!=slot){++it;continue;}
        if(entry.job)entry.job->cancel();
        if(shownDriver_==entry.id){shownDriver_=-1;driverClip_=driverFace_=-1;driverFrame_=0;}
        renderer.retire(std::move(entry.model));renderer.retire(std::move(entry.head));
        it=entries_.erase(it);
    }
}
MenuModels::Entry* MenuModels::find(int kind,int id){for(auto& e:entries_)if(e->kind==kind&&e->id==id)return e.get();return nullptr;}
const MenuModels::Entry* MenuModels::find(int kind,int id)const{for(const auto& e:entries_)if(e->kind==kind&&e->id==id)return e.get();return nullptr;}
MenuModels::Entry& MenuModels::obtain(int kind,int id){
    if(auto* e=find(kind,id))return *e;
    auto entry=std::make_unique<Entry>();entry->kind=kind;entry->id=id;entries_.push_back(std::move(entry));return *entries_.back();
}
void MenuModels::request(const MenuState& state,AssetPriority priority){
    if(state.page==UiPage::Unlock){request(unlockModelState(state),priority);return;}
    const auto w=wanted(state);
    const std::pair<int,int> items[]={{Driver,w.driver},{Background,w.background},{Machine,w.machine}};
    for(const auto& [kind,id]:items){
        if(id<0)continue;
        auto& e=obtain(kind,id);e.used=clock_;if(priority!=AssetPriority::Prefetch)e.pinned=clock_;
        if(kind==Background&&priority!=AssetPriority::Prefetch){
            // A failed speculative/previous-visit load must not permanently
            // poison this mode. Retry once on each new visible selection.
            for(auto& old:entries_)if(old->kind==Background&&old.get()!=&e)old->visibleRequest=false;
            if(!e.visibleRequest){
                e.visibleRequest=true;e.failed=false;
                if(e.job&&e.job->done()&&!e.job->model)e.job.reset();
            }
        }
        if(e.resident()||e.failed)continue;
        if(!e.job||(e.job->done()&&e.job->cancelled())){
            e.job=std::make_shared<ModelJob>(kind,id,kind==Driver&&id>=24?identities_[unsigned(id-24)/2]:NativeMii{});e.priority=priority;
        }else if(int(priority)<int(e.priority))e.priority=priority;
        // Background dispatch waits until update has trimmed/drained old
        // models; allocating first could fail before the LRU gets a turn.
        if(kind!=Background&&!e.job->done())AssetLoader::instance().submit(e.job,priority);
    }
}
bool MenuModels::ready(const MenuState& state)const{
    if(state.page==UiPage::Unlock)return ready(unlockModelState(state));
    const auto w=wanted(state);
    const std::pair<int,int> items[]={{Driver,w.driver},{Background,w.background},{Machine,w.machine}};
    for(const auto& [kind,id]:items){
        if(id<0)continue;
        const auto* e=find(kind,id);
        if(!e||(!e->resident()&&!e->failed))return false;
    }
    return true;
}
void MenuModels::show(Renderer&,int kind,int id){
    auto* e=id>=0?find(kind,id):nullptr;
    if(kind==Driver&&shownDriver_!=id){
        shownDriver_=id;driverFrame_=0;driverClip_=-1;driverFace_=-1;
        if(e&&e->animation){e->animation->clearPoseTransition();driverClip_=e->animation->find("sel_wait");driverFace_=e->animation->face("sel_wait");}
    }
    if(kind==Background&&shownBackground_!=id){
        shownBackground_=id;backgroundFrame_=0;backgroundClip_=e&&e->animation?e->animation->find("loop"):-1;
    }
    if(kind==Machine&&shownMachine_!=id){shownMachine_=id;}
}
float MenuModels::confirmationFrames()const{
    const auto* driver=shownDriver_>=0?find(Driver,shownDriver_):nullptr;
    if(!driver||!driver->animation)return 0;
    const int clip=driver->animation->find("sel_gut");
    return clip>=0?driver->animation->duration(clip):0;
}
void MenuModels::advance(unsigned ticks){
    backgroundFrame_+=ticks;driverFrame_+=ticks;
    machineRotation_.advance(ticks);
}
void MenuModels::update(Renderer& renderer,const MenuState& state,const UiTransitionView& motion){
    const auto visual=state.page==UiPage::Unlock?unlockModelState(state):state;
    const bool confirm=visual.page==UiPage::Vehicle&&motion.active&&!motion.entering&&!motion.back&&motion.to==UiPage::Drift;
    machineRotation_.update(visual.page==UiPage::Vehicle&&!confirm,confirm);
    for(auto& e:entries_)if(e->job&&e->job->done()){
        auto job=std::move(e->job);
        if(!publishModel(renderer,*e,*job)&&!job->cancelled())e->failed=true;
    }
    const auto w=wanted(visual);
    // Show the wanted model once resident; meanwhile keep the previous one.
    auto choose=[&](int kind,int id,int shown){
        if(id<0){show(renderer,kind,-1);return;}
        const auto* e=find(kind,id);
        if(e&&e->resident())show(renderer,kind,id);
        else if(e&&e->failed)show(renderer,kind,-1);
        else if(shown>=0&&!find(kind,shown))show(renderer,kind,-1);
    };
    choose(Background,w.background,shownBackground_);
    choose(Machine,w.machine,shownMachine_);
    // The seated driver must be the one on the displayed machine.
    choose(Driver,shownMachine_>=0&&unsigned(shownMachine_)<GarageChoices.size()?shownMachine_/12:w.driver,shownDriver_);
    if(auto* driver=shownDriver_>=0?find(Driver,shownDriver_):nullptr;driver&&driver->animation){
        // CharacterSelect::OnButtonDriverClick switches the displayed driver
        // to state 1 (sel_gut) while the confirmed button holds before exit.
        // Unlock::OnButtonClick USA80859070 uses the same state for driver
        // rewards. Vehicle state 3 retains the seated animation (8084cacc).
        const bool confirmed=visual.page==UiPage::Character&&motion.active&&!motion.entering&&!motion.back&&(motion.to==UiPage::Vehicle||motion.to==UiPage::TeamConfirm||state.page==UiPage::Unlock);
        const char* name=shownMachine_>=0?seatedClip(unsigned(shownMachine_)<GarageChoices.size()?UiCharacters[shownMachine_/12].weight:driver->body.weightClass,unsigned(shownMachine_%12)):confirmed?"sel_gut":"sel_wait";
        const int clip=driver->animation->find(name);
        if(clip!=driverClip_){driver->animation->clearPoseTransition();driverFrame_=0;}
        driverClip_=clip;
        driverFace_=driver->animation->face(name);
    }
    for(auto& e:entries_){
        const bool shown=(e->kind==Driver&&e->id==shownDriver_)||(e->kind==Background&&e->id==shownBackground_)||(e->kind==Machine&&e->id==shownMachine_);
        if(shown)e->used=clock_;
    }
    // Evict least recently used, never what is shown or pinned this frame.
    auto evictable=[&](const Entry& e){
        const bool shown=(e.kind==Driver&&e.id==shownDriver_)||(e.kind==Background&&e.id==shownBackground_)||(e.kind==Machine&&e.id==shownMachine_);
        return !shown&&e.pinned!=clock_;
    };
    std::uint32_t retiring=0;
    for(;;){
        unsigned counts[3]{};for(const auto& e:entries_)if(e->resident()||e->job)++counts[e->kind];
        Entry* victim=nullptr;
        const bool low=linearSpaceFree()+retiring<LinearReserve;
        for(auto& e:entries_){
            if(!evictable(*e)||!(e->resident()||e->job))continue;
            if(!low&&counts[e->kind]<=Capacity[e->kind])continue;
            if(!victim||e->used<victim->used)victim=e.get();
        }
        if(!victim)break;
        if(victim->job){victim->job->cancel();victim->job.reset();}
        if(victim->model)retiring+=victim->model->vertexCount*sizeof(Vertex);
        renderer.retire(std::move(victim->head));renderer.retire(std::move(victim->model));victim->animation.reset();victim->environment.clear();
    }
    entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[&](const auto& e){return !e->resident()&&!e->job&&!e->failed&&evictable(*e);}),entries_.end());
    const bool backgroundPending=std::any_of(entries_.begin(),entries_.end(),[](const auto& e){return e->kind==Background&&e->job&&!e->job->done();});
    // Retirement is GPU-safe and deferred. Make its memory available before
    // starting a replacement, only when this update actually evicted models.
    if(backgroundPending&&retiring)renderer.waitForGpu();
    for(auto& e:entries_)if(e->kind==Background&&e->job&&!e->job->done())
        AssetLoader::instance().submit(e->job,e->priority);
    ++clock_;
}
void MenuModels::prepare(Renderer& renderer,const MenuState& state){
    request(state,AssetPriority::Urgent);
    // QA path: run this state's jobs on the calling thread.
    for(auto& e:entries_)if(e->job&&!e->job->done()){e->job->cancel();auto job=std::make_shared<ModelJob>(e->kind,e->id,e->kind==Driver&&e->id>=24?identities_[unsigned(e->id-24)/2]:NativeMii{});job->run();
        if(!publishModel(renderer,*e,*job))e->failed=true;
        e->job.reset();
    }
    update(renderer,state);
}
void MenuModels::reset(Renderer& renderer) {
    for(auto& e:entries_){if(e->job)e->job->cancel();renderer.retire(std::move(e->head));renderer.retire(std::move(e->model));}
    entries_.clear();shownDriver_=shownBackground_=shownMachine_=-1;driverClip_=driverFace_=backgroundClip_=-1;
    machineRotation_={};
}
void MenuModels::begin(Renderer& renderer) {
    // Original EGG menu frustum: near10/far100000, not race-scale W depth.
    renderer.setFov(Fov,10.f,100000.f);renderer.setCourseLighting(false);
    renderer.setRetailLighting(&MenuLightings[0]);
    // Stereo: Mario Kart 7's garage, the selection models 7 px behind the
    // screen (stereo::Garage). They stand at modelZ -15 (see draw).
    renderer.setStereoSubject(stereo::Garage,ModelDepth);
    renderer.begin({0,0,EyeDistance},{0,0,0},{0,1,0});
}
float MenuModels::backdropParallax(const Renderer& renderer){
    return renderer.stereoParallax(ModelDepth*stereo::GarageBackdropRatio);
}
void MenuModels::draw(Renderer& renderer,const MenuState& state,const UiTransitionView& motion) {
    if(state.page==UiPage::Unlock){auto model=unlockModelState(state);if(model.page==UiPage::Vehicle)model.page=UiPage::Drift;draw(renderer,model,motion);return;}
    if((state.page==UiPage::Character&&!state.driverUnlocked(state.characterDriver(state.cursor)))||
       (state.page==UiPage::Vehicle&&!state.vehicleUnlocked(state.vehicleChoice(state.cursor))))return;
    // The upper screen is the course movie/details area on these pages. The
    // ordinary 2D backdrop still draws; a retained kart or VS flag must not
    // cover the movie. Keeping the resident models also makes Back immediate.
    const bool timeTrialRecord=uiTimeTrialRecordBackdrop(state);
    const bool randomConfirm=uiRandomCourseConfirmPreview(state);
    if(moviePreviewPage(state.page)&&!timeTrialRecord&&!randomConfirm)return;
    if(randomConfirm&&(!motion.active||!uiRandomCoursePreviewMotion(motion)))return;
    // Retail could place the decorative trophy/flag beside the left-hand
    // character control. The reflowed foreground now occupies that space;
    // keep only the 2D backdrop on model-preview pages so their shared depth
    // buffer cannot put the trophy in front of the selected driver or kart.
    const bool foregroundModel=!timeTrialRecord&&(state.page==UiPage::Character||machinePage(state.page));
    if(auto* background=shownBackground_>=0?find(Background,shownBackground_):nullptr;
       !foregroundModel&&background&&background->resident()) {
        // BackModelMgr::Update's original rates (USA808d5f18) continue across
        // selection pages; a page's age is not the model's animation clock.
        constexpr float rates[]={.5f,.4f,.25f,.5f};
        auto t0=svcGetSystemTick();
        const bool rigid=background->animation&&background->animation->rigid();
        RigidPose pose;
        if(rigid){
            pose.parts=&background->animation->rigidParts();
            pose.palette=background->animation->pose(backgroundClip_,float(backgroundFrame_)*rates[shownBackground_],backgroundFrame_);
        }else if(background->animation)background->animation->apply(*background->model,backgroundClip_,float(backgroundFrame_)*rates[shownBackground_],backgroundFrame_);
        auto t1=svcGetSystemTick();renderMetrics.skinMs+=float((t1-t0)*1000.0/SYSCLOCK_ARM11);
        if(!rigid)environment(*background->model,background->environment,0.f);
        auto t2=svcGetSystemTick();renderMetrics.environmentMs+=float((t2-t1)*1000.0/SYSCLOCK_ARM11);
        // Original full 608x480 background render target is displayed in the
        // 608x456 aspect-adjusted pane at BRCTR X80. Z's original BSS table is
        // all zero: its only two REL relocations address the read in Update.
        constexpr float scales[]={.1f,.14f,.19f,.11f};
        constexpr float xs[]={0,0,-9,-1},ys[]={-3.4f,-5.1f,-9,-2.5f};
        constexpr float unit=EyeDistance/15.f;
        C3D_Mtx matrix;Mtx_Identity(&matrix);
        Mtx_Translate(&matrix,xs[shownBackground_]*unit,ys[shownBackground_]*unit,0,true);
        const float scale=scales[shownBackground_]*unit;Mtx_Scale(&matrix,scale,scale,scale);
        if(!rigid||pose.palette){
            if(rigid)renderer.setRigidPose(&pose);
            // Controls now live on the lower LCD, so the original 80-unit
            // right offset is no longer needed for mode/trophy backgrounds.
            const auto placement=uiBackdropModelPlacement(unsigned(shownBackground_),motion);
            renderer.drawAtScreen(*background->model,matrix,placement.x,placement.y,
                                 100.f/255.f,2.f*400.f/832.f,display::Height/240.f);
            renderer.setRigidPose(nullptr);
        }
        renderMetrics.drawMs+=float((svcGetSystemTick()-t2)*1000.0/SYSCLOCK_ARM11);
    }
    if(timeTrialRecord)return;
    auto* driver=shownDriver_>=0?find(Driver,shownDriver_):nullptr;
    if(!driver||!driver->resident())return;
    {const auto t0=svcGetSystemTick();
    if(driver->animation)driver->animation->apply(*driver->model,driverClip_,float(driverFrame_),driverFrame_,driverFace_,float(driverFrame_));
    renderMetrics.skinMs+=float((svcGetSystemTick()-t0)*1000.0/SYSCLOCK_ARM11);}
    auto* machine=shownMachine_>=0?find(Machine,shownMachine_):nullptr;
    if(machine&&machine->resident()) {
        const unsigned driverId=unsigned(shownMachine_)/12,choice=unsigned(shownMachine_)%12;
        // Original centered 480-high framebuffer is cropped to a 256-square
        // image, then placed by model_set_p. Its AspectAdjust flag reduces
        // horizontal scale to 608/832 before the 608x456 UI reaches the LCD.
        C3D_Mtx matrix;Mtx_Identity(&matrix);
        const auto placement=topModelPlacement(state.page,motion);
        const float x=placement.x,y=placement.y;
        constexpr unsigned ids[]={0,1,2,6,7,8,3,4,5,9,10,11};
        const bool mii=unsigned(shownMachine_)>=GarageChoices.size();
        const unsigned weight=mii?driver->body.weightClass:UiCharacters[driverId].weight,vehicle=ids[choice]*3+weight;
        const float scale=vehicle==5?1.03f:weight==0?1.3f:weight==1?1.2f:1.1f;
        // EyeZ1000 plus modelZ-15 is the native eyeZ15/modelZ-1000 pair.
        Mtx_Translate(&matrix,0.f,-135.f,-15.f,true);
        // Retail Update builds Euler Y=(90+rotation)*2*pi/720.
        Mtx_RotateY(&matrix,(90.f+machineRotation_.angle)*2.f*3.141592741f/720.f,true);
        Mtx_Scale(&matrix,scale,scale,scale);
        renderer.drawAtScreen(*machine->model,matrix,x,y,1.f,2.f*400.f/832.f*placement.scale,2.f*display::MenuScaleY*placement.scale);
        const auto seat=mii?MenuSeat{MiiMenuSeats[unsigned(shownMachine_)-GarageChoices.size()][0],MiiMenuSeats[unsigned(shownMachine_)-GarageChoices.size()][1]}:MenuSeats[driverId][choice];Mtx_Translate(&matrix,0,seat.y,seat.z,true);
        renderer.drawAtScreen(*driver->model,matrix,x,y,1.f,2.f*400.f/832.f*placement.scale,2.f*display::MenuScaleY*placement.scale);
        if(driver->head&&driver->animation){
            C3D_Mtx joint,local,attached,world;
            if(driver->animation->headTransform(joint)){
                Mtx_Identity(&local);Mtx_Translate(&local,driver->body.headX,driver->body.headY,0,true);
                Mtx_RotateZ(&local,3.141592741f,true);Mtx_RotateY(&local,-1.570796371f,true);
                Mtx_Scale(&local,driver->body.headScale,driver->body.headScale,driver->body.headScale);
                Mtx_Multiply(&attached,&joint,&local);Mtx_Multiply(&world,&matrix,&attached);
                driver->head->drawAtScreen(renderer,world,x,y,2.f*400.f/832.f*placement.scale,2.f*display::MenuScaleY*placement.scale);
            }
        }
    }else if(state.page==UiPage::Character) {
        C3D_Mtx matrix;Mtx_Identity(&matrix);
        Mtx_Translate(&matrix,0.f,-130.f,-15.f,true);Mtx_Scale(&matrix,1.6f,1.6f,1.6f);
        const auto placement=topModelPlacement(state.page,motion);
        renderer.drawAtScreen(*driver->model,matrix,placement.x,placement.y,1.f,
                              2.f*400.f/832.f*placement.scale,2.f*display::MenuScaleY*placement.scale);
        if(driver->head&&driver->animation){
            C3D_Mtx joint,local,attached,world;
            if(driver->animation->headTransform(joint)){
                Mtx_Identity(&local);
                Mtx_Translate(&local,driver->body.headX,driver->body.headY,0,true);
                Mtx_RotateZ(&local,3.1415927410125732f,true);
                Mtx_RotateY(&local,-1.5707963705062866f,true);
                const float scale=driver->body.headScale;Mtx_Scale(&local,scale,scale,scale);
                Mtx_Multiply(&attached,&joint,&local);Mtx_Multiply(&world,&matrix,&attached);
                const bool confirming=motion.active&&!motion.entering&&!motion.back&&
                    (motion.to==UiPage::Vehicle||motion.to==UiPage::TeamConfirm);
                const auto expression=confirming?NativeMiiHead::Smile:
                    driverFrame_%180>=2&&driverFrame_%180<6?NativeMiiHead::Blink:NativeMiiHead::Normal;
                driver->head->drawAtScreen(renderer,world,placement.x,placement.y,
                    2.f*400.f/832.f*placement.scale,2.f*display::MenuScaleY*placement.scale,expression);
            }
        }
    }
}
}
