#include <malloc.h>
#include <mkw/diag.hpp>
#include <mkw/course_mii_billboards.hpp>
#include <mkw/joint_billboard.hpp>
#include <mkw/course_mii_statue.hpp>
#include <game/system/RaceConfig.hh>
#include <mkw/blast_renderer.hpp>
#include "mkw/course_objects.hpp"
#include <mkw/animation_budget.hpp>
#include "mkw/course_object_kinds.hpp"
#include "mkw/render_quality.hpp"
#include "mkw/escalator_pieces.hpp"
#include "mkw/retail_lighting.hpp"
#include <mkw/render_metrics.hpp>
#include <mkw/job_worker.hpp>
#include <mkw/asset_pack.hpp>
#include <game/field/ObjectDirector.hh>
#include <game/field/ObjectDrivableDirector.hh>
#include <game/field/obj/ObjectEscalator.hh>
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
// Static precull switches, read at the first link: no-static-precull.txt turns
// it off; DIAG precull-check.txt runs the culled presenters anyway and counts
// any that add a draw; object-set.txt logs each object pass's records.
// DIAG no-reset-skip.txt: every object record resets overrides, rigid pose and
// skin palette again (for comparing command-word hashes).
bool staticPrecull=true,precullCheck=false,objectSetLog=false,resetSkip=true,switchesRead=false;
void readSwitches(){
    if(switchesRead)return;switchesRead=true;
    auto exists=[](const char* path){FILE* f=std::fopen(path,"r");if(f)std::fclose(f);return f!=nullptr;};
    staticPrecull=!exists("sdmc:/3ds/mkw-3ds/no-static-precull.txt");
#ifndef MKW_RELEASE
    precullCheck=exists("sdmc:/3ds/mkw-3ds/precull-check.txt");
    objectSetLog=exists("sdmc:/3ds/mkw-3ds/object-set.txt");
    resetSkip=!exists("sdmc:/3ds/mkw-3ds/no-reset-skip.txt");
#endif
}
CourseObjects::PrecullStats precullStats;
std::unique_ptr<Model> cloneModel(const Model& source){
    auto copy=std::make_unique<Model>();
    copy->vertices=static_cast<Vertex*>(linearAlloc(source.vertexCount*sizeof(Vertex)));
    copy->indices=static_cast<uint16_t*>(linearAlloc(source.indexCount*sizeof(uint16_t)));
    if(!copy->vertices||!copy->indices)return nullptr;
    copy->vertexCount=source.vertexCount;copy->indexCount=source.indexCount;
    std::memcpy(copy->vertices,source.vertices,source.vertexCount*sizeof(Vertex));
    std::memcpy(copy->indices,source.indices,source.indexCount*sizeof(uint16_t));
    copy->submeshes=source.submeshes;copy->textures=source.textures;
    copy->samplers=source.samplers;
    copy->staticLightingBaked=source.staticLightingBaked;copy->sourceVertexHash=source.sourceVertexHash;
    copy->fixedLighting=source.fixedLighting;copy->liveDiffuse=source.liveDiffuse;copy->bakedCourse=source.bakedCourse;
    if(source.staticEnvironment2){
        const size_t bytes=size_t(source.vertexCount)*2*sizeof(float);
        copy->staticEnvironment2=static_cast<float*>(linearAlloc(bytes));if(!copy->staticEnvironment2)return nullptr;
        std::memcpy(copy->staticEnvironment2,source.staticEnvironment2,bytes);flushGpuData(copy->staticEnvironment2,bytes);
    }
    copy->minimum=source.minimum;copy->maximum=source.maximum;
    flushGpuData(copy->indices,copy->indexCount*sizeof(uint16_t));
    return copy;
}
bool endsWith(const std::string& s,const char* suffix){const size_t n=std::strlen(suffix);return s.size()>=n&&!s.compare(s.size()-n,n,suffix);}
// A Coconut Mall escalator as retail draws it (escalator_pieces.hpp), in one
// GPU-skinned draw (unlit skin: the bone in the normal's x, as
// ObjectBank::gpuSkin; the strip's lighting is baked at load). Bone 0 is the
// strip, bones 1-4 the escalator_pieces.hpp slots. escalatorA/B come from
// the generic object cook (race.pak obj05_escalator__*): the retail piece
// geometry and UVs, under another cook's material and texture. They take the
// strip's (the same retail material, escalator_m). Every strip vertex faces
// up with a white source colour, so the load-time bake gives them one colour;
// the pieces take it. Without both pieces the model is the strip alone.
struct EscalatorBuild {std::unique_ptr<Model> model;unsigned bones=0,dropped=0;bool uniform=true;};
EscalatorBuild buildEscalator(const Model& strip,const Model* a,const Model* b){
    EscalatorBuild out;
    if(strip.submeshes.size()!=1||!strip.vertexCount||strip.staticEnvironment2||strip.skinBoneIndices||!strip.staticLightingBaked||strip.liveDiffuse)return out;
    const auto& s=strip.submeshes[0];
    // The cook appends copies of the end treads past the retail strip (Y
    // -200..1500, Z -4500..600) for the old bent-strip draw: 24 triangles, 8 at
    // the bottom and 16 at the top, whose first riser stands on the strip's last edge.
    auto extension=[&](uint32_t i){
        for(unsigned k=0;k<3;++k){const Vec3 p=strip.vertices[strip.indices[i+k]].position;if(p.z< -4500.5f||p.z>600.5f||p.y< -200.5f||p.y>1500.5f)return true;}
        return false;
    };
    const Model* pieces[4]={a,b,a,b};
    const bool withPieces=a&&b&&a->submeshes.size()==1&&b->submeshes.size()==1;
    uint32_t vertexCount=strip.vertexCount,indexCount=0;
    for(uint32_t i=s.first;i+2<s.first+s.count;i+=3)if(extension(i))++out.dropped;else indexCount+=3;
    if(withPieces)for(const Model* p:pieces){vertexCount+=p->vertexCount;indexCount+=p->submeshes[0].count;}
    if(vertexCount>65535)return out;
    auto m=std::make_unique<Model>();
    m->vertices=static_cast<Vertex*>(linearAlloc(vertexCount*sizeof(Vertex)));
    m->indices=static_cast<uint16_t*>(linearAlloc(indexCount*sizeof(uint16_t)));
    if(!m->vertices||!m->indices)return out;
    m->vertexCount=vertexCount;m->indexCount=indexCount;
    const uint8_t* color=strip.vertices[0].color;
    for(uint32_t i=1;i<strip.vertexCount;++i)out.uniform&=!std::memcmp(strip.vertices[i].color,color,4);
    std::memcpy(m->vertices,strip.vertices,strip.vertexCount*sizeof(Vertex));
    for(uint32_t i=0;i<strip.vertexCount;++i)m->vertices[i].normal={0,0,0};
    uint32_t at=0;bool any=false;Vec3 low{},high{};
    auto include=[&](Vec3 p){if(!any){low=high=p;any=true;}
        low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};};
    for(uint32_t i=s.first;i+2<s.first+s.count;i+=3)if(!extension(i))
        for(unsigned k=0;k<3;++k){m->indices[at++]=strip.indices[i+k];include(strip.vertices[strip.indices[i+k]].position);}
    uint32_t base=strip.vertexCount;
    if(withPieces)for(unsigned slot=0;slot<4;++slot){
        const Model& p=*pieces[slot];const auto& ps=p.submeshes[0];
        for(uint32_t i=0;i<p.vertexCount;++i){
            Vertex v=p.vertices[i];std::memcpy(v.color,color,4);v.normal={float((slot+1)*3),0,0};m->vertices[base+i]=v;
        }
        for(uint32_t k=0;k<ps.count;++k)m->indices[at++]=uint16_t(base+p.indices[ps.first+k]);
        base+=p.vertexCount;
        // Bounds: the piece wherever its slot puts it over a step period.
        for(unsigned step=0;step<=80;++step){
            EscalatorPiece place[4];escalatorPieces(std::min(step*.25f,19.999f),place);const auto& q=place[slot];
            if(q.shown)for(const Vec3 c:{p.minimum,p.maximum})include({c.x,q.y+q.scaleY*c.y,q.z+c.z});
        }
    }
    Submesh material=s;material.first=0;material.count=indexCount;
    m->submeshes={material};
    m->textures=strip.textures;m->samplers=strip.samplers;
    m->uvTracks=strip.uvTracks;m->materialTracks=strip.materialTracks;m->materialData=strip.materialData;
    m->staticLightingBaked=true;m->fixedLighting=strip.fixedLighting;m->bakedCourse=strip.bakedCourse;
    m->dynamicLightTextures=strip.dynamicLightTextures;
    m->minimum=low;m->maximum=high;
    flushGpuData(m->vertices,vertexCount*sizeof(Vertex));flushGpuData(m->indices,indexCount*sizeof(uint16_t));
    out.bones=withPieces?5:1;
    // Per-bone culling blocks; the player-shadow receiver pass needs them too.
    m->enableSkinCulling(out.bones);
    out.model=std::move(m);
    return out;
}
// One CPU-skinned instance copy (its palette follows the record).
// skinned: the prepare()d results (after the palette), or null.
struct SkinWrite {const ObjectBank* bank;Model* model;bool normals;int clip;float frame;Vec3* skinned;};
void writeSkin(void* payload){
    const auto& w=*static_cast<const SkinWrite*>(payload);
    w.bank->skin(reinterpret_cast<const float*>(&w+1),*w.model,w.normals,w.clip,w.frame,w.skinned);
}
void prepareSkin(void* payload){
    const auto& w=*static_cast<const SkinWrite*>(payload);
    w.bank->skinCompute(reinterpret_cast<const float*>(&w+1),*w.model,w.normals,w.clip,w.frame,w.skinned);
}
// directWrites: both halves at frame start, into a copy no unfinished frame draws.
void directSkin(void* payload){
    prepareSkin(payload);writeSkin(payload);
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
// Which file and step failed in the last ObjectAssets::load (runtime.log).
char objectLoadFailure[160]="";
namespace {
// fireBPlane's 100 phases (13 MB): as many as the heap can spare.
bool openFirePhases(ObjModel& m){
    auto stream=std::make_unique<FirePhaseStream>();
    // Leave ordinary heap headroom for racers, menus and live effects.
    const size_t heapBytes=envGetHeapSize(),usedBytes=mallinfo().uordblks;
    const size_t budget=heapBytes>usedBytes+(8u<<20)?heapBytes-usedBytes-(8u<<20):0;
    // Old 3DS (reduced): at least 10 of the 100 phases (1.3 MB), not none.
    const size_t phaseBudget=renderQuality().reduced?std::max<size_t>(budget,10u*131072u):budget;
    auto& texture=*m.model->textures[m.fireTexture];
    if(!stream->open("textures/firebar-indirect.ftan",phaseBudget)||!stream->read(0,texture.gpu.data,texture.gpu.size)){
        std::snprintf(objectLoadFailure,sizeof objectLoadFailure,"fire phases (budget %u)",unsigned(phaseBudget));return false;}
    C3D_TexFlush(&texture.gpu);
    // Staging for the phase uploads (New 3DS; off: no-hook-prep.txt). 256 KB
    // of LINEAR; without it each phase is a CPU write on the frame hook.
    if(DeferredWrites::prepareWrites&&!renderQuality().reduced&&!m.fireStaging.half[0]&&(stream->frameBytes&15)==0)
        if(auto* both=static_cast<uint8_t*>(linearAlloc(size_t(stream->frameBytes)*2))){m.fireStaging.half[0]=both;m.fireStaging.half[1]=both+stream->frameBytes;}
    m.firePhase=0;m.firePending=false;
    fireCacheBytes.store(stream->bytes());fireCacheStride.store(stream->stride());
    m.firePhases=std::move(stream);
    return true;
}
}
namespace {
// shareObjectTextures: a texture whose size, format, sampler state and every
// byte match one an earlier model of this course loaded is replaced by that
// one (unit-1 marks merged), so the picture can't change.
struct SharedTextures {
    struct Entry {uint32_t hash;std::shared_ptr<Texture> texture;};
    std::vector<Entry> canonical;
    // Replaced copies stay alive until the load ends: the path cache then
    // hands later models of the same archive the copy instead of a new read.
    std::vector<std::pair<std::shared_ptr<Texture>,std::shared_ptr<Texture>>> replaced;
    unsigned shared=0;std::size_t bytes=0;
    static uint32_t hashOf(const void* data,std::size_t bytes){
        const auto* w=static_cast<const uint32_t*>(data);uint32_t h=2166136261u;
        for(std::size_t i=0;i<bytes/4;++i)h=(h^w[i])*16777619u;
        return h;
    }
    void share(Model& model){
        for(auto& t:model.textures){
            if(!t||!t->ready||!t->gpu.data)continue;
            bool done=false;
            for(const auto& c:canonical)if(c.texture==t){done=true;break;}
            if(done)continue;
            for(const auto& [copy,to]:replaced)if(copy==t){to->unitOne|=t->unitOne;t=to;done=true;break;}
            if(done)continue;
            const auto& g=t->gpu;const std::size_t size=C3D_TexCalcTotalSize(g.size,g.maxLevel);
            const uint32_t hash=hashOf(g.data,size);
            for(const auto& c:canonical){
                const auto& h=c.texture->gpu;
                if(c.hash!=hash||h.width!=g.width||h.height!=g.height||h.fmt!=g.fmt||h.size!=g.size||h.param!=g.param||
                   h.border!=g.border||h.lodParam!=g.lodParam||std::memcmp(h.data,g.data,size))continue;
                c.texture->unitOne|=t->unitOne;replaced.push_back({t,c.texture});t=c.texture;
                ++shared;bytes+=size;done=true;break;
            }
            if(!done)canonical.push_back({hash,t});
        }
    }
};
}
// models/<name><ext>, or with objectWeldOff its unwelded copy
// models/<name>_pv<ext> when the pack (or SD) has one.
static bool objectFile(char* out,size_t size,const char* name,const char* ext){
    if(objectWeldOff){
        std::snprintf(out,size,"models/%s_pv%s",name,ext);
        if(pack::size(out)>=0)return true;
        if(FILE* f=std::fopen(out,"rb")){std::fclose(f);return true;}
    }
    std::snprintf(out,size,"models/%s%s",name,ext);
    return false;
}
bool ObjectAssets::load(int course){
    unweldedObjectMeshes=unweldedObjectBanks=0;
    char path[64];std::snprintf(path,sizeof path,"models/course_%02d_visuals.list",course);
    FILE* list=std::fopen(path,"r");if(!list)return course>=32;
    const auto* race=Kinoko::System::RaceConfig::Instance();
    const bool miiHeads= race&&miiStatuePlayersEligible(race->raceScenario().players,race->raceScenario().playerCount);
    char resource[64],name[64],mesh[96],bankName[96];bool ok=true;
    SharedTextures sharedTextures;
    while(std::fscanf(list,"%63s %63s %95s %95s",resource,name,mesh,bankName)==4){
        if(std::strchr(mesh,'/')||std::strstr(mesh,"..")||std::strchr(bankName,'/')||std::strstr(bankName,"..")){ok=false;break;}
        // No Mii racer means the original manager returns no statue identity.
        // Keep the base statue; do not load an unused substitute head pool.
        if(!miiHeads&&!std::strcmp(resource,"__MiiStatueHeads"))continue;
        // Battle coin visuals belong exclusively to CoinBattleRuntime. The
        // GOBJ entry registers a spawn, not a permanently placed idle model.
        if(course>=32&&course<=41&&!std::strcmp(resource,"coin"))continue;
        auto m=std::make_unique<ObjModel>();m->resource=resource;m->name=name;m->model=std::make_unique<Model>();
        char file[128];unweldedObjectMeshes+=objectFile(file,sizeof file,mesh,".gmdl");
        // Crowds and shadow volumes keep the whole bake (many vertices; no lit surface).
        const bool liveLight=diag::liveLight&&std::strncmp(resource,"group_",6)&&std::strncmp(resource,"monte_",6)&&!endsWith(name,"-shadow");
        if(!m->model->load(file,false,liveLight)){std::snprintf(objectLoadFailure,sizeof objectLoadFailure,"model %s",file);ok=false;break;}
        if(shareObjectTextures)sharedTextures.share(*m->model);
        if((course==18&&!std::strcmp(resource,"WLscreenGC")&&!std::strcmp(name,"WLscreenGC"))||
           (course==6&&!std::strcmp(resource,"dkmonitor")&&!std::strcmp(name,"dkmonitor"))||
           (course==35&&!std::strcmp(resource,"fks_screen_wii")&&!std::strcmp(name,"fks_screen_wii"))){
            const int screenTexture=course==6?3:course==35?2:0;m->model->stadiumTexture=screenTexture;
            // This display face is immutable. Do not walk all its indexed
            // triangles on the simulation thread every frame to cull capture.
            for(const auto& shape:m->model->submeshes)if(shape.texture==screenTexture)
                for(unsigned k=shape.first;k<shape.first+shape.count;++k){
                    const auto p=m->model->vertices[m->model->indices[k]].position;
                    if(!m->hasStadiumFace){m->stadiumFace={p,p};m->hasStadiumFace=true;}
                    else m->stadiumFace.include(p);
                }
        }
        // Keep the original spectator TEV/light-texture material intact.
        // Model::load already bakes course diffuse and environment UV once;
        // Renderer selects the fixed (not camera-sampled) map pair for it.
        // HW108: these small flag textures use trilinear GX mip sampling.
        // Keep the original base pixels/lighting, and avoid the PICA mip path
        // only for Gold Mine's flags, including both FlagB model LODs.
        if(course==7&&(!std::strcmp(resource,"FlagA1")||!std::strcmp(resource,"FlagB1")))
            for(auto& row:m->model->samplers)if(row[0].valid)row[0].minFilter&=1;
        m->bounds={m->model->minimum,m->model->maximum};
        if(std::strcmp(bankName,"-")){
            m->bank=std::make_unique<ObjectBank>();unweldedObjectBanks+=objectFile(file,sizeof file,bankName,".dobj");
            if(!m->bank->load(file)){std::snprintf(objectLoadFailure,sizeof objectLoadFailure,"bank %s",file);ok=false;break;}
            if(m->resource=="pakkun_f"&&!m->bank->hasJointCallbacks()){
                std::fprintf(stderr,"pakkun_f bank missing original joint callbacks\n");ok=false;break;
            }
            m->bank->markRegisterAlpha(*m->model);
            m->bank->markUnitOneTextures(*m->model);
            Bounds animated;if(m->bank->animatedBounds(animated)){m->bounds.include(animated.minimum);m->bounds.include(animated.maximum);}
            if(m->bank->skinned()){
                // One bone per triangle: share the mesh and skin on the GPU.
                // Weighted meshes are CPU-skinned into per-instance copies.
                const bool animatedShadow=m->resource=="r_parasol"&&m->name=="r_parasol-shadow";
                if(!animatedShadow&&m->bank->makeRigid(*m->model))m->rigid=true;
                else if((m->cpuSkin=m->bank->weighted()||animatedShadow)){
                    m->crowd=!std::strncmp(resource,"group_",6)||!std::strncmp(resource,"monte_",6);
                    if(!m->model->staticLightingBaked)for(const auto& s:m->model->submeshes)
                        m->litNormals|=(s.lighting&retail::PrimaryEnvironment)||((s.lighting&retail::Retail)&&((s.lighting&retail::ChannelMask)>>retail::ChannelShift)!=0);
                    // Unlit objects never read skinned normals: skin on the GPU
                    // (one shared mesh, any pose per instance), else positions only.
                    if(!animatedShadow&&!m->litNormals&&m->bank->gpuSkin(*m->model,m->model->liveDiffuse)){m->gpuSkin=true;m->cpuSkin=false;m->model->enableSkinCulling(m->bank->matrixCount());}
                    else if(!m->litNormals){m->model->finishStaticLighting();m->bank->shareSkinnedPositions();}
                    else m->bank->shareSkinnedVertices();
                    if(m->cpuSkin){
                        // One mandatory pose buffer must exist before preview labels
                        // and race sound consume the remaining LINEAR space. Additional
                        // per-instance copies may respect the overlay reserve; the first
                        // visible instance must never disappear because of that reserve.
                        auto first=cloneModel(*m->model);
                        if(!first){std::snprintf(objectLoadFailure,sizeof objectLoadFailure,"pose copy %s",file);ok=false;break;}
                        m->skins.push_back(std::move(first));m->skinKeys.push_back({-1,0.f});
                    }
                }
            }
            if(m->bank->shapeCount()==m->model->submeshes.size())m->overrides.resize(m->model->submeshes.size());
            for(const auto& source:CourseMiiBillboards){
                if(m->resource!=source.resource||m->name!=source.model)continue;
                const int index=m->bank->node(source.node);if(index<0)continue;
                const auto slot=m->bank->nodes()[index].slot;
                if(slot>=0&&unsigned(slot)<m->bank->matrixCount())m->miiBillboards.push_back({unsigned(slot),source.mode});
            }

        }
        if(m->resource=="WLfirebarGC"&&m->name=="fireBPlane"){
            auto texture=std::make_shared<Texture>();
            if(!C3D_TexInit(&texture->gpu,256,256,GPU_LA8)){std::snprintf(objectLoadFailure,sizeof objectLoadFailure,"fire texture");ok=false;break;}
            texture->ready=true;
            std::memset(texture->gpu.data,0,texture->gpu.size);
            C3D_TexSetFilter(&texture->gpu,GPU_LINEAR,GPU_LINEAR);
            C3D_TexSetWrap(&texture->gpu,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
            m->fireTexture=int(m->model->textures.size());
            m->model->textures.push_back(std::move(texture));
            m->firePending=true;
            // New 3DS opens the phases now; Old 3DS waits for the opponents
            // (loadFirePhases): a cold first race would give the stream the heap
            // its racers' banks need, and the race sound cache then has no LINEAR.
            if(!renderQuality().reduced&&!openFirePhases(*m)){ok=false;break;}
        }
        models.push_back(std::move(m));
    }
    std::fclose(list);
    sharedObjectTextures=sharedTextures.shared;sharedObjectTextureBytes=sharedTextures.bytes;
    if(!ok){models.clear();return false;}
    if(find("koopaBall","koopaBall")||find("HeyhoBallGBA","HeyhoBallGBA")||find("K_bomb_car","K_bomb_car")){
        explosion=std::make_unique<Model>();explosionBank=std::make_unique<DriverAnimation>();
        if(!explosion->load("models/bomb_blast.gmdl")||!explosionBank->load("models/bomb_core.dskm",*explosion)){
            explosion.reset();explosionBank.reset();models.clear();return false;
        }
    }
    return true;
}
void ObjectAssets::beginFrame(){explosionUsed=0;for(auto& m:models)m->skinUsed=0;}

// Retail's draw distance for a course object, squared; 0 for none. Objects
// whose ObjFlow clip mode (+0x64) is 1, 2, 8, 9, 10 or 11 register with the
// clip manager with ObjFlow +0x68 x 10 as their distance (USA 0x8080d548..,
// 0x80783674). Each frame it hides an object when its position's squared
// distance from the camera, times (min(fovy, 55) / 55)^2, exceeds that
// distance squared (USA 0x807834a4..0x807834f8, factor from 0x80782cf4).
float drawDistance2(const K::Field::SObjectCollisionSet* set){
    if(!set)return 0;
    auto field=[set](unsigned offset){return int16_t((set->_62[offset-0x62]<<8)|set->_62[offset-0x62+1]);};
    switch(field(0x64)){case 1:case 2:case 8:case 9:case 10:case 11:break;default:return 0;}
    const float distance=float(field(0x68))*10;
    return distance*distance;
}
float drawDistance2(K::Field::ObjectId id){
    const auto* objects=K::Field::ObjectDirector::Instance();
    if(!objects)return 0;
    const auto& flow=objects->flowTable();
    return drawDistance2(flow.set(flow.slot(id)));
}
struct CourseObjects::Impl {
    size_t allocationReserve=SkinReserve;
    std::unique_ptr<Model> palm;
    // Coconut Mall escalators (buildEscalator): one model shared by all six,
    // each drawn once with its own palette of escalatorBones matrices.
    std::unique_ptr<Model> escalator;
    unsigned escalatorBones=0;
    std::vector<C3D_Mtx> palms;
    struct LavaSplash { bool active=false;unsigned start=0;Vec3 position{};float scale=1; };
    std::array<LavaSplash,12> lavaSplashes{};
    int course=-1;
    ObjectAssets assets;
    std::vector<Visual> visuals,suspendedVisuals;
    // Per visual (same order), retail's draw distance squared (drawDistance2).
    std::vector<float> drawDistances;
    float palmDistance=0,escalatorDistance=0;
    // Retail's FOV factor (min(fovy, 55) / 55) squared; 0: draw at any distance.
    float distanceScale=0;
    // The recorded frame draws in stereo (read in begin, on the main thread).
    bool stereo=false;
    // ObjectDraw::shadowRatio for this frame, set at record time (begin).
    float shadowRatio=0;
    bool beyond(Vec3 position,float limit) const {
        return distanceScale>0&&limit>0&&dot(position-camera,position-camera)*distanceScale>limit;
    }
    bool graphicsSuspended=false;
    bool linked=false;
    const void* director=nullptr;
    uint32_t lastTimer=0;
    std::vector<float> scratch;
    std::vector<Renderer::ShadowVolume> shadows,skyShadows;
    void link();
    // Re-links after a restarted race builds a new Kinoko scene.
    bool ready();
    // Static precull (no-static-precull.txt): per visual whose presenter draws
    // at one matrix (singleMatrixBounds), its models' bounds and their padded
    // world box at the matrix last seen. A box clearly outside the view skips
    // the presenter, whose own tests would draw nothing.
    struct Precull {Bounds local{},world{};C3D_Mtx matrix{};bool eligible=false,valid=false;};
    std::vector<Precull> precull;
    // DIAG object-set.txt: each recorded object pass's records hashed (object-set.csv).
    std::string objectSetLines;
    void logObjectSet();
    // One frame's recorded sky pass and object pass (record), replayed in order.
    ObjectDrawList sky,objects,water;
    ViewFrustum view{};
    std::vector<Bounds> stadiumScreens;bool stadiumBoundsReady=false;
    unsigned tick=0;
    Vec3 camera{};
    bool linkedReady=false,recorded=false,queued=false;
    // The sky pass and the object pass are separate jobs, run in that order:
    // drawSky waits only for the sky, and the objects record during the track.
    Job skyJob,objectJob;
    void recordSky();
    void recordObjects();
    void begin(const Renderer& renderer,unsigned frameTick,Vec3 frameCamera);
    void replay(Renderer& renderer,const ObjectDrawList& list,unsigned phase=0);
};
CourseObjects::CourseObjects():impl_(std::make_unique<Impl>()){}
CourseObjects::~CourseObjects()=default;
unsigned CourseObjects::palmCount() const {return impl_->palms.size();}
std::string CourseObjects::modelName(unsigned m) const {
    const auto& models=impl_->assets.models;
    return m<models.size()?models[m]->resource+"/"+models[m]->name:"palm";
}
void CourseObjects::forEachTextureImpl(const std::function<void(unsigned,unsigned,const Texture*)>& f) const {
    unsigned m=0;
    auto each=[&](const Model* model){if(model)for(unsigned i=0;i<model->textures.size();++i)if(model->textures[i])f(m,i,model->textures[i].get());++m;};
    for(const auto& a:impl_->assets.models)each(a->model.get());
    each(impl_->palm.get());
}
unsigned CourseObjects::moveTexturesToVram(Renderer& renderer,std::size_t reserve,Renderer::VramLeft* left){
    unsigned moved=0;
    // Models of one archive share their textures (Model::load's cache): each
    // is placed, and counted in left, once.
    std::vector<const Texture*> seen;
    auto move=[&](const Model* model,int dynamic=-1){if(model)for(unsigned i=0;i<model->textures.size();++i)
        if(int(i)!=dynamic&&model->textures[i]&&std::find(seen.begin(),seen.end(),model->textures[i].get())==seen.end()){
            seen.push_back(model->textures[i].get());moved+=renderer.moveToVram(*model->textures[i],reserve,left);}};
    // Phase textures remain CPU-writable LINEAR; uploads are deferred until
    // the previous GPU frame has finished, unlike immutable course textures.
    for(const auto& m:impl_->assets.models)move(m->model.get(),m->fireTexture);
    move(impl_->palm.get());
    return moved;
}
unsigned CourseObjects::modelCount() const {return impl_->assets.models.size();}
void CourseObjects::reset(){
    auto& d=*impl_;
    if(d.queued){jobWorker().finish(d.skyJob);jobWorker().finish(d.objectJob);}
    const auto reserve=d.allocationReserve;
    // Recorded palettes and skin scratch are race-owned too. Rebuild the
    // empty owner rather than retaining the previous course's capacities.
    impl_=std::make_unique<Impl>();impl_->allocationReserve=reserve;
}
void CourseObjects::suspendGraphics(){
    auto& d=*impl_;if(d.graphicsSuspended)return;
    jobWorker().finishAll();
    if(!d.linked)d.link();
    d.suspendedVisuals=std::move(d.visuals);
    d.sky.clear();d.objects.clear();d.water.clear();d.shadows.clear();d.skyShadows.clear();
    d.assets.explosionPoses.clear();d.assets.explosionUsed=0;d.assets.explosion.reset();d.assets.explosionBank.reset();d.assets.models.clear();
    d.palm.reset();d.escalator.reset();d.escalatorBones=0;
    d.linked=false;d.linkedReady=false;d.recorded=false;d.queued=false;d.graphicsSuspended=true;
}
bool CourseObjects::resumeGraphics(){
    auto& d=*impl_;if(!d.graphicsSuspended)return true;
    d.assets.models.clear();d.palm.reset();d.escalator.reset();d.escalatorBones=0;
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
void CourseObjects::invalidate(){impl_->linked=false;impl_->visuals.clear();impl_->lavaSplashes={};}
void CourseObjects::triggerLavaSplash(unsigned racer,Vec3 position,float scale,unsigned tick){
    auto& d=*impl_;
    if(racer>=d.lavaSplashes.size())return;
    // Draw recording reads these states; never mutate a queued frame.
    if(d.queued){jobWorker().finish(d.skyJob);jobWorker().finish(d.objectJob);}
    d.lavaSplashes[racer]={true,tick,position,scale};
}
void CourseObjects::setAllocationReserve(std::size_t bytes){impl_->allocationReserve=std::max(SkinReserve,bytes);}
bool CourseObjects::loadCoconut(){
    auto& d=*impl_;
    d.palm=std::make_unique<Model>();
    if(!d.palm->load("models/coconut_palm_000.gmdl")){d.palm.reset();return false;}
    d.palm->enableStaticCulling();
    // Never written after load: all six escalators draw this one model. The
    // source models go once it is built (the textures stay shared with it).
    {
        Model strip,pieceA,pieceB;
        if(!strip.load("models/coconut_escalator_000.gmdl")){d.palm.reset();return false;}
        // Without its pieces the strip still draws (its landing steps just pop).
        char fileA[96],fileB[96];
        objectFile(fileA,sizeof fileA,"obj05_escalator__escalatora_000",".gmdl");objectFile(fileB,sizeof fileB,"obj05_escalator__escalatorb_000",".gmdl");
        const bool a=pieceA.load(fileA),b=pieceB.load(fileB);
        auto built=buildEscalator(strip,a?&pieceA:nullptr,b?&pieceB:nullptr);
        if(!built.model){d.palm.reset();return false;}
        d.escalator=std::move(built.model);d.escalatorBones=built.bones;
#ifndef MKW_RELEASE
        if(FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/runtime.log","a")){
            std::fprintf(f,"escalators: one skinned draw each, %u bones, %lu vertices, %lu indices; %u cooked extension triangles dropped; strip colour %s\n",
                built.bones,(unsigned long)d.escalator->vertexCount,(unsigned long)d.escalator->indexCount,built.dropped,built.uniform?"uniform":"varies");
            std::fclose(f);
        }
#endif
    }
    const auto* map=K::System::CourseMap::Instance();
    d.palms.clear();
    for(unsigned i=0;i<map->getGeoObjCount();++i){
        const auto* obj=map->getGeoObj(i);
        if(obj->id()!=unsigned(K::Field::ObjectId::PalmTree)||!(obj->presenceFlag()&1))continue;
        d.palms.push_back(placementMatrix(*obj));
    }
    return true;
}
bool CourseObjects::loadFirePhases(){
    for(auto& m:impl_->assets.models)if(m->firePending&&!openFirePhases(*m))return false;
    return true;
}
bool CourseObjects::loadCourse(int courseId){
    fireCacheBytes.store(0);fireCacheStride.store(0);
    auto& d=*impl_;
    d.visuals.clear();d.assets.models.clear();d.linked=false;d.course=courseId;
    return d.assets.load(courseId);
}
void CourseObjects::Impl::link(){
    readSwitches();
    stadiumBoundsReady=false;stadiumScreens.clear();
    visuals.clear();drawDistances.clear();linked=true;
    palmDistance=drawDistance2(K::Field::ObjectId::PalmTree);
    escalatorDistance=drawDistance2(K::Field::ObjectId::Escalator);
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
    // Blocks live only in their manager, not the ordinary object lists.
    // Override the manager's first-placement alias with the actual block.
    if(const auto* drivable=K::Field::ObjectDrivableDirector::Instance())
        if(const auto* manager=drivable->obakeManager())
            for(const auto* block:manager->blocks())if(block->mapObj())owner[block->mapObj()]={block};
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
        if(classifyObject(v,assets,course)){visuals.push_back(v);drawDistances.push_back(drawDistance2(set));}
    }
    precull.assign(visuals.size(),{});
    unsigned eligible=0;
    for(std::size_t i=0;i<visuals.size();++i)
        if(!skyObject(visuals[i])&&!partsClipped(visuals[i])&&singleMatrixBounds(visuals[i],precull[i].local)){precull[i].eligible=true;++eligible;}
    precullStats.eligible=eligible;

}
void CourseObjects::Impl::begin(const Renderer& renderer,unsigned frameTick,Vec3 frameCamera){
    // Linking reads and rebuilds visuals: always on this thread, before recording.
    view=renderer.viewFrustum();tick=frameTick;camera=frameCamera;linkedReady=ready();recorded=true;
    assets.serial=renderer.frameSerial();
    const float fov=std::min(renderer.cameraFov(),55.f)/55.f;
    distanceScale=renderer.drawDistance?fov*fov:0;
    stereo=diag::stereoObjectLod&&renderer.stereoWanted();
    // This recorded frame's stereo, read here on the main thread: the object job may run later.
    shadowRatio=diag::stereoShadowCull&&renderer.stereoWanted()?RenderQuality::stereoShadowRatio(renderer.cameraFov()):0;
}
// The sky pass precedes the object pass, which restarts the per-model
// CPU-skin copies, exactly as drawSky then draw ran them.
void CourseObjects::Impl::recordSky(){
    sky.clear();skyShadows.clear();
    if(!linkedReady)return;
    ObjectDraw context{view,assets,tick,camera,course,scratch,skyShadows,sky,true,allocationReserve};context.stereo=stereo;
    for(auto& v:visuals)if(skyObject(v))drawObject(context,v);
}
void CourseObjects::Impl::recordObjects(){
    objects.clear();water.clear();
    if(palm)for(const auto& matrix:palms)
        if(beyond({matrix.r[0].w,matrix.r[1].w,matrix.r[2].w},palmDistance))diag::tallyHidden("PalmTree");
        else if(!renderQuality().tooSmall({palm->minimum,palm->maximum},matrix,camera,stereo))objects.records.push_back({ObjectDrawRecord::Plain,false,0,palm.get(),nullptr,0,0,0,0,0,matrix});
    // Includes the children of paired escalators. Kinoko updates these transforms
    // after its 60 Hz step, including stops/reversals and the looping tread offset.
    // Query each frame so restart/map changes cannot leave stale scene pointers.
    // Each is one skinned draw at the object's transform: bone 0 the rigid
    // strip, bones 1-4 its landing pieces at the step phase (escalator_pieces.hpp,
    // a hidden piece collapsed by a zero matrix). Nothing is written to the model.
    if(escalator){
        for(const auto* obj:K::Field::ObjectDrivableDirector::Instance()->objects()){
            if(obj->id()!=K::Field::ObjectId::Escalator)continue;
            if(beyond({obj->pos().x,obj->pos().y,obj->pos().z},escalatorDistance)){diag::tallyHidden("escalator");continue;}
            const C3D_Mtx matrix=objectMatrix(obj->transform(),obj->scale());
            if(!view.visible(*escalator,matrix))continue;
            const unsigned palette=unsigned(objects.palettes.size());
            objects.palettes.resize(palette+escalatorBones*12,0.f);
            float* bones=objects.palettes.data()+palette;
            bones[0]=bones[5]=bones[10]=1.f;
            if(escalatorBones>1){
                EscalatorPiece pieces[4];
                escalatorPieces(static_cast<const K::Field::ObjectEscalator*>(obj)->stepFactor(),pieces);
                for(unsigned slot=0;slot<4;++slot)if(const auto& p=pieces[slot];p.shown){
                    float* b=bones+(slot+1)*12;
                    b[0]=1.f;b[5]=p.scaleY;b[7]=p.y;b[10]=1.f;b[11]=p.z;
                }
            }
            objects.records.push_back({ObjectDrawRecord::Submit,false,2,escalator.get(),nullptr,escalatorBones,palette,0,0,0,matrix});
        }
    }
    shadows.clear();
    if(!linkedReady)return;
    assets.beginFrame();
    ObjectDraw context{view,assets,tick,camera,course,scratch,shadows,objects,false,allocationReserve};
    ObjectDraw waterContext{view,assets,tick,camera,course,scratch,shadows,water,false,allocationReserve};
    context.stereo=waterContext.stereo=stereo;
    context.shadowRatio=waterContext.shadowRatio=shadowRatio;
    // The static precull's one world-space frustum.
    C3D_Mtx identity;Mtx_Identity(&identity);
    const ClipFrustum worldView=view.model(identity);
    for(std::size_t i=0;i<visuals.size();++i){
        auto& v=visuals[i];if(skyObject(v))continue;
        auto& draw=waterObject(v)?waterContext:context;
        // Retail clips at the object's own position: Kinoko's for objects it
        // moves, else where the presenter last drew it, else its placement.
        const auto* p=v.object&&(v.kinetic||!v.drawnValid)?&v.object->pos():nullptr;
        const C3D_Mtx& m=v.drawnValid?v.drawn:v.placement;
        // The cart manager stays at its placement while its carts run the whole
        // mine: clipping them all there hid every cart far from that point.
        if(partsClipped(v)){draw.partLimit=drawDistances[i];draw.partScale=distanceScale;}
        else draw.hidden=beyond(p?Vec3{p->x,p->y,p->z}:Vec3{m.r[0].w,m.r[1].w,m.r[2].w},drawDistances[i]);
        if(draw.hidden)diag::tallyHidden(v.resource);
        else if(staticPrecull&&i<precull.size()&&precull[i].eligible){
            C3D_Mtx m;
            if(singleMatrix(v,m)){
                auto& c=precull[i];
                if(!c.valid||std::memcmp(&m,&c.matrix,sizeof m)){
                    // A superset of the box at m: rounding of the corners, padded.
                    float rows[12];for(unsigned r=0;r<3;++r)for(unsigned col=0;col<4;++col)rows[r*4+col]=m.r[r].c[3-col];
                    Bounds b=transformBounds(c.local,rows);
                    const float extent=std::max({std::fabs(b.minimum.x),std::fabs(b.minimum.y),std::fabs(b.minimum.z),std::fabs(b.maximum.x),std::fabs(b.maximum.y),std::fabs(b.maximum.z)});
                    const float pad=1e-4f*extent+.01f;
                    b.minimum=b.minimum-Vec3{pad,pad,pad};b.maximum=b.maximum+Vec3{pad,pad,pad};
                    c.matrix=m;c.world=b;c.valid=true;
                }
                ++precullStats.tested;
                if(worldView.clearlyOutside(c.world,1e-3f)){
                    ++precullStats.culled;
                    if(precullCheck){
                        // DIAG precull-check.txt: the presenter anyway; it must add nothing.
                        const std::size_t records=draw.list.records.size(),volumes=shadows.size();
                        drawObject(draw,v);
                        if(draw.list.records.size()!=records||shadows.size()!=volumes)++precullStats.violations;
                    }
                    continue;
                }
            }
        }
        drawObject(draw,v);draw.hidden=false;draw.partLimit=0;
    }
    // Retail owns one pochaYogan model per player and restarts all three
    // non-looping tracks together. Keep the authored terminal pose rather
    // than inventing a timer that could truncate CLR/SRT or the mesh motion.
    const auto splash=assets.tracks(assets.find("pochaYogan","pochaYogan"));
    if(splash.model)for(const auto& state:lavaSplashes){
        if(!state.active||tick<state.start)continue;
        const auto matrix=basisMatrix({1,0,0},{0,1,0},{0,0,1},state.position,{state.scale,state.scale,state.scale});
        waterContext.submit(splash.model,matrix,animAt(splash,float(tick-state.start)));
    }
    if(objectSetLog)logObjectSet();
}
void CourseObjects::Impl::logObjectSet(){
    // FNV-1a over each record's kind, pose, flags, model size and matrix, and
    // each shadow volume's model size and matrix: the frame's drawn object set.
    uint32_t h=2166136261u;
    auto mix=[&](const void* data,std::size_t bytes){const auto* b=static_cast<const uint8_t*>(data);for(std::size_t k=0;k<bytes;++k){h^=b[k];h*=16777619u;}};
    for(const auto* list:{&objects,&water})for(const auto& r:list->records){
        const uint32_t words[]={uint32_t(r.kind),uint32_t(r.pose),uint32_t(r.backdrop),r.overrideCount,r.bones,r.target?r.target->indexCount:0u,r.target?r.target->vertexCount:0u};
        mix(words,sizeof words);mix(&r.transform,sizeof r.transform);
    }
    for(const auto& v:shadows){const uint32_t n=v.model?v.model->indexCount:0u;mix(&n,sizeof n);mix(&v.transform,sizeof v.transform);}
    char line[96];std::snprintf(line,sizeof line,"%u,%u,%u,%u,%08lx\n",tick,unsigned(objects.records.size()),unsigned(water.records.size()),unsigned(shadows.size()),(unsigned long)h);
    objectSetLines+=line;
    if(objectSetLines.size()>8192){
        if(FILE* f=std::fopen("sdmc:/3ds/mkw-3ds/object-set.csv","a")){std::fputs(objectSetLines.c_str(),f);std::fclose(f);}
        objectSetLines.clear();
    }
}
CourseObjects::PrecullStats CourseObjects::precullTotals(){return precullStats;}
void CourseObjects::Impl::replay(Renderer& renderer,const ObjectDrawList& list,unsigned phase){
    // Bindings a later draw could still see (overrides 1, rigid pose 2, skin palette 4):
    // all of them until the first record resets them, then only what a record set.
    unsigned bound=7;
    for(const auto& r:list.records){
        if(r.kind==ObjectDrawRecord::Explosion){
            if(phase!=1)blast::drawBlast(renderer,*r.target,r.transform,unsigned(std::max(0.f,r.frame)),false);
            continue;
        }
        // Normal world DrawOpa/DrawXlu passes: every model uses its authored
        // material flags. No object-name, firebar or distance exceptions.
        struct Context {const Model* model;unsigned phase;};
        const Context context{r.target,r.backdrop?0:phase};
        auto visible=[](void* data,std::size_t i){
            const auto& c=*static_cast<const Context*>(data);
            return c.phase==0||bool(c.model->submeshes[i].flags&2)==(c.phase==2);
        };
        if(r.backdrop&&phase==2)continue;
        // A phase with no contributing material needs no draw command, pose
        // bindings or repeated bone-uniform upload (common for treehouse props).
        if(context.phase&&std::none_of(r.target->submeshes.begin(),r.target->submeshes.end(),
            [&](const auto& sub){return sub.count&&bool(sub.flags&2)==(context.phase==2);}))continue;

        if(r.kind==ObjectDrawRecord::Plain){renderer.draw(*r.target,r.transform,false,nullptr,context,visible);continue;}
        RigidPose pose;
        if(r.pose==1){pose.parts=r.parts;pose.palette=list.palettes.data()+r.palette;pose.stride=12;pose.matrices=r.bones;renderer.setRigidPose(&pose);}
        else if(r.pose==2)renderer.setSkinPalette(list.palettes.data()+r.palette,r.bones,12);
        if(r.overrideCount)renderer.setOverrides(list.overrides.data()+r.overrides,r.overrideCount);
        renderer.draw(*r.target,r.transform,r.backdrop,nullptr,context,visible);
        // Reset only what may still be bound: an empty reset is a recorded command too.
        bound|=(r.overrideCount?1u:0u)|(r.pose==1?2u:0u)|(r.pose==2?4u:0u);
        if(bound&1)renderer.setOverrides(nullptr,0);
        if(bound&2)renderer.setRigidPose(nullptr);
        if(bound&4)renderer.setSkinPalette(nullptr,0,12);
        bound=resetSkip?0:7;
    }
}
void CourseObjects::prepare(const Renderer& renderer,unsigned tick,Vec3 camera){
    auto& d=*impl_;
    d.begin(renderer,tick,camera);
    d.skyJob.reset([](void* c){static_cast<Impl*>(c)->recordSky();},&d);
    d.objectJob.reset([](void* c){static_cast<Impl*>(c)->recordObjects();},&d);
    auto& worker=jobWorker();
    // Recorded right here on a slow worker: the object draws follow the course
    // draw, too soon for a fraction of a core to have finished them.
    d.queued=worker.running()&&!worker.slow();
    if(d.queued){worker.submit(d.skyJob);worker.submit(d.objectJob);}
    else{d.recordSky();d.recordObjects();}
}
void CourseObjects::requestStadiumScreen(Renderer& renderer){
    auto& d=*impl_;
    // WLscreenGC is a stationary KMP placement. Keep both its local face and
    // placed world bounds for the scene; visibility only queries these AABBs.
    // link() invalidates them on restart, race change and graphics resume.
    if(d.ready()&&!d.stadiumBoundsReady){
        d.stadiumScreens.clear();
        for(const auto& v:d.visuals){
            const auto* asset=v.main.model;
            if(!asset||!asset->hasStadiumFace)continue;
            const auto m=v.kinetic&&v.object?objectMatrix(v.object->transform(),v.object->scale()):v.placement;
            float rows[12];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=m.r[r].c[3-c];
            d.stadiumScreens.push_back(transformBounds(asset->stadiumFace,rows));
        }
        d.stadiumBoundsReady=true;
    }
    // Unknown/missing placement bounds remain conservative: capture normally.
    renderer.requestStadiumScreen(d.stadiumScreens.data(),unsigned(d.stadiumScreens.size()));
}
void CourseObjects::draw(Renderer& renderer,unsigned tick,Vec3 camera){
    auto& d=*impl_;
    if(!d.recorded){d.begin(renderer,tick,camera);d.recordSky();d.recordObjects();}
    else if(d.queued){
        // The object pass may run here only once the sky pass has finished.
        jobWorker().finish(d.skyJob);jobWorker().finish(d.objectJob);d.queued=false;
    }
    // Resolve the ground-shadow volumes before their casters. These authored
    // volumes intersect the body (Chomp, Cataquack, crowds); resolving against
    // the body's depth stamps a hard dark band onto the actor itself.
    if(d.linkedReady)renderer.drawShadowVolumes(d.shadows.data(),unsigned(d.shadows.size()),.5f);
    d.replay(renderer,d.objects,1);
    d.recorded=false;
}

void CourseObjects::drawWater(Renderer& renderer){
    // draw() has joined the object job. Keep the recorded materials and pose
    // at that same tick; no second simulation or recording is needed.
    impl_->replay(renderer,impl_->water);
    impl_->replay(renderer,impl_->objects,2);
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
void CourseObjects::drawSky(Renderer& renderer,unsigned tick,Vec3 camera){
    auto& d=*impl_;
    if(!d.recorded){d.begin(renderer,tick,camera);d.recordSky();d.recordObjects();}
    else if(d.queued)jobWorker().finish(d.skyJob);
    d.replay(renderer,d.sky);
}
void ObjectDraw::shadow(ObjModel* m,const C3D_Mtx& transform,const Anim& anim){
    if(hidden||!renderQuality().shadowVolumes)return;
    if(!m||!m->model||!view.model(transform).intersects(m->bounds))return;
    // Before the skin pass below, so a skipped skinned volume is not posed either.
    if(RenderQuality::shadowTooSmall(m->bounds,transform,camera,shadowRatio))return;
    if(anim.clip>=0&&m->bank&&m->cpuSkin){
        // Reuse the deferred per-instance skin path, then move its posed
        // mesh into the stencil pass instead of the colour draw list.
        const auto before=list.records.size();
        submit(m,transform,anim);
        if(list.records.size()>before){
            shadows_.push_back({list.records.back().target,transform});
            list.records.pop_back();
        }
        return;
    }
    shadows_.push_back({m->model.get(),transform});
}
void ObjectDraw::explosion(const C3D_Mtx& transform,float frame){
    if(hidden||!assets.explosion||!assets.explosionBank)return;
    // Every simultaneous blast needs its own vertex destination: deferred
    // pose writes execute before draws, so a shared destination kept only the
    // last blast's age. Reuse these copies between frames like object skins.
    if(assets.explosionUsed==assets.explosionPoses.size()){
        auto copy=cloneModel(*assets.explosion);if(!copy)return;
        assets.explosionPoses.push_back(std::move(copy));
    }
    ObjectDrawRecord r;r.kind=ObjectDrawRecord::Explosion;r.frame=frame;r.transform=transform;
    auto& pose=*assets.explosionPoses[assets.explosionUsed++];
    assets.explosionBank->apply(pose,0,frame,tick);
    r.target=&pose;
    list.records.push_back(r);
}
void ObjectDraw::plain(const Model& model,const C3D_Mtx& transform){
    if(hidden)return;
    ObjectDrawRecord r;r.kind=ObjectDrawRecord::Plain;r.target=&model;r.transform=transform;
    list.records.push_back(r);
}
// One cooked model at transform with the given pose/material tracks: posed,
// skinned and its materials evaluated now, drawn when the list is replayed.
namespace {
struct FireTextureWrite {Texture* texture;unsigned bytes;};
void writeFireTexture(void* data){
    auto& w=*static_cast<FireTextureWrite*>(data);
    std::memcpy(w.texture->gpu.data,&w+1,w.bytes);C3D_TexFlush(&w.texture->gpu);
}
}
void ObjectDraw::submit(ObjModel* m,const C3D_Mtx& transform,const Anim& source){
    if(hidden||!m||!m->model)return;
    // Reduced quality: crowds step their clip (one frame per tick) every
    // animationStep frames, so a skinned pose repeats instead of being skinned again.
    Anim anim=source;const unsigned budget=animationStep();
    // Never decimate procedural palettes (moving platforms, billboards,
    // suspension/collision-linked deformation). Crowd clips are decorative.
    if(budget>1&&m->crowd&&!anim.palette&&anim.clip>=0)anim.frame=std::floor(anim.frame/budget)*budget;
    const float step=float(renderQuality().animationStep);
    if(m->crowd&&step>0&&anim.clip>=0&&!anim.palette)anim.frame=std::floor(anim.frame/step)*step;
    if(!view.model(transform).intersects(anim.bounds?*anim.bounds:m->bounds))return;
    if(renderQuality().tooSmall(anim.bounds?*anim.bounds:m->bounds,transform,camera,stereo))return;
    const ObjectBank* bank=m->bank.get();
    // GPU-skinned meshes hold bone-local positions: unanimated, they draw the bind pose.
    const bool posed=bank&&(anim.palette||anim.clip>=0||m->rigid||m->gpuSkin);
    const float* palette=nullptr;
    if(posed){
        palette_.resize(size_t(bank->matrixCount())*12);
        if(anim.palette)palette=anim.palette;
        else{
            if(m->budgetClip==anim.clip&&m->budgetFrame==anim.frame&&m->budgetPalette.size()==palette_.size()){
                std::copy(m->budgetPalette.begin(),m->budgetPalette.end(),palette_.begin());++renderMetrics.scenePoseReuses;
            }else{bank->pose(anim.clip,anim.frame,palette_.data());m->budgetPalette=palette_;m->budgetClip=anim.clip;m->budgetFrame=anim.frame;++renderMetrics.scenePoseSamples;}
            palette=palette_.data();
        }
        if(!m->miiBillboards.empty()){
            // Head planes retain their own animated origins and scales. Never
            // turn the complete crowd/car model towards the viewer.
            if(palette!=palette_.data())std::copy_n(palette,size_t(bank->matrixCount())*12,palette_.data());
            palette=palette_.data();
            C3D_Mtx mv;Mtx_Multiply(&mv,&view.view,&transform);
            float rows[12];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)rows[r*4+c]=mv.r[r].c[3-c];
            for(const auto& node:m->miiBillboards)billboardMiiJoint(palette_.data()+node.slot*12,rows,node.mode);
            // Camera-dependent palettes cannot share a CPU skin by clip/frame.
            anim.palette=palette;
        }
    }
    ObjectDrawRecord r;r.backdrop=backdrop;r.transform=transform;
    const float cameraDistance=length(Vec3{transform.r[0].w,transform.r[1].w,transform.r[2].w}-camera);
    const Model* target=m->model.get();
    auto keepPalette=[&](){r.palette=unsigned(list.palettes.size());list.palettes.insert(list.palettes.end(),palette,palette+size_t(bank->matrixCount())*12);};
    if(posed&&m->rigid){r.pose=1;r.parts=&bank->rigidParts();r.bones=bank->matrixCount();keepPalette();}
    else if(posed&&m->gpuSkin){r.pose=2;r.bones=bank->matrixCount();keepPalette();renderMetrics.skinnedVertices+=m->model->vertexCount;diag::tallySkin(m->resource,m->model->vertexCount,true,cameraDistance);}
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
            // out of the measured overlay reserve in LINEAR memory. Past that an
            // instance reuses a pose already skinned this frame.
            const size_t bytes=m->model->vertexCount*sizeof(Vertex)+m->model->indexCount*sizeof(uint16_t);
            auto* deferred=deferredWrites();
            // directWrites: a copy no frame after serial-2 drew (a new one if
            // none is left), skinned when the render thread starts this frame.
            bool direct=false;
            if(deferred&&deferred->preparing()&&DeferredWrites::directWrites){
                const unsigned serial=assets.serial;int idle=-1;
                for(unsigned i=m->skinUsed;i<m->skins.size()&&idle<0;++i){const unsigned d=m->skinKeys[i].drawn;if(!d||d+2<=serial)idle=int(i);}
                if(idle<0&&linearSpaceFree()>allocationReserve+bytes)
                    if(auto fresh=cloneModel(*m->model)){m->skins.push_back(std::move(fresh));m->skinKeys.push_back({});idle=int(m->skins.size()-1);}
                if(idle>=0){std::swap(m->skins[idle],m->skins[m->skinUsed]);std::swap(m->skinKeys[idle],m->skinKeys[m->skinUsed]);direct=true;}
            }
            // Otherwise the previous frame's copy is written on the hook, as before.
            if(!direct&&m->skinUsed>=m->skins.size()&&linearSpaceFree()>allocationReserve+bytes)
                if(auto fresh=cloneModel(*m->model)){m->skins.push_back(std::move(fresh));m->skinKeys.push_back({});}
            if(m->skinUsed<m->skins.size()){
                copy=int(m->skinUsed++);m->skinKeys[copy]={anim.palette?-1:anim.clip,anim.frame};
                if(deferred){
                    const std::size_t bytes=std::size_t(bank->matrixCount())*12*sizeof(float);
                    // The skinning arithmetic runs when the render thread starts the frame; the hook stores it.
                    const std::size_t skinnedBytes=deferred->preparing()?std::size_t(bank->skinnedCount(*m->skins[copy],m->litNormals,anim.clip))*sizeof(Vec3):0;
                    auto* w=static_cast<SkinWrite*>(deferred->allocate(sizeof(SkinWrite)+bytes+skinnedBytes));
                    *w={bank,m->skins[copy].get(),m->litNormals,anim.clip,anim.frame,
                        skinnedBytes?reinterpret_cast<Vec3*>(reinterpret_cast<uint8_t*>(w+1)+bytes):nullptr};std::memcpy(w+1,palette,bytes);
                    if(direct)deferred->prepare(directSkin,w,"object cpu skin");
                    else{
                        if(skinnedBytes)deferred->prepare(prepareSkin,w,"object cpu skin");
                        deferred->queue(writeSkin,w,"object cpu skin");
                    }
                }else bank->skin(palette,*m->skins[copy],m->litNormals,anim.clip,anim.frame);
                renderMetrics.skinnedVertices+=m->skins[copy]->vertexCount;diag::tallySkin(m->resource,m->skins[copy]->vertexCount,false,cameraDistance);
            }else if(m->skinUsed)copy=int(m->skinUsed-1);
            else return;
        }
        m->skinKeys[copy].drawn=assets.serial;
        target=m->skins[copy].get();
        if(m->resource=="quicksand"&&anim.bounds){
            m->skins[copy]->minimum=anim.bounds->minimum;
            m->skins[copy]->maximum=anim.bounds->maximum;
        }
    }
    diag::nameModel(target,m->resource);
    bool material=false;
    if(bank&&!m->overrides.empty()&&(anim.pat>=0||anim.srt>=0||anim.clr>=0||anim.opacity!=1||anim.mirrorU)){
        material=bank->material(anim.pat,anim.patFrame,anim.srt,anim.srtFrame,anim.clr,anim.clrFrame,m->overrides.data(),unsigned(m->overrides.size()));
        for(auto& o:m->overrides){
            if(anim.opacity!=1){o.tint[3]*=anim.opacity;o.flags|=SubmeshOverride::Tint;material=true;}
            if(anim.mirrorU){
                // BGteresa's authored SRT is identity: negate its U basis,
                // including the second sampled layer if one exists.
                for(unsigned base:{0u,6u}){o.uv[base]*=-1;o.uv[base+3]*=-1;}
                o.flags|=SubmeshOverride::Uv;material=true;
            }
        }
    }
    if(m->firePhases&&!m->overrides.empty()){
        const unsigned phase=m->firePhases->phase((unsigned(std::max(0.f,anim.srtFrame))/budget)*budget);
        bool ready=int(phase)==m->firePhase;
        if(!ready){
            auto& texture=*m->model->textures[m->fireTexture];
            auto* deferred=deferredWrites();
            // A GX copy from the stream's cache after the previous frame, not a CPU write on the hook.
            if(deferred&&deferred->upload(texture.gpu.data,m->firePhases->frame(phase),m->firePhases->frameBytes,m->fireStaging.half,"fire texture"))ready=true;
            else if(deferred){
                auto* write=static_cast<FireTextureWrite*>(deferred->allocate(sizeof(FireTextureWrite)+m->firePhases->frameBytes));
                *write={&texture,m->firePhases->frameBytes};
                ready=m->firePhases->read(phase,write+1,write->bytes);
                if(ready)deferred->queue(writeFireTexture,write,"fire texture");
            }else{
                ready=m->firePhases->read(phase,texture.gpu.data,texture.gpu.size);
                if(ready)C3D_TexFlush(&texture.gpu);
            }
            if(ready){m->firePhase=int(phase);++renderMetrics.fireReads;}
            else std::fprintf(stderr,"fireBPlane phase %u read failed\n",phase);
        }
        if(ready)for(auto& o:m->overrides){
            m->firePhases->mapUv(o.uv);o.texture[0]=m->fireTexture;
            o.flags|=SubmeshOverride::Uv|SubmeshOverride::Texture;material=true;
        }
    }
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
}
