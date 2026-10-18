#include "mkw/course_objects.hpp"
#include "mkw/course_object_kinds.hpp"
#include "mkw/render_quality.hpp"
#include "mkw/escalator_landing.hpp"
#include "mkw/retail_lighting.hpp"
#include <mkw/render_metrics.hpp>
#include <mkw/job_worker.hpp>
#include <game/field/ObjectDirector.hh>
#include <game/field/ObjectDrivableDirector.hh>
#include <game/field/obj/ObjectEscalator.hh>
#include <game/field/obj/ObjectHeyhoBall.hh>
#include <game/field/obj/ObjectKoopaBall.hh>
#include <mkw/race_effects.hpp>
#include <game/system/CourseMap.hh>
#include <game/system/RaceManager.hh>
#include <egg/math/Matrix.hh>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace mkw {
namespace K=Kinoko;
C3D_Mtx objectMatrix(const K::EGG::Matrix34f& source,const K::EGG::Vector3f& scale){
    C3D_Mtx m;Mtx_Identity(&m);
    const float factors[]={scale.x,scale.y,scale.z,1.f};
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)
        m.r[row].c[3-col]=source[row,col]*factors[col];
    return m;
}
C3D_Mtx placementMatrix(const K::System::MapdataGeoObj& geo){
    K::EGG::Matrix34f matrix;
    matrix.makeRT(geo.rot()*(3.14159265358979323846f/180.f),geo.pos());
    return objectMatrix(matrix,geo.scale());
}
C3D_Mtx basisMatrix(Vec3 x,Vec3 y,Vec3 z,Vec3 position,Vec3 scale){
    C3D_Mtx m;Mtx_Identity(&m);
    const Vec3 axes[]={x*scale.x,y*scale.y,z*scale.z};
    for(unsigned col=0;col<3;++col){m.r[0].c[3-col]=axes[col].x;m.r[1].c[3-col]=axes[col].y;m.r[2].c[3-col]=axes[col].z;}
    m.r[0].w=position.x;m.r[1].w=position.y;m.r[2].w=position.z;
    return m;
}
namespace {
constexpr size_t SkinReserve=2u*1024*1024;
std::unique_ptr<Model> cloneModel(const Model& source){
    auto copy=std::make_unique<Model>();
    copy->vertices=static_cast<Vertex*>(linearAlloc(source.vertexCount*sizeof(Vertex)));
    copy->indices=static_cast<uint16_t*>(linearAlloc(source.indexCount*sizeof(uint16_t)));
    if(!copy->vertices||!copy->indices)return nullptr;
    copy->vertexCount=source.vertexCount;copy->indexCount=source.indexCount;
    std::memcpy(copy->vertices,source.vertices,source.vertexCount*sizeof(Vertex));
    std::memcpy(copy->indices,source.indices,source.indexCount*sizeof(uint16_t));
    copy->submeshes=source.submeshes;copy->textures=source.textures;
    copy->minimum=source.minimum;copy->maximum=source.maximum;
    flushGpuData(copy->indices,copy->indexCount*sizeof(uint16_t));
    return copy;
}
bool endsWith(const std::string& s,const char* suffix){const size_t n=std::strlen(suffix);return s.size()>=n&&!s.compare(s.size()-n,n,suffix);}
// Coconut Mall escalator treads at their Kinoko step phase.
struct EscalatorWrite {Model* mesh;const Vertex* base;float phase;};
void writeEscalator(void* payload){
    const auto& w=*static_cast<const EscalatorWrite*>(payload);auto& mesh=*w.mesh;
    // Only Y and Z ever change: every other field keeps its loaded (base) value.
    for(unsigned i=0;i<mesh.vertexCount;++i){
        const auto p=escalatorLanding(w.base[i].position.y,w.base[i].position.z,w.phase);
        mesh.vertices[i].position.y=p.y;mesh.vertices[i].position.z=p.z;
    }
    flushGpuData(mesh.vertices,mesh.vertexCount*sizeof(Vertex));
}
// One CPU-skinned instance copy (its palette follows the record).
struct SkinWrite {const ObjectBank* bank;Model* model;bool normals;};
void writeSkin(void* payload){
    const auto& w=*static_cast<const SkinWrite*>(payload);
    w.bank->skin(reinterpret_cast<const float*>(&w+1),*w.model,w.normals);
}
}

