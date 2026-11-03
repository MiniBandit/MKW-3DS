#include "mkw/render_quality.hpp"
#include <mkw/race_effects.hpp>
#include <mkw/asset_pack.hpp>
#include <cstdio>
#include <cstring>
#include <cerrno>
namespace mkw {
namespace {
using ef::be16;using ef::be32;using ef::bef;
Vec3 bevec(const uint8_t* p){return {bef(p),bef(p+4),bef(p+8)};}

// EmitterDesc and EmitterDrawSetting (MKW layout, docs/VFX_RESEARCH.md) from BREFF bytes.
void parseDesc(ef::EmitterDesc& d,const uint8_t* p){
    d.commonFlag=be32(p)|ef::EmitterDesc::Relocated;d.emitFlag=be32(p+4);
    d.emitLife=be16(p+8);d.ptclLife=be16(p+0xa);
    d.ptclLifeRandom=int8_t(p[0xc]);d.inheritChildPtclTranslate=int8_t(p[0xd]);d.emitIntervalRandom=int8_t(p[0xe]);d.emitRandom=int8_t(p[0xf]);
    d.emit=bef(p+0x10);d.emitStart=be16(p+0x14);d.emitPast=be16(p+0x16);d.emitInterval=be16(p+0x18);
    d.inheritPtclTranslate=int8_t(p[0x1a]);d.inheritChildEmitTranslate=int8_t(p[0x1b]);
    for(int i=0;i<6;++i)d.commonParam[i]=bef(p+0x1c+i*4);
    d.emitDiv=be16(p+0x34);d.velInitVelocityRandom=int8_t(p[0x36]);d.velMomentumRandom=int8_t(p[0x37]);
    d.velPowerRadiationDir=bef(p+0x38);d.velPowerYAxis=bef(p+0x3c);d.velPowerRandomDir=bef(p+0x40);d.velPowerNormalDir=bef(p+0x44);
    d.velDiffusionEmitterNormal=bef(p+0x48);d.velPowerSpecDir=bef(p+0x4c);d.velDiffusionSpecDir=bef(p+0x50);
    d.velSpecDir=bevec(p+0x54);d.scale=bevec(p+0x60);d.rotate=bevec(p+0x6c);d.translate=bevec(p+0x78);
    d.lodNear=p[0x84];d.lodFar=p[0x85];d.lodMinEmit=p[0x86];d.lodAlpha=p[0x87];d.randomSeed=be32(p+0x88);
    const uint8_t* s=p+0x94;ef::DrawSetting& w=d.draw;
    w.flags=be16(s);w.acmpComp0=s[2];w.acmpComp1=s[3];w.acmpOp=s[4];w.numTevs=s[5];w.flagClamp=s[6];w.indirectTargetStage=s[7];
    std::memcpy(w.tevTexture,s+0x8,4);std::memcpy(w.tevColor,s+0xc,16);std::memcpy(w.tevColorOp,s+0x1c,20);
    std::memcpy(w.tevAlpha,s+0x30,16);std::memcpy(w.tevAlphaOp,s+0x40,20);
    std::memcpy(w.kColorSel,s+0x54,4);std::memcpy(w.kAlphaSel,s+0x58,4);
    w.blendType=s[0x5c];w.blendSrc=s[0x5d];w.blendDst=s[0x5e];w.blendOp=s[0x5f];
    std::memcpy(w.colorInput,s+0x60,8);std::memcpy(w.alphaInput,s+0x68,8);
    w.zCompare=s[0x70];w.alphaFlickType=s[0x71];w.alphaFlickCycle=be16(s+0x72);w.alphaFlickRandom=s[0x74];w.alphaFlickAmplitude=s[0x75];
    w.lightingMode=s[0x76];w.lightingType=s[0x77];std::memcpy(w.lightingAmbient,s+0x78,4);std::memcpy(w.lightingDiffuse,s+0x7c,4);
    w.lightingRadius=bef(s+0x80);w.lightingPosition=bevec(s+0x84);
    for(int i=0;i<2;++i)for(int j=0;j<3;++j)w.indTexOffsetMtx[i][j]=bef(s+0x90+(i*3+j)*4);
    w.indTexScaleExp=int8_t(s[0xa8]);w.pivotX=int8_t(s[0xa9]);w.pivotY=int8_t(s[0xaa]);
    w.ptclType=s[0xac];w.typeOption=s[0xad];w.typeDir=s[0xae];w.typeAxis=s[0xaf];
    w.typeOption0=s[0xb0];w.typeOption1=s[0xb1];w.typeOption2=s[0xb2];w.zOffset=bef(s+0xb4);
}
void parsePtcl(ef::ParticleDesc& d,const uint8_t* p,const uint16_t tex[3]){
    std::memcpy(d.color,p,16);
    d.size[0]=bef(p+0x10);d.size[1]=bef(p+0x14);d.scale[0]=bef(p+0x18);d.scale[1]=bef(p+0x1c);
    for(int i=0;i<3;++i)d.rotate[i]=bef(p+0x20+i*4);
    for(int i=0;i<3;++i){
        d.textureScale[i][0]=bef(p+0x2c+i*8);d.textureScale[i][1]=bef(p+0x30+i*8);
        d.textureRotate[i]=bef(p+0x44+i*4);
        d.textureTranslate[i][0]=bef(p+0x50+i*8);d.textureTranslate[i][1]=bef(p+0x54+i*8);
        d.texture[i]=tex[i];
    }
    d.textureWrap=be16(p+0x74);d.textureReverse=p[0x76];d.acmpRef0=p[0x77];d.acmpRef1=p[0x78];
    for(int i=0;i<3;++i){d.rotateOffsetRandom[i]=p[0x79+i];d.rotateOffset[i]=bef(p+0x7c+i*4);}
}
ef::Mtx34 frameMtx(const EffectFrame& f){
    const Vec3 x=f.x*f.scale,y=f.y*f.scale,z=f.z*f.scale;
    return {{{x.x,y.x,z.x,f.origin.x},{x.y,y.y,z.y,f.origin.y},{x.z,y.z,z.z,f.origin.z}}};
}
}

RaceEffects::RaceEffects():system_(std::make_unique<ef::System>()){system_->reset();}
RaceEffects::~RaceEffects()=default;
void RaceEffects::unload(){
    reset();ready=false;
    decltype(system_->resources){}.swap(system_->resources);
    decltype(textures){}.swap(textures);decltype(blob_){}.swap(blob_);
}

bool RaceEffects::load(const char* path){
    unload();loadError_="open";loadErrno_=0;
    // Use the same already-open archive path as models and textures. Loose
    // SD files remain supported, but are no longer the only source here.
    std::vector<uint8_t> packed;
    if(pack::read(path,packed))return loadMemory(packed.data(),packed.size());
    FILE* f=std::fopen(path,"rb");if(!f){loadErrno_=errno;return false;}
    // One bounded read, rather than hundreds of tiny reads and a trailing
    // fgetc EOF probe. Native SD and emulator stdio must parse the same bytes.
    std::setvbuf(f,nullptr,_IONBF,0);
    loadError_="file size";
    const bool sized=std::fseek(f,0,SEEK_END)==0;
    const long length=sized?std::ftell(f):-1;
    if(length<20||length>2*1024*1024){loadErrno_=errno;std::fclose(f);return false;}
    std::vector<uint8_t> file(size_t(length),0);bool read=false;
    loadError_="SD read";
    for(unsigned attempt=0;attempt<3&&!read;++attempt){
        std::clearerr(f);
        read=std::fseek(f,0,SEEK_SET)==0&&std::fread(file.data(),1,file.size(),f)==file.size();
    }
    if(!read)loadErrno_=errno;
    std::fclose(f);if(!read)return false;
    return loadMemory(file.data(),file.size());
}
bool RaceEffects::loadMemory(const void* bytes,size_t size){
    unload();loadError_="header";loadErrno_=0;
    if(size<20)return false;
    const auto* file=static_cast<const uint8_t*>(bytes);
    uint32_t h[5]{};std::memcpy(h,file,sizeof h);
    bool ok=!std::memcmp(h,"REFX",4)&&h[1]==3&&h[2]>0&&h[2]<=512&&h[3]<=160&&h[4]<=(1u<<20);
    struct Record { char name[32]; uint32_t desc,ptcl; uint16_t texture[3],ptclTracks,ptclInit,emitTracks,emitInit,ptclSize,pad[2]; uint32_t list; };
    static_assert(sizeof(Record)==64,"REFX record");
    if(!ok)return false;
    loadError_="table/payload size";
    const size_t expected=sizeof h+size_t(h[3])*40+size_t(h[2])*64+h[4];
    if(size!=expected)return false;
    std::vector<Record> records(h[2]);textures.resize(h[3]);blob_.resize(h[4]);
    size_t at=sizeof h;
    if(!textures.empty())std::memcpy(textures.data(),file+at,textures.size()*40);
    at+=textures.size()*40;
    std::memcpy(records.data(),file+at,records.size()*64);at+=records.size()*64;
    if(!blob_.empty())std::memcpy(blob_.data(),file+at,blob_.size());
    loadError_="resource offsets";
    auto& res=system_->resources;
    if(ok)res.resize(records.size());
    for(size_t i=0;ok&&i<records.size();++i){
        Record& r=records[i];r.name[31]=0;
        const size_t curves=size_t(r.ptclTracks)+r.emitTracks;
        ok&=r.desc<=blob_.size()&&blob_.size()-r.desc>=0x14c&&r.ptcl<=blob_.size()&&blob_.size()-r.ptcl>=0x88u&&r.ptclInit<=r.ptclTracks&&r.emitInit<=r.emitTracks&&
            (!curves||r.list+curves*4<=blob_.size());
        for(int k=0;k<3;++k)ok&=r.texture[k]==0xffff||r.texture[k]<textures.size();
        if(!ok)break;
        ef::EmitterResource& e=res[i];
        std::memcpy(e.name,r.name,32);e.index=uint16_t(i);
        parseDesc(e.desc,blob_.data()+r.desc);
        parsePtcl(e.ptcl,blob_.data()+r.ptcl,r.texture);
        for(size_t k=0;k<curves;++k){
            uint32_t at;std::memcpy(&at,blob_.data()+r.list+k*4,4);
            loadError_="curve offsets";
            ok&=at<=blob_.size()&&blob_.size()-at>=0x40;
            if(!ok)break;
            (k<r.ptclTracks?e.ptclTracks:e.emitTracks).push_back(blob_.data()+at);
        }
        e.ptclInitTracks=r.ptclInit;e.emitInitTracks=r.emitInit;
    }
    for(auto& t:textures)t.name[31]=0;
    if(!ok){textures.clear();res.clear();blob_.clear();}
    reset();
    if(ok)loadError_="none";
    return ready=ok;
}
int RaceEffects::find(const char* name) const {
    const auto& res=system_->resources;
    for(size_t i=0;i<res.size();++i)if(!std::strcmp(res[i].name,name))return int(i);
    return -1;
}
void RaceEffects::reset(){pendingBlue_.clear();system_->reset();std::vector<Slot>(SlotCount).swap(slots_);serial_=0;++generation_;diagnosticMask_=0;}
unsigned RaceEffects::allocateObjectSlots(unsigned count){
    const unsigned first=unsigned(slots_.size());slots_.resize(size_t(first)+count);return first;
}
unsigned RaceEffects::liveParticles() const {return system_->liveParticles;}
void RaceEffects::setCamera(const float view[3][4]){std::memcpy(system_->cameraMtx.m,view,sizeof(float)*12);}
void RaceEffects::setCamera(Vec3 eye,Vec3 target,Vec3 up){
    const Vec3 look=normalized(eye-target,{0,0,1}),right=normalized(cross(up,look),{1,0,0}),camUp=cross(look,right);
    const float view[3][4]={{right.x,right.y,right.z,-dot(eye,right)},{camUp.x,camUp.y,camUp.z,-dot(eye,camUp)},
                            {look.x,look.y,look.z,-dot(eye,look)}};
    setCamera(view);
}

ef::Effect* RaceEffects::start(int spec,const EffectFrame& frame,uint8_t group){
    if(!ready||spec<0||size_t(spec)>=system_->resources.size())return nullptr;
    ef::Effect* e=system_->createEffect(system_->resources[size_t(spec)],0,group);
#ifndef MKW_RELEASE
    // Bounded transition diagnostics for the reported missing item/hit/boost
    // effects. One line per local/shared root per race; no per-frame I/O.
    if(group==0||group==2){
        const char* name=system_->resources[size_t(spec)].name;
        constexpr const char* names[]={"rk_itemBox","rk_bananaSmk","rk_miniTurbo","rk_spinHalfL","rk_spinHalfR","rk_spinL","rk_spinR","rk_driftSpark1LB_Spark00","rk_driftSpark1LB_Spark01","rk_driftSpark1RB_Spark00","rk_driftSpark1RB_Spark01","rk_driftSpark1CB_Spark00","rk_driftSpark1CB_Spark01"};
        for(unsigned i=0;i<std::size(names);++i)if(!std::strcmp(name,names[i])&&!(diagnosticMask_&(1u<<i))){
            diagnosticMask_|=1u<<i;
            if(FILE* log=std::fopen("sdmc:/3ds/mkw-3ds/effects-state.txt","a")){
                std::fprintf(log,"create generation=%u root=%s ok=%u particles=%u effects=%lu emitters=%lu managers=%lu\n",generation_,name,e!=nullptr,system_->liveParticles,(unsigned long)system_->effects.used(),(unsigned long)system_->emitters.used(),(unsigned long)system_->managers.used());
                std::fclose(log);
            }
        }
    }
#endif
    if(!e)return nullptr;
    e->serial=++serial_;
    system_->setRootMtx(*e,frameMtx(frame));
    return e;
}
void RaceEffects::burst(int spec,const EffectFrame& frame){start(spec,frame);}
void RaceEffects::burst(int spec,const EffectFrame& frame,float velocity){if(auto* e=start(spec,frame))system_->setSpecificVelocity(*e,velocity);}
bool RaceEffects::alive(unsigned slot) const {
    return slot<slots_.size()&&system_->alive(slots_[slot].effect,slots_[slot].serial);
}
void RaceEffects::create(unsigned slot,int spec,const EffectFrame& frame,uint8_t group){
    if(slot>=slots_.size()||alive(slot))return;
    Slot& s=slots_[slot];
    s.effect=start(spec,frame,group);s.serial=s.effect?s.effect->serial:0;s.spec=spec;s.fading=false;
}
void RaceEffects::move(unsigned slot,const EffectFrame& frame){
    if(alive(slot))system_->setRootMtx(*slots_[slot].effect,frameMtx(frame));
}
void RaceEffects::followFade(unsigned slot){
    if(!alive(slot))return;
    system_->retireEmitters(*slots_[slot].effect);slots_[slot].fading=true;
}
void RaceEffects::fade(unsigned slot){
    if(alive(slot))system_->retireEmitters(*slots_[slot].effect);
    if(slot<slots_.size())slots_[slot]={};
}
void RaceEffects::setColor(unsigned slot,const uint8_t rgba[4]){
    if(alive(slot))system_->setColor(*slots_[slot].effect,rgba);
}
void RaceEffects::setParticleLife(unsigned slot,uint16_t life){
    if(alive(slot))system_->setParticleLife(*slots_[slot].effect,life);
}
void RaceEffects::setParticleScale(unsigned slot,float x,float y){
    if(alive(slot))system_->setParticleScale(*slots_[slot].effect,x,y);
}
void RaceEffects::setEmissionRatio(unsigned slot,float ratio){
    if(alive(slot))system_->setEmissionRatio(*slots_[slot].effect,ratio);
}
void RaceEffects::setSpecificVelocity(unsigned slot,float speed){
    if(alive(slot))system_->setSpecificVelocity(*slots_[slot].effect,speed);
}
void RaceEffects::setVelocity(unsigned slot,Vec3 velocity){
    if(alive(slot))system_->setVelocity(*slots_[slot].effect,{velocity.x,velocity.y,velocity.z});
}
void RaceEffects::stopParticleTrack(unsigned slot,unsigned track,bool stop){
    if(!alive(slot)||track>=32)return;
    auto& mask=slots_[slot].effect->stoppedParticleTracks;
    if(stop)mask|=1u<<track;else mask&=~(1u<<track);
}
void RaceEffects::setDisabled(unsigned slot,bool calc,bool draw){
    if(!alive(slot))return;
    auto& flags=slots_[slot].effect->flags;
    flags=(flags&~3u)|(calc?1u:0u)|(draw?2u:0u);
}
void RaceEffects::kill(unsigned slot){
    if(alive(slot))system_->retireEffect(*slots_[slot].effect);
    if(slot<slots_.size())slots_[slot]={};
}
void RaceEffects::attach(unsigned slot,int spec,const EffectFrame& frame,bool active){
    if(slot>=slots_.size())return;
    if(spec<0){kill(slot);return;}
    Slot& s=slots_[slot];
    if(active){
        // A fading instance or another emitter set hands the slot to a fresh one.
        if(alive(slot)&&(s.fading||s.spec!=spec)){followFade(slot);s={};}
        if(!alive(slot))create(slot,spec,frame);
    }else if(alive(slot)&&!s.fading)followFade(slot);
    move(slot,frame);
}
void RaceEffects::restart(unsigned slot,int spec,const EffectFrame& frame){
    kill(slot);create(slot,spec,frame);
}
void RaceEffects::setGroups(uint32_t calc,uint32_t draw){system_->calcGroups=calc;system_->drawGroups=draw;}
void RaceEffects::burstBlueSmoke(int spec,const EffectFrame& frame){
    if(ready&&spec>=0)pendingBlue_.push_back({spec,frame});
}
bool RaceEffects::roomForBlueSmoke(){
    auto enough=[&](){return system_->effects.freeCount>=1&&system_->emitters.freeCount>=8&&
        system_->managers.freeCount>=8&&system_->particles.freeCount>=32;};
    if(enough())return true;
    // Reuse the bounded pool: only expendable driving trails/smoke are
    // preempted. Do not kill hits, boxes, course effects or another explosion.
    for(auto* e=system_->active.head;e&&!enough();){
        auto* next=e->next;bool decorative=e->emitters.head!=nullptr;
        for(auto* em=e->emitters.head;em;em=em->next){
            const char* n=em->res->name;
            decorative&=!std::strncmp(n,"rk_drift",8)||!std::strncmp(n,"rk_kouraTail",12)||
                !std::strncmp(n,"rk_dirtSmoke",12)||!std::strncmp(n,"rk_start",8)||
                !std::strcmp(n,"rk_wheelie")||!std::strncmp(n,"rk_miniTurbo",12);
        }
        if(decorative){system_->retireEffect(*e);system_->garbageCollection();}
        e=next;
    }
    return enough();
}
void RaceEffects::step(){
    if(!ready)return;
    // Process after all ordinary starts, immediately before calc. The parent
    // and its authored child creation run first, with room for all three layers.
    for(size_t i=0;i<pendingBlue_.size();){
        if(!roomForBlueSmoke())break; // Retain the event; never silently discard it.
        const auto pending=pendingBlue_[i];auto* e=start(pending.spec,pending.frame);
        if(!e)break;
        system_->active.remove(e);system_->active.insertBefore(system_->active.head,e);
        pendingBlue_.erase(pendingBlue_.begin()+i);
        // One burst per calc protects its budget against simultaneous blue hits.
        break;
    }
    system_->calc();
}
}
