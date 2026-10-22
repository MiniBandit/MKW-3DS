#include "mkw/course_effects.hpp"
#include "mkw/asset_pack.hpp"
#include "mkw/kart_drift_effects.hpp"
#include "mkw/kart_effect_lod.hpp"
#include "mkw/race_effects.hpp"
#include "mkw/render_quality.hpp"
#include "mkw/renderer.hpp"
#include <game/field/ObjectDirector.hh>
#include <game/field/RailManager.hh>
#include <game/field/obj/ObjectFlamePoleV.hh>
#include <game/field/obj/ObjectKoopaFigure64.hh>
#include <game/field/obj/ObjectPropeller.hh>
#include <game/field/obj/ObjectVolcanoBall.hh>
#include <game/field/obj/ObjectVolcanoBallLauncher.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/system/CourseMap.hh>
#include <game/system/RaceConfig.hh>
#include <game/system/RaceManager.hh>
#include <game/system/ResourceManager.hh>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace mkw {
namespace K=Kinoko;
namespace {
using K::Field::ObjectId;
// EffectSystem group of every effect created here. Retail creates object effects in group
// 0 (EGG::Effect ctor 0x80222CCC, creator 0); in the port groups only mask calc and draw
// (KartFxSlots uses 2 + racer, nothing clears bit 1), so group 1 behaves as group 0 and
// tells these handles apart from RaceEffects' slots in the shared pools.
constexpr uint8_t Group=1;

// REFX v3 (tools/cook_race_effects.py), as RaceEffects::loadMemory reads it.
struct Head{char magic[4];uint32_t version,emitters,textures,blob;};
struct Rec{char name[32];uint32_t desc,ptcl;uint16_t texture[3],ptclTracks,ptclInit,emitTracks,emitInit,ptclSize,pad[2];uint32_t list;};
static_assert(sizeof(Head)==20&&sizeof(Rec)==64,"REFX v3");
constexpr size_t TexBytes=40;
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
uint16_t be16(const uint8_t* p){return uint16_t(p[0]<<8|p[1]);}
bool header(const std::vector<uint8_t>& f,Head& h){
    if(f.size()<sizeof h)return false;
    std::memcpy(&h,f.data(),sizeof h);
    return !std::memcmp(h.magic,"REFX",4)&&h.version==3&&f.size()==sizeof h+size_t(h.textures)*TexBytes+size_t(h.emitters)*sizeof(Rec)+h.blob;
}
// An asset from race.pak, else the loose SD file (the REFX tables and the archive index are loose).
bool readFile(const char* path,std::vector<uint8_t>& out,size_t limit){
    if(pack::read(path,out))return out.size()<=limit;
    FILE* f=std::fopen(path,"rb");
    if(!f)return false;
    const long size=std::fseek(f,0,SEEK_END)==0?std::ftell(f):-1;
    bool ok=size>0&&size_t(size)<=limit&&std::fseek(f,0,SEEK_SET)==0;
    if(ok){out.resize(size_t(size));ok=std::fread(out.data(),1,out.size(),f)==out.size();}
    std::fclose(f);
    return ok;
}
// The full texture names of one table, in table order, from models/effect-archives.json:
// the table keeps 31 characters of each, and several of one archive share those.
bool tableTextures(const std::string& json,const std::string& table,std::vector<std::string>& out){
    out.clear();
    size_t at=json.find('"'+table+'"');
    if(at==std::string::npos||(at=json.find("\"textures\"",at))==std::string::npos||(at=json.find('[',at))==std::string::npos)return false;
    const size_t end=json.find(']',at);
    if(end==std::string::npos)return false;
    for(size_t q=json.find('"',at);q<end;q=json.find('"',q)){
        const size_t close=json.find('"',q+1);
        if(close==std::string::npos||close>end)return false;
        out.emplace_back(json,q+1,close-q-1);
        q=close+1;
    }
    return true;
}

// Retail loads an object type's effects from the course's effect/<object>/ folder; the
// cooker keys each <course>_<file>. Only archives whose emission step() ports are listed.
struct ArchiveUse{uint16_t object;const char* file;};
constexpr ArchiveUse Archives[]={
    {0x212,"rk_flamepole"},  // FlamePole_v: rk_flamePole, rk_flamePoleS (ctor 0x806C3AA4)
    {0x216,"rk_flamepolel"}, // FlamePole_v_big: rk_flamePoleL
    {0x18b,"rk_koopafire"},  // KoopaFigure64: rk_koopaFire, rk_koopaFireL (ctor 0x806DA914)
    {0x0d6,"rk_volc"},       // VolcanoBall1 (ctor 0x806E2904)
    {0x1a6,"rk_epropeller"}, // Epropeller (ctor 0x80764CC8)
    {0x259,"rk_dkcannon"},   // DonkyCannonGC (calc 0x806BFED0)
    {0x261,"rk_dkcannon"},   // tree_cannon
};
constexpr uint16_t DonkyCannonGC=0x259,TreeCannon=0x261;

// One clip-manager entry for the top screen. Object_initRenderGroup (0x80821070) registers
// it (0x80787AB8 -> 0x80787C24) from the object's ObjFlow entry: +0x64 picks the init
// flag 0x10 and the area mode, +0x66 is the radius, +0x68 tenths of the far distance. The
// area mask is taken at registration (0x80786FC0, AREA type 8). The update (0x80787774)
// runs after the cameras.
struct Clip {
    float radius=0,far2=0;
    uint8_t mode=2;   // +0x1e: 2 no area test, 1 the registration mask, 0 the mask refreshed
    bool wide=false;  // flag 0x10: either side-plane pair passes, no top/bottom planes
    uint16_t mask=0;  // +0x1c: ids of the AREA type 8 boxes holding the object
    bool culled=true,beyondFar=true; // flag bits 0 and 1, registered as 3
    bool valid=false;
};
// AREA ids (+0x28) of the given type holding p (0x80786FC0): bit 1 << id, low 16 bits.
uint16_t areaMask(Vec3 p,int type){
    const auto* map=K::System::CourseMap::Instance();
    if(!map)return 0;
    uint16_t mask=0;
    for(uint16_t i=0;i<map->getAreaCount();++i){
        const auto* a=map->getArea(i);
        if(!a||static_cast<int>(a->type())!=type)continue;
        const int id=a->param(0);
        const uint16_t bit=id>=0&&id<16?uint16_t(1u<<id):0;
        if(bit&&!(mask&bit)&&a->test(K::EGG::Vector3f(p.x,p.y,p.z)))mask|=bit;
    }
    return mask;
}
Clip clipEntry(const K::Field::ObjectFlowTable& flow,uint16_t id,Vec3 registeredAt){
    Clip c;
    const auto* set=flow.set(flow.slot(static_cast<ObjectId>(id)));
    if(!set)return c;
    const int16_t kind=int16_t(be16(set->_62+2)),radius=int16_t(be16(set->_62+4)),far=int16_t(be16(set->_62+6));
    switch(kind){
    case 1:c.wide=false;c.mode=2;break;
    case 2:c.wide=true;c.mode=2;break;
    case 8:c.wide=false;c.mode=1;break;
    case 9:c.wide=false;c.mode=0;break;
    case 10:c.wide=true;c.mode=1;break;
    case 11:c.wide=true;c.mode=0;break;
    default:return c; // no entry
    }
    c.radius=float(radius);
    const float f=float(int(far)*10);
    c.far2=f*f;
    c.mask=areaMask(registeredAt,8);
    c.valid=true;
    return c;
}

// The shot clip's length: CHR0 <name> of <name>.brres in the course archive (loadAnims
// 0x806BFC48; init 0x806BFDDC keeps its frame count, calc stops the shot one frame short).
uint16_t chrFrames(const char* name){
    auto* resources=K::System::ResourceManager::Instance();
    if(!resources)return 0;
    char file[48];
    std::snprintf(file,sizeof file,"%s.brres",name);
    size_t size=0;
    const auto* d=static_cast<const uint8_t*>(resources->getFile(file,&size,K::System::ArchiveId::Course));
    if(!d||size<0x10||std::memcmp(d,"bres",4))return 0;
    // An index group: u32 size, u32 count, then count + 1 entries of 16 bytes whose name
    // and data offsets count from the group.
    auto entry=[&](uint32_t group,const char* want,uint32_t& out){
        if(group>size||size-group<8)return false;
        const uint32_t n=be32(d+group+4);
        for(uint32_t i=1;i<=n;++i){
            const uint32_t e=group+8+i*16;
            if(e>size||size-e<16)return false;
            const uint32_t name=group+be32(d+e+8);
            const size_t len=std::strlen(want);
            if(name<size&&size-name>len&&!std::memcmp(d+name,want,len+1)){out=group+be32(d+e+12);return out<size;}
        }
        return false;
    };
    const uint32_t root=be16(d+0xc);
    uint32_t folder=0,chr=0;
    if(root>size||size-root<8||std::memcmp(d+root,"root",4)||!entry(root+8,"AnmChr(NW4R)",folder)||!entry(folder,name,chr))return 0;
    // CHR0 version 5: the frame count is the u16 at +0x20.
    if(size-chr<0x24||std::memcmp(d+chr,"CHR0",4)||be32(d+chr+8)!=5)return 0;
    return be16(d+chr+0x20);
}

enum class Kind:uint8_t{Pole,Statue,Propeller,Ball,Cannon};
struct Handle{ef::Effect* effect=nullptr;uint16_t serial=0;};
struct Tracked {
    Kind kind;
    const K::Field::ObjectBase* object=nullptr;
    const K::System::MapdataGeoObj* geo=nullptr;
    Clip clip;
    std::array<Handle,3> fx{};
    uint32_t frame=480;    // statue in the kart-triggered mode: +0xc8 (init 0x806DAA44)
    bool hidden=false;     // ball: +0xf8, the clip result its calc stored a step before
    Vec3 start{};          // ball: its rail's first point (init 0x806E2C4C)
    int64_t shot=-1;       // cannon: the timer of the last shot start
    uint16_t shotFrames=0; // cannon: the shot clip's frame count
};
Vec3 vec(const K::EGG::Vector3f& v){return {v.x,v.y,v.z};}
// EGG::Effect update (0x80223F58): the matrix (identity unless setMtx), every axis times
// the setScale value, the setTranslate translation.
ef::Mtx34 at(Vec3 p,float s=1){return {{{s,0,0,p.x},{0,s,0,p.y},{0,0,s,p.z}}};}
ef::Mtx34 at(const K::EGG::Matrix34f& m,Vec3 p,float s){
    ef::Mtx34 out;
    for(int r=0;r<3;++r){for(int c=0;c<3;++c)out.m[r][c]=m[r,c]*s;}
    out.m[0][3]=p.x;out.m[1][3]=p.y;out.m[2][3]=p.z;
    return out;
}
}