ObjectAssets::~ObjectAssets()=default;
ObjModel* ObjectAssets::find(const char* resource,const char* name) const {
    if(!resource||!name)return nullptr;
    for(const auto& m:models)if(m->resource==resource&&m->name==name)return m.get();
    return nullptr;
}
ObjModel* ObjectAssets::main(const char* resource) const {
    if(auto* m=find(resource,resource))return m;
    for(const auto& m:models)if(m->resource==resource&&!endsWith(m->name,"-low")&&!endsWith(m->name,"-shadow"))return m.get();
    return nullptr;
}
ObjModel* ObjectAssets::low(const ObjModel* model) const {return model?find(model->resource.c_str(),(model->name+"-low").c_str()):nullptr;}
ObjModel* ObjectAssets::shadow(const ObjModel* model) const {return model?find(model->resource.c_str(),(model->name+"-shadow").c_str()):nullptr;}
Tracks ObjectAssets::tracks(const ObjModel* model,const char* name) const {
    Tracks t;t.model=const_cast<ObjModel*>(model);
    if(!model||!model->bank)return t;
    const auto& b=*model->bank;
    const char* names[]={name,model->name.c_str(),model->resource.c_str()};
    for(const char* n:names){
        if(!n)continue;
        if(t.clip<0)t.clip=b.clip(n);
        if(t.srt<0)t.srt=b.scroll(n);
        if(t.pat<0)t.pat=b.pattern(n);
        if(t.clr<0)t.clr=b.color(n);
    }
    return t;
}
bool ObjectAssets::load(int course){
    char path[64];std::snprintf(path,sizeof path,"models/course_%02d_visuals.list",course);
    FILE* list=std::fopen(path,"r");if(!list)return true;
    char resource[64],name[64],mesh[96],bankName[96];bool ok=true;
    while(std::fscanf(list,"%63s %63s %95s %95s",resource,name,mesh,bankName)==4){
        if(std::strchr(mesh,'/')||std::strstr(mesh,"..")||std::strchr(bankName,'/')||std::strstr(bankName,"..")){ok=false;break;}
        auto m=std::make_unique<ObjModel>();m->resource=resource;m->name=name;m->model=std::make_unique<Model>();
        char file[128];std::snprintf(file,sizeof file,"models/%s.gmdl",mesh);
        if(!m->model->load(file)){ok=false;break;}
        m->bounds={m->model->minimum,m->model->maximum};
        if(std::strcmp(bankName,"-")){
            m->bank=std::make_unique<ObjectBank>();std::snprintf(file,sizeof file,"models/%s.dobj",bankName);
            if(!m->bank->load(file)){ok=false;break;}
            m->bank->markRegisterAlpha(*m->model);
            Bounds animated;if(m->bank->animatedBounds(animated)){m->bounds.include(animated.minimum);m->bounds.include(animated.maximum);}
            if(m->bank->skinned()){
                // One bone per triangle: share the mesh and skin on the GPU.
                // Weighted meshes are CPU-skinned into per-instance copies.
                if(m->bank->makeRigid(*m->model))m->rigid=true;
                else if((m->cpuSkin=m->bank->weighted())){
                    m->crowd=!std::strncmp(resource,"group_",6)||!std::strncmp(resource,"monte_",6);
                    for(const auto& s:m->model->submeshes)
                        m->litNormals|=(s.lighting&retail::Retail)&&((s.lighting&retail::ChannelMask)>>retail::ChannelShift)!=0;
                    // Unlit objects never read skinned normals: skin on the GPU
                    // (one shared mesh, any pose per instance), else positions only.
                    if(!m->litNormals&&m->bank->gpuSkin(*m->model)){m->gpuSkin=true;m->cpuSkin=false;}
                    else if(!m->litNormals)m->bank->shareSkinnedPositions();
                    else m->bank->shareSkinnedVertices();
                    // Its first instance copy now, while LINEAR is free: a model posed
                    // procedurally every frame (the Maple Treeway net) found none
                    // mid-race under SkinReserve and was never drawn.
                    if(m->cpuSkin)if(auto fresh=cloneModel(*m->model)){m->skins.push_back(std::move(fresh));m->skinKeys.push_back({-2,-1.f});}
                }
            }
            if(m->bank->shapeCount()==m->model->submeshes.size())m->overrides.resize(m->model->submeshes.size());
        }
        models.push_back(std::move(m));
    }
    std::fclose(list);
    if(!ok){models.clear();return false;}
    if(find("koopaBall","koopaBall")||find("HeyhoBallGBA","HeyhoBallGBA")||find("K_bomb_car","K_bomb_car")){
        explosion=std::make_unique<Model>();explosionBank=std::make_unique<DriverAnimation>();
        if(!explosion->load("models/bomb_core.gmdl")||!explosionBank->load("models/bomb_core.dskm",*explosion)){explosion.reset();explosionBank.reset();}
    }
    return true;
}
void ObjectAssets::beginFrame(){for(auto& m:models)m->skinUsed=0;}

