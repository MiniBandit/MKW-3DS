#include <mkw/ceremony_scene.hpp>
#include <mkw/animation.hpp>
#include <mkw/garage.hpp>
#include <mkw/renderer.hpp>
#include <mkw/sound_ids.hpp>
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace mkw {
namespace {
// Retail character ids (Kinoko order) to the award models' codes.
constexpr const char* Codes[24]={"mr","bpc","wl","kp","bds","kk","bmr","lg","ko","dk","ys","wr",
                                 "blg","ka","nk","ds","pc","ca","dd","kt","jr","bk","fk","rs"};
constexpr const char* Cups[8]={"kinoko","flower","star","special","koura","banana","konoha","thunder"};
// Podium (world space, as the stands are authored): the winner on the tall
// stand at the centre, second on the camera's left, third on its right. The
// stand tops are at y 1364 (first) and 1304 (the others).
struct Spot {float x,y,z;const char* stand;float standX;};
constexpr Spot Spots[3]={{0,1364,27850,nullptr,0},{-290,1304,27755,nullptr,0},{290,1304,27755,nullptr,0}};
// The losing stage's first start point (KMP KTPT 0) and its heading.
constexpr float LoseX=-10604.2f,LoseY=5488.3f,LoseZ=51922.2f,LoseYaw=167.f;
constexpr unsigned HoldFrames=300; // the camera's last frame stays this long

struct Camera {
    std::vector<float> frames; // 13 floats a frame (tools/ceremony/export_scene.ts)
    unsigned count=0;
    bool load(const char* name){
        char path[96];std::snprintf(path,sizeof path,"models/ceremony/%s.scam",name);
        FILE* f=std::fopen(path,"rb");if(!f)return false;
        char magic[4];uint32_t header[2];
        const bool ok=std::fread(magic,1,4,f)==4&&!std::memcmp(magic,"SCAM",4)&&std::fread(header,4,2,f)==2&&header[0]==1&&header[1]>0&&header[1]<20000;
        if(ok){count=header[1];frames.resize(size_t(count)*13);}
        const bool read=ok&&std::fread(frames.data(),sizeof(float),frames.size(),f)==frames.size();
        std::fclose(f);return read;
    }
    const float* at(unsigned frame)const{return frames.data()+size_t(std::min(frame,count-1))*13;}
};
std::unique_ptr<Model> loadModel(const char* name){
    char path[96];std::snprintf(path,sizeof path,"models/%s.gmdl",name);
    auto m=std::make_unique<Model>();
    if(!m->load(path))return nullptr;
    m->enableStaticCulling();
    return m;
}
bool loadList(const char* list,std::vector<std::unique_ptr<Model>>& parts){
    FILE* f=std::fopen(list,"r");if(!f)return false;
    char name[64];bool ok=true;
    while(std::fscanf(f,"%63s",name)==1){
        if(std::strchr(name,'/')||std::strstr(name,"..")){ok=false;break;}
        auto m=loadModel(name);if(!m){ok=false;break;}
        parts.push_back(std::move(m));
    }
    std::fclose(f);return ok&&!parts.empty();
}
// The award models and trophies are converted with their shading baked into the
// vertex colours (tools/cook_ceremony.py cook_static): draw them at full
// brightness, as race drivers, not with the legacy 0.35 floor for unlit parts.
void fullBright(Model& model){for(auto& s:model.submeshes)s.flags|=8;}
void place(C3D_Mtx& m,float x,float y,float z,float yawDegrees){
    Mtx_Identity(&m);Mtx_Translate(&m,x,y,z,true);Mtx_RotateY(&m,yawDegrees*float(M_PI)/180.f,true);
}
}

const char* ceremonyCharacterCode(unsigned character,bool bike){
    if(character>=24)return "mr";
    if(bike&&character==15)return "ds3";
    if(bike&&character==16)return "pc3";
    if(bike&&character==23)return "rs3";
    return Codes[character];
}

struct CeremonyScene::Impl {
    struct Actor {
        std::unique_ptr<Model> model;
        std::unique_ptr<DriverAnimation> bank;
        int clip=-1;
        C3D_Mtx matrix{};
    };
    CeremonySetup setup;
    std::vector<std::unique_ptr<Model>> stage,sky,stands;
    std::vector<C3D_Mtx> standMatrices;
    std::unique_ptr<Model> trophy;
    std::vector<Actor> actors;
    Camera camera;
    unsigned frame=0;
    bool loadActor(const CeremonyDriver& driver,const char* clipName,float x,float y,float z,float yaw){
        Actor a;const char* code=ceremonyCharacterCode(driver.character,driver.bike);
        char path[96];std::snprintf(path,sizeof path,"models/award_%s.dskm",code);
        // The award models' conversion lost their per-part texture choice; most share
        // the race driver model's mesh exactly, so that textured model is posed by the
        // award bank (its own vertex positions). Otherwise the award model as it is.
        for(const auto& choice:GarageChoices)if(choice.character==driver.character&&choice.bike==driver.bike){
            char name[64];std::snprintf(name,sizeof name,"%sdriver",choice.prefix);
            auto model=loadModel(name);auto bank=std::make_unique<DriverAnimation>();
            if(model&&bank->load(path,*model)){a.model=std::move(model);a.bank=std::move(bank);}
            break;
        }
        if(!a.model){
            char name[48];std::snprintf(name,sizeof name,"award_%s",code);
            a.model=loadModel(name);if(!a.model)return false;
            fullBright(*a.model);
            a.bank=std::make_unique<DriverAnimation>();
            if(!a.bank->load(path,*a.model))return false;
        }
        a.bank->prepareLitSkinning(*a.model);
        // As race opponents: the scene's lighting (set before loading) fixed into the
        // model, the bank's skinned vertices shared for drawing.
        a.model->freezeLighting(a.bank->gpuSkinned()?a.bank->palette():nullptr);
        if(a.bank->gpuSkinned()){a.bank->shareDrawVertices(*a.model);a.model->enableSkinCulling(a.bank->matrixCount());}
        a.clip=a.bank->find(clipName);if(a.clip<0)return false;
        place(a.matrix,x,y,z,yaw);
        actors.push_back(std::move(a));return true;
    }
};

CeremonyScene::CeremonyScene():impl_(std::make_unique<Impl>()){}
CeremonyScene::~CeremonyScene()=default;

bool CeremonyScene::load(const CeremonySetup& setup){
    release();auto& m=*impl_;m.setup=setup;
    const bool podium=setup.podium;
    if(!loadList(podium?"models/course_55.list":"models/course_56.list",m.stage))return false;
    loadList(podium?"models/course_55_sky.list":"models/course_56_sky.list",m.sky); // optional
    if(podium){
        // The podium (v_stand2: second, first and third blocks) and the winner's raised block (v_stand1).
        for(const char* name:{"award_v_stand2","award_v_stand1"}){
            auto stand=loadModel(name);if(!stand)return false;
            C3D_Mtx t;Mtx_Identity(&t);m.stands.push_back(std::move(stand));m.standMatrices.push_back(t);
        }
        for(unsigned i=0;i<3;++i)if(!m.loadActor(setup.top[i],"demowin",Spots[i].x,Spots[i].y,Spots[i].z,0))return false;
        char name[32];std::snprintf(name,sizeof name,"trophy_%s",Cups[std::min(setup.cup,7u)]);
        m.trophy=loadModel(name); // shown in front of the podium when present
        if(m.trophy){
            m.trophy->freezeLighting();fullBright(*m.trophy);
            // Its metal is reflection-only (untextured): gold, keeping the baked shading.
            for(unsigned i=0;i<m.trophy->vertexCount;++i){auto& c=m.trophy->vertices[i].color;
                c[1]=uint8_t(c[1]*200/255);c[2]=uint8_t(c[2]*70/255);}
            GSPGPU_FlushDataCache(m.trophy->vertices,m.trophy->vertexCount*sizeof(Vertex));
        }
        if(!m.camera.load("demowin03"))return false;
    }else{
        if(!m.loadActor(setup.player,"demooth",LoseX,LoseY,LoseZ,LoseYaw))return false;
        if(!m.camera.load("demolos01_16vs9"))return false;
    }
    m.frame=0;return true;
}
void CeremonyScene::release(){
    auto& m=*impl_;
    m.actors.clear();m.trophy.reset();m.stands.clear();m.standMatrices.clear();m.sky.clear();m.stage.clear();
    m.camera=Camera{};m.frame=0;
}
bool CeremonyScene::step(){
    auto& m=*impl_;
    if(!m.camera.count)return false;
    ++m.frame;
    return m.frame<m.camera.count+HoldFrames;
}
bool CeremonyScene::skippable()const{return impl_->frame>120;}
unsigned CeremonyScene::music()const{return impl_->setup.podium?snd::id::STRM_O_GP_AWARD:snd::id::STRM_O_AWARD_LOSE;}

void CeremonyScene::draw(Renderer& renderer){
    auto& m=*impl_;
    if(!m.camera.count)return;
    // SCN0 rotation camera: position, then pitch (positive looks up), yaw and roll
    // in degrees; facing -Z at zero.
    const float* c=m.camera.at(m.frame);
    const float pitch=c[3]*float(M_PI)/180.f,yaw=c[4]*float(M_PI)/180.f;
    const Vec3 eye{c[0],c[1],c[2]};
    const Vec3 forward{-std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};
    renderer.setFov(c[10],std::max(c[11],10.f),std::max(c[12],100000.f));
    renderer.begin(eye,eye+forward*1000.f,{0,1,0});
    C3D_Mtx identity;Mtx_Identity(&identity);
    // As a race: the stage with the course's lighting, the actors with their own.
    renderer.setCourseLighting(true);
    for(auto& part:m.sky)renderer.draw(*part,identity,true);
    for(auto& part:m.stage)renderer.draw(*part,identity);
    renderer.setCourseLighting(false);
    for(size_t i=0;i<m.stands.size();++i)renderer.draw(*m.stands[i],m.standMatrices[i]);
    for(auto& a:m.actors){
        const float duration=a.bank->duration(unsigned(a.clip));
        const float t=duration>1?(a.bank->loop(unsigned(a.clip))?std::fmod(float(m.frame),duration):std::min(float(m.frame),duration-1)):0;
        a.bank->apply(*a.model,a.clip,t,m.frame);
        if(a.bank->gpuSkinned())renderer.setSkinPalette(a.bank->palette(),a.bank->matrixCount(),21);
        renderer.draw(*a.model,a.matrix);
        renderer.setSkinPalette(nullptr,0,12);
    }
    if(m.trophy){
        // Turning slowly on the ground in front of the podium.
        C3D_Mtx t;place(t,0,1224,28300,float(m.frame)*.6f);Mtx_Scale(&t,.6f,.6f,.6f);
        renderer.draw(*m.trophy,t);
    }
}
}