struct CourseEffects::Impl {
    // KartDriftEffects' emitters race-effects.bin lacks, from common_rkrace_1 (loadRaceVfx).
    std::vector<uint8_t> drift;
    bool driftRead=false,baseKnown=false,merged=false;
    unsigned baseEmitters=0,baseTextures=0,textureCount=0;
    struct Ids{int flamePole=-1,flamePoleS=-1,flamePoleL=-1,koopaFire=-1,koopaFireL=-1,volcErupt=-1,volcBrk=-1,
               volcTail=-1,volcTailS=-1,volcFire=-1,volcFireS=-1,epropeller=-1,epropellerS=-1,dkCannon=-1;} ids;
    std::vector<Tracked> objects;
    unsigned generation=~0u;
    std::array<Vec3,12> kartPos{};
    bool kartPosValid=false,cannonStart=false,lookBack=false;
    uint16_t serial=0;
    char summary[192]="course fx: not loaded";

    // race-effects.bin plus the drift emitters, as loadRaceVfx built the live table.
    bool baseTable(std::vector<uint8_t>& out){
        Head h;
        if(!readFile("textures/race-effects.bin",out,2u<<20)||!header(out,h))return false;
        if(h.emitters!=baseEmitters){
            if(!driftRead){
                driftRead=true;
                std::vector<uint8_t> common,empty(sizeof(Head));
                const Head none{{'R','E','F','X'},3,0,0,0};
                std::memcpy(empty.data(),&none,sizeof none);
                if(readFile("textures/fx/common_rkrace/common_rkrace_1.refx",common,2u<<20)&&
                   appendRefxEmitters(empty,common,KartDriftEffects::Names,KartDriftEffects::FxCount))drift.swap(empty);
            }
            if(!drift.empty())appendRefxEmitters(out,drift,KartDriftEffects::Names,KartDriftEffects::FxCount);
        }
        return header(out,h)&&h.emitters==baseEmitters&&h.textures==baseTextures;
    }
    bool alive(const ef::System& s,const Handle& h) const {return s.alive(h.effect,h.serial)&&h.effect->group==Group;}
    // EGG::Effect create (vtable +0xc, 0x80222D90): a new instance unless one is running.
    void create(ef::System& s,Handle& h,int spec){
        if(spec<0||alive(s,h))return;
        h={};
        ef::Effect* e=s.createEffect(s.resources[size_t(spec)],0,Group);
        if(!e)return;
        // RaceEffects slots compare serials only and restart theirs at 0 every race; its
        // serials stay below 0x8000 within a race, so these never match a stale slot handle.
        e->serial=uint16_t(0x8000u|(++serial&0x7fffu));h={e,e->serial};
    }
    void burst(ef::System& s,int spec,const ef::Mtx34& m){
        Handle h;create(s,h,spec);
        if(alive(s,h))s.setRootMtx(*h.effect,m);
    }
    void move(ef::System& s,const Handle& h,const ef::Mtx34& m){if(alive(s,h))s.setRootMtx(*h.effect,m);}
    // Fade (+0x10, 0x80222E60): emitters stop, particles live on, the handle is released.
    void fade(ef::System& s,Handle& h){if(alive(s,h))s.retireEmitters(*h.effect);h={};}
    // Kill (+0x18, 0x80222EF4): emitters and particles retire.
    void kill(ef::System& s,Handle& h){if(alive(s,h))s.retireEffect(*h.effect);h={};}
    void rebuild();
    void stepObject(ef::System& s,Tracked& t,uint32_t timer,unsigned screens,bool timeTrial,unsigned players);
};

