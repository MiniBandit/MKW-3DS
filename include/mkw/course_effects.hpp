#pragma once
// Course object particle effects (findings/fix_batch2_course_effects.md, triage C1).
// tools/cook_effect_archives.py cooks every course effect/<object>/*.breff to
// textures/fx/<course>_<file>/ (REFX v3 tables, loose; textures in race.pak). Retail loads
// an object type's archive into the one EffectSystem (EffectInfo 0x8067B4C8: 150 effects,
// 250 emitters, 250 particle managers, 1500 particles) and the object's calc creates, moves
// and fades its EGG::Effect handles. Here the course's archives join the race table
// (RaceEffects) at track load, so they step, draw and share the pools with every other
// race effect; step() then follows retail's per-object emission from Kinoko's object state.
// Presentation only: nothing here feeds race or replay state.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace mkw {
class RaceEffects;
class Texture;
struct EffectClipCamera;

// Appends every emitter of the REFX v3 table `extra` to `base`, keeping the child and
// postfield references (curve kinds 2 and 5) that appendRefxEmitters (kart_drift_effects.hpp)
// refuses: a cooked course table is closed under its references, so its emitter indices
// move by base's emitter count and its texture indices (records, kind 4 curves) by base's
// texture count, and its payload is appended whole. False, with base unchanged, if the
// result would not load (RaceEffects::loadMemory limits).
inline bool appendRefxTable(std::vector<uint8_t>& base,const std::vector<uint8_t>& extra){
    struct Head{char magic[4];uint32_t version,emitters,textures,blob;};
    struct Rec{char name[32];uint32_t desc,ptcl;uint16_t texture[3],ptclTracks,ptclInit,emitTracks,emitInit,ptclSize,pad[2];uint32_t list;};
    static_assert(sizeof(Head)==20&&sizeof(Rec)==64,"REFX v3");
    constexpr size_t TexBytes=40;
    auto header=[](const std::vector<uint8_t>& f,Head& h){
        if(f.size()<sizeof h)return false;
        std::memcpy(&h,f.data(),sizeof h);
        return !std::memcmp(h.magic,"REFX",4)&&h.version==3&&f.size()==sizeof h+size_t(h.textures)*TexBytes+size_t(h.emitters)*sizeof(Rec)+h.blob;
    };
    auto be32=[](const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
    auto be16=[](const uint8_t* p){return uint32_t(p[0])<<8|p[1];};
    auto putBe32=[](uint8_t* p,uint32_t v){p[0]=uint8_t(v>>24);p[1]=uint8_t(v>>16);p[2]=uint8_t(v>>8);p[3]=uint8_t(v);};
    Head bh,eh;
    if(!header(base,bh)||!header(extra,eh)||!eh.emitters||bh.emitters+eh.emitters>255||bh.textures+eh.textures>96)return false;
    const size_t at=(size_t(bh.blob)+3)&~size_t(3); // the payload's offset in the merged one
    if(at+eh.blob>(1u<<20))return false;
    const uint8_t* et=extra.data()+sizeof eh;
    const uint8_t* er=et+size_t(eh.textures)*TexBytes;
    const uint8_t* eb=er+size_t(eh.emitters)*sizeof(Rec);
    std::vector<uint8_t> blob(eb,eb+eh.blob);
    std::vector<Rec> records(eh.emitters);
    std::memcpy(records.data(),er,records.size()*sizeof(Rec));
    for(Rec& r:records){
        const uint32_t curves=uint32_t(r.ptclTracks)+r.emitTracks;
        if(r.desc>eh.blob||eh.blob-r.desc<0x14c||r.ptcl>eh.blob||eh.blob-r.ptcl<0x88)return false;
        if(curves&&(r.list>eh.blob||eh.blob-r.list<curves*4))return false;
        for(uint32_t k=0;k<curves;++k){
            uint32_t c;std::memcpy(&c,blob.data()+r.list+k*4,4);
            if(c>eh.blob||eh.blob-c<0x40)return false;
            uint8_t* p=blob.data()+c;
            const uint32_t size=eh.blob-c;
            // Particle curves resolve their name table (cook_race_effects resolve()):
            // kinds 2 and 5 to emitter indices, kind 4 to texture indices, 0xffffffff absent.
            if(k<r.ptclTracks&&(p[0]==0xab||p[0]==0xac)&&(p[2]==2||p[2]==4||p[2]==5)&&be32(p+24)){
                const uint32_t shift=p[2]==4?bh.textures:bh.emitters;
                const uint32_t table=32+be32(p+12)+be32(p+16)+be32(p+20);
                if(table>size||size-table<4)return false;
                const uint32_t n=be16(p+table);
                if(size-table-4<n*4)return false;
                for(uint32_t i=0;i<n;++i){
                    uint8_t* slot=p+table+4+i*4;
                    if(const uint32_t v=be32(slot);v!=0xffffffffu)putBe32(slot,v+shift);
                }
            }
            c+=uint32_t(at);std::memcpy(blob.data()+r.list+k*4,&c,4);
        }
        r.desc+=uint32_t(at);r.ptcl+=uint32_t(at);
        if(curves)r.list+=uint32_t(at);
        for(auto& t:r.texture)if(t!=0xffff)t=uint16_t(t+bh.textures);
    }
    const uint8_t* bt=base.data()+sizeof bh;
    const uint8_t* br=bt+size_t(bh.textures)*TexBytes;
    const uint8_t* bb=br+size_t(bh.emitters)*sizeof(Rec);
    Head h=bh;h.emitters+=eh.emitters;h.textures+=eh.textures;h.blob=uint32_t(at+eh.blob);
    std::vector<uint8_t> out(sizeof h);
    std::memcpy(out.data(),&h,sizeof h);
    out.insert(out.end(),bt,br);out.insert(out.end(),et,er);
    out.insert(out.end(),br,bb);
    out.insert(out.end(),reinterpret_cast<const uint8_t*>(records.data()),reinterpret_cast<const uint8_t*>(records.data()+records.size()));
    out.insert(out.end(),bb,bb+bh.blob);out.resize(out.size()+(at-bh.blob),0);
    out.insert(out.end(),blob.begin(),blob.end());
    base.swap(out);
    return true;
}

class CourseEffects {
public:
    CourseEffects();
    ~CourseEffects();
    // Course change (GPU idle): rebuilds the race table as loadRaceVfx made it and appends
    // the archives of the course's placed object types whose emission step() ports; their
    // textures load into textures[base..capacity). `archive` is the course's
    // CourseChoice::archive ("Race/Course/<folder>.szs").
    bool load(RaceEffects& effects,Texture* textures,unsigned capacity,const char* archive);
    // Race end (GPU idle): frees the course textures. The table keeps its course emitters,
    // which nothing creates any more, until the next load replaces it.
    void unload(Texture* textures);
    // The last load's outcome, for the runtime log.
    const char* summary() const;
    // Once per simulation step after Kinoko's, before RaceEffects::step: the objects' calc
    // effect calls. ObjectDirector calc runs before the karts' (calcEnginesUnpaused
    // 0x80554AD4), so what an object reads of a kart is the previous step's.
    void step(RaceEffects& effects);
    // After the step's camera: the clip manager (0x80787774) for the top screen, which the
    // objects' next calc reads (object +0x1c, flags +0x20), and RaceCamera's look-behind
    // flag 0x20 (0x8067DCC0: no object effect is culled while it is set).
    void clip(const EffectClipCamera& camera,bool lookBack);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