struct CourseObjects::Impl {
    std::function<void(Renderer&,const C3D_Mtx&,unsigned)> blastDrawer;
    std::unique_ptr<Model> palm;
    std::array<std::unique_ptr<Model>,6> escalators;
    std::vector<Vertex> escalatorBase;
    std::array<bool,6> escalatorWritten{};
    std::vector<C3D_Mtx> palms;
    int course=-1;
    ObjectAssets assets;
    std::vector<Visual> visuals,suspendedVisuals;
    bool graphicsSuspended=false;
    bool linked=false;
    const void* director=nullptr;
    uint32_t lastTimer=0;
    std::vector<float> scratch;
    std::vector<Renderer::ShadowVolume> shadows,skyShadows;
    void link();
    // Re-links after a restarted race builds a new Kinoko scene.
    bool ready();
    // One frame's recorded sky pass and object pass (record), replayed in order.
    ObjectDrawList sky,objects,water;
    ViewFrustum view{};
    unsigned tick=0;
    Vec3 camera{};
    bool linkedReady=false,recorded=false,queued=false;
    // The sky pass and the object pass are separate jobs, run in that order:
    // drawSky waits only for the sky, and the objects record during the track.
    Job skyJob,objectJob;
    void recordSky();
    void recordObjects();
    void begin(const Renderer& renderer,unsigned frameTick,Vec3 frameCamera);
    void replay(Renderer& renderer,const ObjectDrawList& list);
};
CourseObjects::CourseObjects():impl_(std::make_unique<Impl>()){}
CourseObjects::~CourseObjects()=default;
unsigned CourseObjects::palmCount() const {return impl_->palms.size();}
unsigned CourseObjects::moveTexturesToVram(Renderer& renderer,std::size_t reserve){
    unsigned moved=0;
    auto move=[&](const Model* model){if(model)for(const auto& t:model->textures)if(t)moved+=renderer.moveToVram(*t,reserve);};
    for(const auto& m:impl_->assets.models)move(m->model.get());
    move(impl_->palm.get());
    return moved;
}
unsigned CourseObjects::modelCount() const {return impl_->assets.models.size();}
void CourseObjects::reset(){
    auto& d=*impl_;
    d.assets.explosion.reset();d.assets.explosionBank.reset();
    d.palms.clear();d.palm.reset();for(auto& mesh:d.escalators)mesh.reset();d.escalatorBase.clear();d.escalatorWritten={};
    d.suspendedVisuals.clear();d.graphicsSuspended=false;
    d.visuals.clear();d.assets.models.clear();d.course=-1;d.linked=false;d.director=nullptr;
}
void CourseObjects::suspendGraphics(){
    auto& d=*impl_;if(d.graphicsSuspended)return;
    jobWorker().finishAll();
    if(!d.linked)d.link();
    d.suspendedVisuals=std::move(d.visuals);
    d.sky.clear();d.objects.clear();d.water.clear();d.shadows.clear();d.skyShadows.clear();
    d.assets.explosion.reset();d.assets.explosionBank.reset();d.assets.models.clear();
    d.palm.reset();for(auto& mesh:d.escalators)mesh.reset();d.escalatorBase.clear();
    d.linked=false;d.linkedReady=false;d.recorded=false;d.queued=false;d.graphicsSuspended=true;
}
bool CourseObjects::resumeGraphics(){
    auto& d=*impl_;if(!d.graphicsSuspended)return true;
    d.assets.models.clear();d.palm.reset();for(auto& mesh:d.escalators)mesh.reset();
    if(d.course==5&&!loadCoconut())return false;
    if(!d.assets.load(d.course))return false;
    d.link();
    // Rebind only graphics pointers; preserve route followers, deforming-object
    // state, sampled animation ages and observed native states from the freeze.
    for(auto& v:d.visuals)for(auto& saved:d.suspendedVisuals)if(saved.index==v.index){
        const auto main=v.main,low=v.low,shadow=v.shadow;const auto extra=v.extra;
        const auto clips=v.clips;const auto present=v.present;
        v=std::move(saved);v.main=main;v.low=low;v.shadow=shadow;v.extra=extra;
        v.clips=clips;v.present=present;break;
    }
    d.suspendedVisuals.clear();d.graphicsSuspended=false;d.recorded=false;return true;
}
void CourseObjects::invalidate(){impl_->linked=false;impl_->visuals.clear();}
bool CourseObjects::loadCoconut(){
    auto& d=*impl_;
    d.palm=std::make_unique<Model>();
    if(!d.palm->load("models/coconut_palm_000.gmdl")){d.palm.reset();return false;}
    d.palm->enableStaticCulling();
    // Each escalator has independent GPU vertices until the frame completes.
    // Model's weak texture cache shares the original texture across all six.
    for(auto& mesh:d.escalators){
        mesh=std::make_unique<Model>();
        if(!mesh->load("models/coconut_escalator_000.gmdl")){for(auto& m:d.escalators)m.reset();d.palm.reset();return false;}
    }
    d.escalatorBase.assign(d.escalators[0]->vertices,d.escalators[0]->vertices+d.escalators[0]->vertexCount);
    d.escalatorWritten={};
    const auto* map=K::System::CourseMap::Instance();
    d.palms.clear();
    for(unsigned i=0;i<map->getGeoObjCount();++i){
        const auto* obj=map->getGeoObj(i);
        if(obj->id()!=unsigned(K::Field::ObjectId::PalmTree)||!(obj->presenceFlag()&1))continue;
        d.palms.push_back(placementMatrix(*obj));
    }
    return true;
}
bool CourseObjects::loadCourse(int courseId){
    auto& d=*impl_;
    d.visuals.clear();d.assets.models.clear();d.linked=false;d.course=courseId;
    return d.assets.load(courseId);
}
void CourseObjects::Impl::link(){
    visuals.clear();linked=true;
    const auto* map=K::System::CourseMap::Instance();
    const auto* objects=K::Field::ObjectDirector::Instance();
    director=objects;
    if(!map||!objects||assets.models.empty())return;
    // Kinoko constructs and loads an object's parts (fireballs, wiggler
    // segments, cow followers) before the object itself, so the last object
    // registered for a placement is the one created from it.
    std::unordered_map<const K::System::MapdataGeoObj*,std::vector<const K::Field::ObjectBase*>> owner;
    for(const auto* obj:objects->objects())if(obj->mapObj())owner[obj->mapObj()].push_back(obj);
    if(const auto* drivable=K::Field::ObjectDrivableDirector::Instance())
        for(const auto* obj:drivable->objects())if(obj->mapObj())owner[obj->mapObj()].push_back(obj);
    const auto& flow=objects->flowTable();
    for(unsigned i=0;i<map->getGeoObjCount();++i){
        const auto* geo=map->getGeoObj(i);
        if(!geo||!(geo->presenceFlag()&1))continue;
        const auto* set=flow.set(flow.slot(static_cast<K::Field::ObjectId>(geo->id())));
        if(!set)continue;
        Visual v;v.geo=geo;v.index=i;
        if(auto it=owner.find(geo);it!=owner.end()){v.parts=it->second;v.object=v.parts.back();}
        std::memcpy(v.name,set->name,sizeof v.name);v.name[sizeof v.name-1]=0;
        std::memcpy(v.resources,set->resources,sizeof v.resources);v.resources[sizeof v.resources-1]=0;
        std::strncpy(v.resource,v.resources,sizeof v.resource-1);
        if(char* semicolon=std::strchr(v.resource,';'))*semicolon=0;
        v.placement=placementMatrix(*geo);
        if(classifyObject(v,assets,course))visuals.push_back(v);
    }
    // Bowser's statue opens its mouth as its fireball launches.
    const K::Field::ObjectBase* fireball=nullptr;
    for(const auto& v:visuals)if(!std::strcmp(v.name,"koopaBall"))fireball=v.object;
    for(auto& v:visuals)if(!std::strcmp(v.name,"koopaFigure"))v.partner=fireball;
}
void CourseObjects::Impl::begin(const Renderer& renderer,unsigned frameTick,Vec3 frameCamera){
    // Linking reads and rebuilds visuals: always on this thread, before recording.
    view=renderer.viewFrustum();tick=frameTick;camera=frameCamera;linkedReady=ready();recorded=true;
}
// The sky pass precedes the object pass, which restarts the per-model
// CPU-skin copies, exactly as drawSky then draw ran them.
void CourseObjects::Impl::recordSky(){
    sky.clear();skyShadows.clear();
    if(!linkedReady)return;
    ObjectDraw context{view,assets,tick,camera,course,scratch,skyShadows,sky,true};
    for(auto& v:visuals)if(skyObject(v))drawObject(context,v);
}
void CourseObjects::Impl::recordObjects(){
    objects.clear();water.clear();
    if(palm)for(const auto& matrix:palms)if(!renderQuality().tooSmall({palm->minimum,palm->maximum},matrix,camera))objects.records.push_back({ObjectDrawRecord::Plain,false,0,palm.get(),nullptr,0,0,0,0,0,matrix});
    // Includes the children of paired escalators. Kinoko updates these transforms
    // after its 60 Hz step, including stops/reversals and the looping tread offset.
    // Query each frame so restart/map changes cannot leave stale scene pointers.
    if(escalators[0]){
        unsigned index=0;
        for(const auto* obj:K::Field::ObjectDrivableDirector::Instance()->objects()){
            if(obj->id()!=K::Field::ObjectId::Escalator)continue;
            if(index>=escalators.size())break;
            const unsigned slot=index;auto& mesh=*escalators[index++];
            const EscalatorWrite write{&mesh,escalatorBase.data(),static_cast<const K::Field::ObjectEscalator*>(obj)->stepFactor()};
            // Reduced quality: each tread mesh re-poses on alternate frames
            // (half of them each frame); its transform still moves every frame.
            const bool skip=renderQuality().animationStep&&escalatorWritten[slot]&&((tick/2+slot)&1);
            escalatorWritten[slot]=true;
            if(skip){}
            else if(auto* deferred=deferredWrites()){
                auto* w=static_cast<EscalatorWrite*>(deferred->allocate(sizeof write));*w=write;deferred->queue(writeEscalator,w);
            }else writeEscalator(const_cast<EscalatorWrite*>(&write));
            objects.records.push_back({ObjectDrawRecord::Plain,false,0,&mesh,nullptr,0,0,0,0,0,objectMatrix(obj->transform(),obj->scale())});
        }
    }
    shadows.clear();
    if(!linkedReady)return;
    assets.beginFrame();
    ObjectDraw context{view,assets,tick,camera,course,scratch,shadows,objects};
    ObjectDraw waterContext{view,assets,tick,camera,course,scratch,shadows,water};
    for(auto& v:visuals)if(!skyObject(v))drawObject(waterObject(v)?waterContext:context,v);
}
void CourseObjects::Impl::replay(Renderer& renderer,const ObjectDrawList& list){
    for(const auto& r:list.records){
        if(r.kind==ObjectDrawRecord::Plain){renderer.draw(*r.target,r.transform);continue;}
        if(r.kind==ObjectDrawRecord::Explosion){
            if(blastDrawer){blastDrawer(renderer,r.transform,unsigned(std::max(r.frame,0.f)));continue;}
            assets.explosionBank->apply(*assets.explosion,0,r.frame,tick);
            renderer.draw(*assets.explosion,r.transform);continue;
        }
        RigidPose pose;
        if(r.pose==1){pose.parts=r.parts;pose.palette=list.palettes.data()+r.palette;pose.stride=12;pose.matrices=r.bones;renderer.setRigidPose(&pose);}
        else if(r.pose==2)renderer.setSkinPalette(list.palettes.data()+r.palette,r.bones,12);
        if(r.overrideCount)renderer.setOverrides(list.overrides.data()+r.overrides,r.overrideCount);
        renderer.draw(*r.target,r.transform,r.backdrop);
        renderer.setOverrides(nullptr,0);
        renderer.setRigidPose(nullptr);
        renderer.setSkinPalette(nullptr,0,12);
    }
}
void CourseObjects::prepare(const Renderer& renderer,unsigned tick,Vec3 camera){
    auto& d=*impl_;
    d.begin(renderer,tick,camera);
    d.skyJob.reset([](void* c){static_cast<Impl*>(c)->recordSky();},&d);
    d.objectJob.reset([](void* c){static_cast<Impl*>(c)->recordObjects();},&d);
    auto& worker=jobWorker();
    d.queued=worker.running();
    if(d.queued){worker.submit(d.skyJob);worker.submit(d.objectJob);}
    else{d.recordSky();d.recordObjects();}
}
void CourseObjects::draw(Renderer& renderer,unsigned tick,Vec3 camera){
    auto& d=*impl_;
    if(!d.recorded){d.begin(renderer,tick,camera);d.recordSky();d.recordObjects();}
    else if(d.queued){
        // The object pass may run here only once the sky pass has finished.
        jobWorker().finish(d.skyJob);jobWorker().finish(d.objectJob);d.queued=false;
    }
    d.replay(renderer,d.objects);
    d.recorded=false;
    // Retail shadow volumes darken the course and objects drawn before them.
    if(d.linkedReady)renderer.drawShadowVolumes(d.shadows.data(),unsigned(d.shadows.size()),.5f);
}
void CourseObjects::drawWater(Renderer& renderer){
    // draw() has joined the object job. Keep the recorded materials and pose
    // at that same tick; no second simulation or recording is needed.
    impl_->replay(renderer,impl_->water);
}
bool CourseObjects::Impl::ready(){
    if(assets.models.empty())return false;
    // A restarted race builds a new Kinoko scene: placements must re-link.
    const auto* race=K::System::RaceManager::Instance();
    const uint32_t timer=race?race->timer():0;
    if(linked&&(K::Field::ObjectDirector::Instance()!=director||timer<lastTimer)){linked=false;visuals.clear();}
    lastTimer=timer;
    if(!linked)link();
    return true;
}
void CourseObjects::setBlastDrawer(std::function<void(Renderer&,const C3D_Mtx&,unsigned)> drawer){impl_->blastDrawer=std::move(drawer);}
void CourseObjects::drawSky(Renderer& renderer,unsigned tick,Vec3 camera){
    auto& d=*impl_;
    if(!d.recorded){d.begin(renderer,tick,camera);d.recordSky();d.recordObjects();}
    else if(d.queued)jobWorker().finish(d.skyJob);
    d.replay(renderer,d.sky);
}
void ObjectDraw::shadow(ObjModel* m,const C3D_Mtx& transform){
    if(!renderQuality().shadowVolumes)return;
    if(!m||!m->model||!view.model(transform).intersects(m->bounds))return;
    shadows_.push_back({m->model.get(),transform});
}
void ObjectDraw::explosion(const C3D_Mtx& transform,float frame){
    if(!assets.explosion||!assets.explosionBank)return;
    ObjectDrawRecord r;r.kind=ObjectDrawRecord::Explosion;r.frame=frame;r.transform=transform;
    list.records.push_back(r);
}
void ObjectDraw::plain(const Model& model,const C3D_Mtx& transform){
    ObjectDrawRecord r;r.kind=ObjectDrawRecord::Plain;r.target=&model;r.transform=transform;
    list.records.push_back(r);
}
// One cooked model at transform with the given pose/material tracks: posed,
// skinned and its materials evaluated now, drawn when the list is replayed.
void ObjectDraw::submit(ObjModel* m,const C3D_Mtx& transform,const Anim& source){
    if(!m||!m->model)return;
    // Reduced quality: crowds step their clip (one frame per tick) every
    // animationStep frames, so a skinned pose repeats instead of being skinned again.
    Anim anim=source;const float step=float(renderQuality().animationStep);
    if(m->crowd&&step>0&&anim.clip>=0&&!anim.palette)anim.frame=std::floor(anim.frame/step)*step;
    if(!view.model(transform).intersects(anim.bounds?*anim.bounds:m->bounds))return;
    if(renderQuality().tooSmall(anim.bounds?*anim.bounds:m->bounds,transform,camera))return;
    const ObjectBank* bank=m->bank.get();
    // GPU-skinned meshes hold bone-local positions: unanimated, they draw the bind pose.
    const bool posed=bank&&(anim.palette||anim.clip>=0||m->rigid||m->gpuSkin);
    const float* palette=nullptr;
    if(posed){
        palette_.resize(size_t(bank->matrixCount())*12);
        if(anim.palette)palette=anim.palette;
        else{bank->pose(anim.clip,anim.frame,palette_.data());palette=palette_.data();}
    }
    ObjectDrawRecord r;r.backdrop=backdrop;r.transform=transform;
    const Model* target=m->model.get();
    auto keepPalette=[&](){r.palette=unsigned(list.palettes.size());list.palettes.insert(list.palettes.end(),palette,palette+size_t(bank->matrixCount())*12);};
    if(posed&&m->rigid){r.pose=1;r.parts=&bank->rigidParts();r.bones=bank->matrixCount();keepPalette();}
    else if(posed&&m->gpuSkin){r.pose=2;r.bones=bank->matrixCount();keepPalette();renderMetrics.skinnedVertices+=m->model->vertexCount;}
    else if(posed&&m->cpuSkin&&(anim.clip>=0||anim.palette)){
        int copy=-1;
        // A clip pose is a function of (clip, frame); procedural palettes are unique.
        if(!anim.palette)for(unsigned i=0;i<m->skinUsed;++i)if(m->skinKeys[i].clip==anim.clip&&m->skinKeys[i].frame==anim.frame){copy=int(i);break;}
        // A copy left from an earlier frame may already hold this pose.
        if(copy<0&&!anim.palette)for(unsigned i=m->skinUsed;i<m->skins.size();++i)
            if(m->skinKeys[i].clip==anim.clip&&m->skinKeys[i].frame==anim.frame){
                std::swap(m->skins[i],m->skins[m->skinUsed]);std::swap(m->skinKeys[i],m->skinKeys[m->skinUsed]);
                copy=int(m->skinUsed++);break;
            }
        if(copy<0){
            // Race UI pages and effects still load mid-race: new copies stay
            // out of the last SkinReserve bytes of LINEAR memory. Past that an
            // instance reuses a pose already skinned this frame.
            const size_t bytes=m->model->vertexCount*sizeof(Vertex)+m->model->indexCount*sizeof(uint16_t);
            if(m->skinUsed>=m->skins.size()&&linearSpaceFree()>SkinReserve+bytes)
                if(auto fresh=cloneModel(*m->model)){m->skins.push_back(std::move(fresh));m->skinKeys.push_back({});}
            if(m->skinUsed<m->skins.size()){
                copy=int(m->skinUsed++);m->skinKeys[copy]={anim.palette?-1:anim.clip,anim.frame};
                if(auto* deferred=deferredWrites()){
                    const std::size_t bytes=std::size_t(bank->matrixCount())*12*sizeof(float);
                    auto* w=static_cast<SkinWrite*>(deferred->allocate(sizeof(SkinWrite)+bytes));
                    *w={bank,m->skins[copy].get(),m->litNormals};std::memcpy(w+1,palette,bytes);
                    deferred->queue(writeSkin,w);
                }else bank->skin(palette,*m->skins[copy],m->litNormals);
                renderMetrics.skinnedVertices+=m->skins[copy]->vertexCount;
            }else if(m->skinUsed)copy=int(m->skinUsed-1);
            else return;
        }
        target=m->skins[copy].get();
    }
    const bool material=bank&&!m->overrides.empty()&&(anim.pat>=0||anim.srt>=0||anim.clr>=0)&&
        bank->material(anim.pat,anim.patFrame,anim.srt,anim.srtFrame,anim.clr,anim.clrFrame,m->overrides.data(),unsigned(m->overrides.size()));
    if(material){r.overrides=unsigned(list.overrides.size());r.overrideCount=unsigned(m->overrides.size());list.overrides.insert(list.overrides.end(),m->overrides.begin(),m->overrides.end());}
    r.target=target;
    list.records.push_back(r);
}
bool CourseObjects::reviewBounds(unsigned index,Vec3& center,float& radius){
    auto& d=*impl_;
    if(index>=d.visuals.size())return false;
    const auto& v=d.visuals[index];
    const ObjModel* model=v.main.model?v.main.model:v.extra[0].model;
    if(!model)return false;
    const auto* focus=reviewFocus(v);
    const C3D_Mtx m=v.drawnValid?v.drawn:focus?objectMatrix(focus->transform(),focus->scale()):v.placement;
    float rows[12];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=m.r[r].c[3-c];
    const auto b=transformBounds(model->bounds,rows);
    center=(b.minimum+b.maximum)*.5f;radius=std::max(50.f,length(b.maximum-b.minimum)*.5f);
    return true;
}
std::vector<CourseObjects::ReviewTarget> CourseObjects::reviewTargets(){
    auto& d=*impl_;
    if(!d.linked)d.link();
    std::vector<ReviewTarget> out;
    for(unsigned i=0;i<d.visuals.size();++i){
        const auto& v=d.visuals[i];
        bool seen=false;for(const auto& t:out)seen|=!std::strcmp(t.name,v.name);
        if(seen)continue;
        ReviewTarget t{};std::memcpy(t.name,v.name,sizeof t.name);t.visual=i;
        if(reviewBounds(i,t.center,t.radius))out.push_back(t);
    }
    return out;
}
void courseExplosionEffects(RaceEffects& effects,int bombHei){
    const auto* director=K::Field::ObjectDirector::Instance();
    if(!director||!effects.ready||bombHei<0)return;
    for(const auto* obj:director->objects()){
        if(!obj)continue;
        if(obj->id()==K::Field::ObjectId::HeyhoBallGBA){
            // Effect update PAL 0x806D15A8: frame 0 of the explode state fires
            // rk_bombHei at m_initPos (Kinoko pos + 50 Y while exploding), with
            // no rotation and scale 1500/900.
            const auto& b=*static_cast<const K::Field::ObjectHeyhoBall*>(obj);
            if(b.stateId()!=3||b.stateFrame()!=0)continue;
            EffectFrame f;f.origin={b.pos().x,b.pos().y+50.f,b.pos().z};f.scale=1500.f/900.f;
            effects.burst(bombHei,f);
        }else if(obj->id()==K::Field::ObjectId::KoopaBall){
            // PAL 0x807719E8, called by calcTangible 0x80770F4C on the step the
            // ball starts exploding: rk_bombHei on the ball's transform at scale
            // 2.0 (0x808CF3F4). Kinoko sets the explode timer to the clip length then.
            const auto& b=*static_cast<const K::Field::ObjectKoopaBall*>(obj);
            if(b.state()!=2||b.explodeTimer()!=int(b.explodeFrames()))continue;
            const auto& t=b.transform();
            EffectFrame f;f.origin={t[0,3],t[1,3],t[2,3]};
            f.x=normalized(Vec3{t[0,0],t[1,0],t[2,0]},{1,0,0});
            f.y=normalized(Vec3{t[0,1],t[1,1],t[2,1]},{0,1,0});
            f.z=normalized(Vec3{t[0,2],t[1,2],t[2,2]},{0,0,1});
            f.scale=2.f;
            effects.burst(bombHei,f);
        }
    }
}
}