CourseEffects::CourseEffects():impl_(std::make_unique<Impl>()){}
CourseEffects::~CourseEffects()=default;

bool CourseEffects::load(RaceEffects& effects,Texture* textures,unsigned capacity,const char* archive){
    Impl& m=*impl_;
    unload(textures);
    m.objects.clear();m.generation=~0u;m.ids={};
    if(!effects.ready){std::snprintf(m.summary,sizeof m.summary,"course fx: race effects not loaded");return false;}
    if(!m.baseKnown){
        // Before the first merge the live table is loadRaceVfx's.
        m.baseEmitters=unsigned(effects.system().resources.size());m.baseTextures=unsigned(effects.textures.size());m.baseKnown=true;
    }
    // "Race/Course/<folder>.szs"
    const char* slash=std::strrchr(archive,'/');
    std::string folder=slash?slash+1:archive;
    if(const size_t dot=folder.rfind('.');dot!=std::string::npos)folder.resize(dot);
    // The archives of the object types placed on the course (KMP GOBJ).
    std::vector<std::string> keys;
    if(const auto* map=K::System::CourseMap::Instance())
        for(uint16_t i=0;i<map->getGeoObjCount();++i)
            if(const auto* geo=map->getGeoObj(i))
                for(const auto& use:Archives)
                    if(use.object==geo->id()){
                        std::string key=folder+'_'+use.file;
                        bool seen=false;for(const auto& k:keys)seen|=k==key;
                        if(!seen)keys.push_back(key);
                    }
    if(keys.empty()&&!m.merged){std::snprintf(m.summary,sizeof m.summary,"course fx %s: none",folder.c_str());return true;}
    std::vector<uint8_t> base;
    if(!m.baseTable(base)){std::snprintf(m.summary,sizeof m.summary,"course fx %s: race table rebuild failed",folder.c_str());return false;}
    std::vector<uint8_t> table=base;
    std::vector<std::string> paths,names;
    unsigned refused=0,missing=0;
    {
    std::string index;
    if(std::vector<uint8_t> json;!keys.empty()&&readFile("models/effect-archives.json",json,4u<<20))index.assign(json.begin(),json.end());
    for(const auto& key:keys)
        for(unsigned i=0;;++i){
            char path[160];
            std::snprintf(path,sizeof path,"textures/fx/%s/%s_%u.refx",key.c_str(),key.c_str(),i);
            if(index.find(std::string("\"")+path+'"')==std::string::npos){refused+=!i;break;}
            std::vector<uint8_t> extra;Head eh;
            if(!readFile(path,extra,2u<<20)||!header(extra,eh)||!tableTextures(index,path,names)||names.size()!=eh.textures||
               paths.size()+eh.textures>capacity-m.baseTextures||!appendRefxTable(table,extra)){
                ++refused;
                continue;
            }
            for(const auto& n:names)paths.push_back("textures/fx/"+key+'/'+n+".t3x");
        }
    }
    if(!effects.loadMemory(table.data(),table.size())){
        // Unreachable unless the cooked tables change: appendRefxTable checks loadMemory's limits.
        paths.clear();refused=unsigned(keys.size());
        if(!effects.loadMemory(base.data(),base.size())){
            std::snprintf(m.summary,sizeof m.summary,"course fx %s: race table reload failed (%s)",folder.c_str(),effects.loadError());
            return false;
        }
    }
    m.merged=effects.system().resources.size()>m.baseEmitters;
    for(const auto& p:paths)missing+=!textures[m.baseTextures+m.textureCount++].load(p.c_str());
    auto find=[&](const char* name){
        const auto& res=effects.system().resources;
        for(size_t i=m.baseEmitters;i<res.size();++i)if(!std::strcmp(res[i].name,name))return int(i);
        return -1;
    };
    m.ids={find("rk_flamePole"),find("rk_flamePoleS"),find("rk_flamePoleL"),find("rk_koopaFire"),find("rk_koopaFireL"),
           find("rk_volcErupt"),find("rk_volcBrk"),find("rk_volcTail"),find("rk_volcTailS"),find("rk_volcFire_L"),
           find("rk_volcFire_LS"),find("rk_epropeller"),find("rk_epropellerS"),find("rk_dkCannon")};
    std::snprintf(m.summary,sizeof m.summary,"course fx %s: %u archives (%u refused), %u emitters, %u textures (%u missing)",folder.c_str(),
                  unsigned(keys.size()),refused,unsigned(effects.system().resources.size()-m.baseEmitters),m.textureCount,missing);
    return !refused&&!missing;
}

const char* CourseEffects::summary() const {return impl_->summary;}

void CourseEffects::unload(Texture* textures){
    Impl& m=*impl_;
    for(unsigned i=0;i<m.textureCount;++i)textures[m.baseTextures+i].reset();
    m.textureCount=0;m.objects.clear();m.generation=~0u;
}

// Every object this file emits for, read from the scene's ObjectDirector. Runs on the first
// step after RaceEffects::reset (each race start), which also retired every handle.
void CourseEffects::Impl::rebuild(){
    objects.clear();kartPosValid=false;cannonStart=false;
    const auto* director=K::Field::ObjectDirector::Instance();
    if(!director)return;
    const auto& flow=director->flowTable();
    // A VolcanoBall1 placement registers its balls (loaded by the launcher's ctor) and then
    // the launcher: the placement's last object is the launcher.
    std::vector<std::pair<const K::System::MapdataGeoObj*,const K::Field::ObjectBase*>> launchers;
    for(const auto* obj:director->objects()){
        if(!obj||!obj->mapObj())continue;
        const auto* geo=obj->mapObj();
        const auto id=static_cast<uint16_t>(obj->id());
        Tracked t{};t.object=obj;t.geo=geo;
        if(obj->id()==ObjectId::FlamePoleV||obj->id()==ObjectId::FlamePoleVBig)t.kind=Kind::Pole;
        else if(obj->id()==ObjectId::KoopaFigure64)t.kind=Kind::Statue;
        else if(obj->id()==ObjectId::Propeller)t.kind=Kind::Propeller;
        else if(obj->id()==ObjectId::VolcanoBall){
            bool found=false;
            for(auto& l:launchers)if(l.first==geo){l.second=obj;found=true;}
            if(!found)launchers.push_back({geo,obj});
            continue;
        }else continue;
        // Registered at load, before init or calc moved anything: the placement.
        t.clip=clipEntry(flow,id,vec(geo->pos()));
        objects.push_back(t);
    }
    for(const auto& l:launchers){
        const auto& launcher=*static_cast<const K::Field::ObjectVolcanoBallLauncher*>(l.second);
        const auto* rail=K::Field::RailManager::Instance()?K::Field::RailManager::Instance()->rail(size_t(l.first->pathId())):nullptr;
        if(!rail||rail->points().empty())continue;
        for(const auto* ball:launcher.balls()){
            if(!ball)continue;
            Tracked t{};t.kind=Kind::Ball;t.object=ball;t.geo=l.first;t.start=vec(rail->points()[0].pos);
            t.clip=clipEntry(flow,0x0d6,vec(l.first->pos()));
            objects.push_back(t);
        }
    }
    // The cannons have no Kinoko object. Only the first constructed one animates and fires
    // (ctor 0x806BFB14 numbers them, calc tests +0xe8); each course places one.
    if(const auto* map=K::System::CourseMap::Instance())
        for(uint16_t i=0;i<map->getGeoObjCount();++i){
            const auto* geo=map->getGeoObj(i);
            if(!geo||(geo->id()!=DonkyCannonGC&&geo->id()!=TreeCannon))continue;
            Tracked t{};t.kind=Kind::Cannon;t.geo=geo;
            t.shotFrames=chrFrames(geo->id()==DonkyCannonGC?"DonkyCannonGC":"tree_cannon");
            t.clip=clipEntry(flow,geo->id(),vec(geo->pos()));
            if(t.shotFrames)objects.push_back(t);
            break;
        }
}

void CourseEffects::Impl::stepObject(ef::System& s,Tracked& t,uint32_t timer,unsigned screens,bool timeTrial,unsigned players){
    // vtable +0x30 (0x8067DA54) is the clip entry: an effect is culled once the object is
    // outside every screen and no screen shows the look-behind view (0x8067DCC0).
    const bool culled=t.clip.valid&&t.clip.culled&&!lookBack;
    switch(t.kind){
    case Kind::Pole:{
        // Calc 0x806C3FCC runs past the start delay only; its effect update 0x806C44E4:
        // the pillar effect lives in states 1-3 (before erupting is 0, dormant 4) at the
        // pole's position, scaled by its y scale. rk_flamePoleL for the big pole; the small
        // ones use rk_flamePole on one screen and rk_flamePoleS on more (ctor 0x806C3AA4).
        const auto& o=*static_cast<const K::Field::ObjectFlamePoleV*>(t.object);
        if(timer<=uint32_t(t.geo->setting(1)))break;
        const int spec=o.id()==ObjectId::FlamePoleVBig?ids.flamePoleL:screens==1?ids.flamePole:ids.flamePoleS;
        const unsigned state=o.stateId();
        if(state>=1&&state<=3&&!culled)create(s,t.fx[0],spec);
        if(state==0||state==4||culled)fade(s,t.fx[0]);
        move(s,t.fx[0],at(vec(o.pos()),o.scale().y));
        break;
    }
    case Kind::Statue:{
        // Calc 0x806DAB5C past the start delay (setting 2). The fire (rk_koopaFireL on the
        // big statue, setting 1 == 1, else rk_koopaFire) burns while the cycle frame is
        // below 300 of 300 + 180, on the statue's matrix scaled by its y scale.
        const auto& o=*static_cast<const K::Field::ObjectKoopaFigure64*>(t.object);
        const bool big=t.geo->setting(1)==1;
        if(timer<uint32_t(t.geo->setting(2)))break;
        bool on=false,off=false;
        if(big||timeTrial){
            // Retail's timed cycle (ObjectDirector +0x55, time trials and ghost races) is
            // Kinoko's (0x806DAB5C): a new cycle on frame 480. From its frame after calc:
            // 1 is the step that restarted it (neither branch), 2-300 burn, 301-480 fade.
            // The big statue's burn hit is Kinoko's, so its fire follows Kinoko in every mode.
            const uint32_t f=o.cycleFrame();
            on=f>=2&&f<=300;off=f>=301;
        }else{
            // Offline GP and VS: a cycle starts when the kart picked by timer % players
            // stands within setting 0 (x/z) of a resting statue; the kart's position is the
            // previous step's, as the object calc runs before the karts.
            const int16_t reach=int16_t(t.geo->setting(0));
            const Vec3 k=kartPos[players?(timer%players)&0xff:0],p=vec(o.pos());
            const float dx=p.x-k.x,dz=p.z-k.z;
            const bool trigger=kartPosValid&&dx*dx+dz*dz<=float(int(reach)*int(reach))&&t.frame==480;
            on=t.frame<300;off=t.frame>=300&&!trigger;
            if(trigger)t.frame=0;
            if(t.frame<480)++t.frame;
        }
        const int spec=big?ids.koopaFireL:ids.koopaFire;
        if(on){if(!culled)create(s,t.fx[0],spec);}
        else if(off)fade(s,t.fx[0]);
        if(culled)fade(s,t.fx[0]);
        move(s,t.fx[0],at(o.transform(),vec(o.pos()),o.scale().y));
        break;
    }
    case Kind::Propeller:{
        // Calc 0x80765068 -> 0x807653A8: one rk_epropeller (rk_epropellerS on more than one
        // screen) per blade, turned i * 120 degrees about the spin axis, at the hub.
        const auto& o=*static_cast<const K::Field::ObjectPropeller*>(t.object);
        if(culled){for(auto& h:t.fx)fade(s,h);break;}
        const int spec=screens<2?ids.epropeller:ids.epropellerS;
        for(unsigned i=0;i<3;++i){
            K::EGG::Matrix34f r=K::EGG::Matrix34f::ident;
            r.setAxisRotation(float(int(i*120))*K::DEG2RAD,o.axis());
            create(s,t.fx[i],spec);
            move(s,t.fx[i],at(r.multiplyTo(o.transform()),vec(o.pos()),1));
        }
        break;
    }
    case Kind::Ball:{
        // VolcanoBall1 (ctor 0x806E2904): its calc (0x806E2E08) runs the state, then stores
        // the clip result (+0xf8) the next step's state code reads.
        const auto& b=*static_cast<const K::Field::ObjectVolcanoBall*>(t.object);
        const uint32_t frame=b.stateFrame();
        const float scale=b.scale().x;
        const Vec3 p=vec(b.pos());
        Handle& tail=t.fx[0];Handle& fire=t.fx[1];
        if(b.stateId()==1){
            if(frame==0){
                // enterState1 0x806E2F38: rk_volcErupt (one screen only) at the rail start
                // the init just put it on, and the tail.
                if(!t.hidden&&screens<2)burst(s,ids.volcErupt,at(t.start));
                create(s,tail,screens==1?ids.volcTail:ids.volcTailS);
            }
            // calcFalling 0x806E3034: the tail follows the ball.
            if(!t.hidden){create(s,tail,screens==1?ids.volcTail:ids.volcTailS);move(s,tail,at(p,scale));}
            else fade(s,tail);
        }else if(b.stateId()==2){
            if(frame==0){
                // enterState2 0x806E327C: the tail is killed, rk_volcBrk bursts where it landed.
                kill(s,tail);
                if(!t.hidden)burst(s,ids.volcBrk,at(p,scale));
            }
            // calcBurning 0x806E3324: rk_volcFire_L for setting 3 frames. Its +0xf9 (a hit
            // in 0x806E2D80 puts the fire out) is not simulated by Kinoko.
            if(frame<uint32_t(int32_t(int16_t(t.geo->setting(3))))){
                if(!t.hidden)create(s,fire,screens==1?ids.volcFire:ids.volcFireS);
                else fade(s,fire);
                move(s,fire,at(p,scale));
            }else fade(s,fire);
        }
        t.hidden=t.clip.valid&&t.clip.culled&&!lookBack;
        break;
    }
    case Kind::Cannon:{
        // Calc 0x806BFED0: a kart's CannonStart (seen before the karts' calc, so the
        // previous step's) starts the shot while its clip rests on frame 0; it reaches
        // frame count - 1 and rests again a step later. The start fires rk_dkCannon at the
        // cannon, translated only.
        const bool rest=t.shot<0||int64_t(timer)>=t.shot+t.shotFrames;
        if(cannonStart&&rest){
            t.shot=int64_t(timer);
            if(!culled)burst(s,ids.dkCannon,at(vec(t.geo->pos())));
        }
        break;
    }
    }
}

void CourseEffects::step(RaceEffects& effects){
    Impl& m=*impl_;
    if(!m.merged||!effects.ready)return;
    if(effects.generation()!=m.generation){m.rebuild();m.generation=effects.generation();}
    const auto* race=K::System::RaceManager::Instance();
    const auto* config=K::System::RaceConfig::Instance();
    const auto* karts=K::Kart::KartObjectManager::Instance();
    if(!race||!config||!karts)return;
    const uint32_t timer=race->timer();
    const auto& scenario=config->raceScenario();
    const unsigned players=std::min<unsigned>(scenario.playerCount,unsigned(m.kartPos.size()));
    // Retail picks its variants by local screen count (RaceConfig +0x25). One screen here;
    // Old 3DS (reduced quality) takes the counts retail's split screen uses, as the kart
    // effect LOD does (render_quality.hpp kartEffectScreens).
    const unsigned screens=renderQuality().kartEffectScreens;
    auto& s=effects.system();
    for(auto& t:m.objects)m.stepObject(s,t,timer,screens,scenario.isTimeTrial(),players);
    // What the next step's object calc reads of the karts.
    m.cannonStart=false;
    for(unsigned i=0;i<players;++i){
        const auto* kart=karts->object(i);
        m.kartPos[i]=vec(kart->pos());
        m.cannonStart|=kart->status().onBit(K::Kart::eStatus::CannonStart);
    }
    m.kartPosValid=true;
}

void CourseEffects::clip(const EffectClipCamera& camera,bool lookBack){
    Impl& m=*impl_;
    m.lookBack=lookBack;
    if(m.objects.empty())return;
    // The camera's planes (0x8078707C): left and right, those two turned so the camera's
    // up (their cross product) is world up (0x807DEBCC), top and bottom; the AREA type 9
    // ids holding the camera.
    const Vec3 left=camera.planes[0],right=camera.planes[1];
    Vec3 n=cross(left,right);
    if(const float l=dot(n,n);l>0.f)n=n*(1.f/std::sqrt(l));
    auto level=[&](Vec3 v){
        const Vec3 up{0,1,0},axis=cross(n,up);
        const float c=dot(n,up);
        return axis*(dot(v,axis)/(1.f+c))+cross(axis,v)+v*c;
    };
    const Vec3 levelLeft=level(left),levelRight=level(right);
    const uint16_t cameraMask=areaMask(camera.position,9);
    for(auto& t:m.objects){
        Clip& c=t.clip;
        if(!c.valid)continue;
        const Vec3 p=t.object?vec(t.object->pos()):vec(t.geo->pos());
        // Mode 0 refreshes the mask while the entry is within its far distance (retail on
        // every eighth frame per entry; here every step, see the log).
        if(c.mode==0&&!c.beyondFar)c.mask=areaMask(p,8);
        const Vec3 d=p-camera.position;
        auto outside=[&](Vec3 plane){return (d.x*plane.x+d.y*plane.y)+d.z*plane.z>c.radius;};
        c.beyondFar=camera.distanceScale*((d.z*d.z+d.x*d.x)+d.y*d.y)>c.far2;
        if(c.beyondFar)c.culled=true;
        else if(c.mode!=2&&(c.mask&cameraMask))c.culled=true;
        else if(!outside(left)&&!outside(right))c.culled=!c.wide&&(outside(camera.planes[2])||outside(camera.planes[3]));
        else c.culled=!(c.wide&&!outside(levelLeft)&&!outside(levelRight));
    }
}
}
