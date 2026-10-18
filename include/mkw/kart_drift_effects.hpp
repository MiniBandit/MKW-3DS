#pragma once
#include "mkw/kart_terrain_effects.hpp"
#include "mkw/vehicle_exhaust.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mkw {
// Appends the named emitters of a REFX v3 table to `base` (the race table, race-effects.bin)
// when `base` lacks them. tools/cook_effect_archives.py cooks the whole RKRace archive into
// such tables (textures/fx/common_rkrace/), so retail emitters the race table never needed
// play by name with no new cook. Descriptors, particle parameters and curves are copied
// verbatim; their textures join base's texture list, and the records and texture curves
// (kind 4) are re-pointed at them. An emitter with child or postfield references (kind 2/5)
// is refused. Returns the number of emitters appended; on 0 `base` is unchanged.
inline unsigned appendRefxEmitters(std::vector<uint8_t>& base,const std::vector<uint8_t>& extra,const char* const* names,unsigned count){
    struct Head { char magic[4]; uint32_t version,emitters,textures,blob; };
    struct Rec { char name[32]; uint32_t desc,ptcl; uint16_t texture[3],ptclTracks,ptclInit,emitTracks,emitInit,ptclSize,pad[2]; uint32_t list; };
    static_assert(sizeof(Head)==20&&sizeof(Rec)==64,"REFX v3");
    constexpr size_t TexBytes=40;
    auto header=[](const std::vector<uint8_t>& f,Head& h){
        if(f.size()<sizeof h)return false;
        std::memcpy(&h,f.data(),sizeof h);
        return !std::memcmp(h.magic,"REFX",4)&&h.version==3&&f.size()==sizeof h+size_t(h.textures)*TexBytes+size_t(h.emitters)*sizeof(Rec)+h.blob;
    };
    Head bh,eh;
    if(!header(base,bh)||!header(extra,eh))return 0;
    const uint8_t* bt=base.data()+sizeof bh;const uint8_t* br=bt+size_t(bh.textures)*TexBytes;const uint8_t* bb=br+size_t(bh.emitters)*sizeof(Rec);
    const uint8_t* et=extra.data()+sizeof eh;const uint8_t* er=et+size_t(eh.textures)*TexBytes;const uint8_t* eb=er+size_t(eh.emitters)*sizeof(Rec);
    auto rec=[](const uint8_t* table,unsigned i){Rec r;std::memcpy(&r,table+size_t(i)*sizeof r,sizeof r);r.name[31]=0;return r;};
    auto find=[&](const uint8_t* table,unsigned n,const char* name){for(unsigned i=0;i<n;++i)if(!std::strcmp(rec(table,i).name,name))return int(i);return -1;};
    auto be32=[](const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|uint32_t(p[3]);};
    // The cookers write the payload item after item (4-aligned): an item ends where the next starts.
    std::vector<uint32_t> starts{eh.blob};
    for(unsigned i=0;i<eh.emitters;++i){
        const Rec r=rec(er,i);const unsigned curves=r.ptclTracks+r.emitTracks;
        starts.push_back(r.desc);starts.push_back(r.ptcl);
        if(!curves)continue;
        if(r.list>eh.blob||eh.blob-r.list<curves*4u)return 0;
        starts.push_back(r.list);
        for(unsigned k=0;k<curves;++k){uint32_t at;std::memcpy(&at,eb+r.list+k*4,4);starts.push_back(at);}
    }
    std::sort(starts.begin(),starts.end());
    auto extent=[&](uint32_t at)->uint32_t{const auto it=std::upper_bound(starts.begin(),starts.end(),at);return at<eh.blob&&it!=starts.end()?*it-at:0u;};
    std::vector<uint8_t> textures(bt,bt+size_t(bh.textures)*TexBytes),records(br,br+size_t(bh.emitters)*sizeof(Rec)),blob(bb,bb+bh.blob);
    std::vector<int> textureMap(eh.textures,-1);
    auto texture=[&](uint32_t t)->int{
        if(t>=eh.textures)return -1;
        if(textureMap[t]<0){
            if(textures.size()/TexBytes>=96)return -1;
            textureMap[t]=int(textures.size()/TexBytes);textures.insert(textures.end(),et+size_t(t)*TexBytes,et+size_t(t+1)*TexBytes);
        }
        return textureMap[t];
    };
    auto put=[&](const uint8_t* p,size_t n){while(blob.size()%4)blob.push_back(0);const uint32_t at=uint32_t(blob.size());blob.insert(blob.end(),p,p+n);return at;};
    // A curve's table 3 name slots (big-endian u32 after the count; cook_race_effects resolve()).
    auto slots=[&](uint8_t* c,uint32_t size,auto&& visit){
        if(size<32)return false;
        const uint32_t table=32+be32(c+12)+be32(c+16)+be32(c+20);
        if(!be32(c+24))return true;
        if(table+4>size)return false;
        const unsigned n=unsigned(c[table])<<8|c[table+1];
        if(table+4+n*4>size)return false;
        for(unsigned i=0;i<n;++i)if(!visit(c+table+4+i*4))return false;
        return true;
    };
    unsigned added=0;
    for(unsigned q=0;q<count;++q){
        if(find(br,bh.emitters,names[q])>=0)continue;
        const int at=find(er,eh.emitters,names[q]);
        if(at<0)continue;
        Rec r=rec(er,unsigned(at));
        const unsigned curves=r.ptclTracks+r.emitTracks;
        std::vector<uint32_t> list(curves);
        for(unsigned k=0;k<curves;++k){
            uint32_t src;std::memcpy(&src,eb+r.list+k*4,4);
            const uint32_t n=extent(src);
            if(n<0x40)return 0;
            std::vector<uint8_t> c(eb+src,eb+src+n);
            if(k<r.ptclTracks&&(c[0]==0xab||c[0]==0xac)){
                bool refs=false;
                if((c[2]==2||c[2]==5)&&(!slots(c.data(),n,[&](uint8_t* p){refs|=be32(p)!=0xffffffffu;return true;})||refs))return 0;
                if(c[2]==4&&!slots(c.data(),n,[&](uint8_t* p){
                    const uint32_t t=be32(p);if(t==0xffffffffu)return true;
                    const int m=texture(t);if(m<0)return false;
                    p[0]=0;p[1]=0;p[2]=uint8_t(m>>8);p[3]=uint8_t(m);return true;}))return 0;
            }
            list[k]=put(c.data(),c.size());
        }
        const uint32_t descBytes=extent(r.desc),ptclBytes=extent(r.ptcl);
        if(descBytes<0x14c||ptclBytes<0x88||ptclBytes<r.ptclSize)return 0;
        r.desc=put(eb+r.desc,descBytes);r.ptcl=put(eb+r.ptcl,ptclBytes);
        r.list=curves?put(reinterpret_cast<const uint8_t*>(list.data()),list.size()*4):0;
        for(auto& t:r.texture)if(t!=0xffff){const int m=texture(t);if(m<0)return 0;t=uint16_t(m);}
        records.insert(records.end(),reinterpret_cast<const uint8_t*>(&r),reinterpret_cast<const uint8_t*>(&r)+sizeof r);
        ++added;
    }
    while(blob.size()%4)blob.push_back(0);
    if(!added||bh.emitters+added>255||blob.size()>(1u<<20))return 0;
    Head h=bh;h.emitters+=added;h.textures=uint32_t(textures.size()/TexBytes);h.blob=uint32_t(blob.size());
    std::vector<uint8_t> out(sizeof h);std::memcpy(out.data(),&h,sizeof h);
    out.insert(out.end(),textures.begin(),textures.end());out.insert(out.end(),records.begin(),records.end());out.insert(out.end(),blob.begin(),blob.end());
    base.swap(out);
    return added;
}

// KartEffect's drift group for a racer no local player drives (PAL addresses). KartEffect
// init (0x8068F044) creates for such a racer (+0x129 isLocal and +0x12b isGhost clear,
// 0x8068DF98; a CPU sets +0x12a, and on one screen +0x10, 0x806B11B0) only these drift handles:
//   kart (+0x530): rk_driftSmoke at both rear wheels [0, 1] and the remote spark emitters
//        rk_driftSpark1L/1R_Spark01S [3, 5] (blue) and 2L/2R_Spark01S [7, 9] (orange);
//   bike (+0x614): rk_driftSmoke [0] and rk_driftSpark1LB/1RB/1CB_Spark01S [2, 4, 6].
// The Spark00 cores, Chip00 chips, 1T bursts and rk_autoDrift* are local only (+0x129), and
// CPUs drift manually (+0xe clear, race_grid.cpp). The update (kart 0x80698028, bike
// 0x80698F70) runs from 0x80695BA8 only while the kart effect LOD flag +7 is clear
// (0x80693D84); its rising edge kills the whole group (0x806958F4). Racer r's instances live
// in effect group 2 + r, so EffectManager's calc/draw group masks (KartEffectLod) apply.
// Not ported: EGG::Effect +0x4c (0x802237D0), the drift smoke's 1.1 x wheel-delta velocity
// (kart_terrain_effects.hpp leaves it out the same way).
// The boost flame is KartEffect's exhaust group: rk_miniTurboS (+0x308..+0x314, one per pipe;
// the full rk_miniTurbo only for the one-screen player or a ghost, 0x8068F044). KartMove calls
// 0x806A3AC4 with the frames of every boost it activates (activateBoost 0x8057F090, inlined
// in releaseMt 0x80582F9C, ...): unless the last update saw a boost panel or ramp contact go
// on/stay on/go off (+0x25..+0x28) or +7 was set, it kills the pipes' flames, creates them on
// the pipes above water (+0x848, +0x96d) and restarts the flame clock (+0x1a frames,
// +0x1c elapsed). Each update moves them to the pipe matrices +0x8ac (0x8069BAF8) and, once
// the clock reaches the frames, followFades them (0x8069C040). The local-only 1T burst and
// OneT puff (rk_miniTurbo1T, rk_gasSmokeOneT) and the idle rk_gasSmoke are not ported here.
// Only the nearest few racers are outside the LOD at once, so the slots form a pool of
// blocks, one per racer outside the LOD, released at the kill on the LOD edge.
class KartDriftEffects {
public:
    static constexpr unsigned MaxRacers=12,Pipes=4,SlotsPerBlock=6+Pipes;
    static constexpr unsigned FirstSlot=KartTerrainEffects::DirtSlot+1;
    static constexpr unsigned Blocks=(RaceEffects::SlotCount-FirstSlot)/SlotsPerBlock;
    // One screen: 3 racers outside the LOD (KartEffectLod::limits), one more for the step
    // in which one leaves and another enters it.
    static_assert(Blocks>=4,"drift effect slots");
    enum Fx : uint8_t {Smoke,BlueL,BlueR,OrangeL,OrangeR,BikeL,BikeR,BikeC,Flame,FxCount};
    // The init's names (0x8068F044); the archive loader appends those race-effects.bin lacks.
    static constexpr const char* Names[FxCount]={"rk_driftSmoke","rk_driftSpark1L_Spark01S","rk_driftSpark1R_Spark01S",
        "rk_driftSpark2L_Spark01S","rk_driftSpark2R_Spark01S","rk_driftSpark1LB_Spark01S","rk_driftSpark1RB_Spark01S","rk_driftSpark1CB_Spark01S",
        "rk_miniTurboS"};
    // What KartEffect copies from the kart (0x80693E94) for this group.
    struct Input {
        EffectFrame pose;             // +0x6f4 (axes) and +0x6d0 (position)
        Vec3 wheel[2]{};              // +0x7bc / +0x7c8: wheelEdgePos (0x80590390) of the rear wheels; bike front, rear
        bool floor[2]{};              // +0x7b9 / +0x7ba: wheelIdxHasFloorCollision (0x80590338)
        Vec3 rearFloorNormal{0,1,0};  // bike +0x80c: the rear wheel's floor normal, world up in the air
        unsigned driftState=0;        // getDriftState (0x805911D8)
        bool driftAuto=false;         // status bit 28
        int hopStickX=0;              // KartMove +0xcc
        uint16_t mtCharge=0;          // getMtCharge (0x805911E8)
        bool bike=false,insideBike=false; // +0x124 == 1, +0xd (vehicle type 2)
        bool scaled=false;float scale=1;  // +0xb0, +0x72c / +0x738 (KartItemEffects::effectScale)
        bool pressed=false;           // +0x49
        bool vanished=false;          // status bit 83: 0x80693D84 kills every effect instead (0x806958F4, 1)
        bool bullet=false;            // the Bullet's update returns before this group (0x80693D84)
        // Boost flame. What the port observes of this step's boost activations:
        bool boostVoice=false;        // KartMove SoundCue::BoostVoice (an activation, or any releaseMt)
        bool allMtActive=false;       // KartBoost AllMt active after the step
        uint16_t miniTurbo=0;         // stats +0x6c, an MT's boost frames (releaseMt; x3 for an SMT)
        uint32_t startSerial=0;int16_t startFrames=0; // applyStartBoost (0x8058212C)
        bool mushroom=false;          // ItemEffect::Mushroom: activateMushroom (0x8057F3D8), 90 frames
        bool beforeRespawn=false,inAction=false; // activateMushroom returns early in either
        bool onBoostPanel=false;      // after the countdown, a wheel's floor is KCL type 6 or 7 (0x806A0FDC)
        const VehicleExhaust* exhaust=nullptr; // the vehicle's pipes (+0x848, +0x8ac)
        bool diving=false,hasSea=false;float seaHeight=0; // +0x96d: a pipe below the Psea surface
    };
    bool bind(const RaceEffects& e){
        bool all=true;
        for(unsigned i=0;i<FxCount;++i)all&=(spec_[i]=e.find(Names[i]))>=0;
        return all;
    }
    int spec(Fx fx) const { return spec_[fx]; }
    void reset(){state_={};used_={};sparkFrames_=smokeFrames_=flameStarts_=0;}
    // QA: steps on which some racer emitted remote sparks / drift smoke; boost flame starts.
    unsigned sparkFrames() const { return sparkFrames_; }
    unsigned smokeFrames() const { return smokeFrames_; }
    unsigned flameStarts() const { return flameStarts_; }
    void beginStep(){sparked_=smoked_=false;}
    void endStep(){sparkFrames_+=sparked_;smokeFrames_+=smoked_;}
    // One KartEffect update of racer 1..11 after the step's physics. lod is this frame's +7
    // (KartEffectLod::lod).
    void step(RaceEffects& e,unsigned racer,const Input& in,bool lod){
        if(!racer||racer>=MaxRacers)return;
        auto& s=state_[racer];
        const uint8_t group=uint8_t(KartFxSlots::FirstGroup+racer);
        // During the physics: KartMove's boost activations call 0x806A3AC4 with the last
        // update's boost-panel flags and +7.
        const uint16_t frames=boostFrames(in,s);
        if(frames&&(!s.panel||s.panelRising)&&!s.panelFalling&&!s.lastLod)startFlame(e,in,s,frames,group);
        s.lastDrift=in.driftState;s.startSerial=in.startSerial;s.lastLod=lod;
        // The wheel collision's selector (0x806A0FDC): +0x25 is cleared on its first call after an
        // update cleared +0x29 (0x80696CF0), then collects boost panel and ramp contact.
        if(!s.panelLatched){s.panel=false;s.panelLatched=true;}
        s.panel|=in.onBoostPanel;
        if(in.vanished){release(e,s);return;}
        if(in.bullet)return;
        if(lod&&!s.wasLod)release(e,s);
        s.wasLod=lod;
        if(lod)return;
        const int base=acquire(s);
        // 0x806946BC: the flame clock, the boost-panel edges, the air frames of the tracked
        // wheels and the pipes below the water.
        ++s.flameElapsed;
        s.panelRising=s.panel&&!s.panelPrev;s.panelFalling=!s.panel&&s.panelPrev;s.panelPrev=s.panel;
        for(unsigned w=0;w<2;++w)s.air[w]=in.floor[w]?0:s.air[w]+1;
        for(unsigned p=0;p<Pipes;++p)s.underwater[p]=in.diving&&in.hasSea&&pipe(in,p).origin.y<in.seaHeight;
        if(base>=0){
            Ctx c{e,in,s,unsigned(base),group};
            if(in.bike)bikeStep(c);else kartStep(c);
            flameStep(c);
        }
        // 0x80696CF0: this update's drift state and wheel positions for the next one.
        s.prevState=in.driftState;s.prev[0]=in.wheel[0];s.prev[1]=in.wheel[1];
        s.panelLatched=false;
    }
    // A pipe's matrix (+0x8ac): the kart pose moved to the pipe and turned by its pitch and
    // yaw (Ry(yaw) Rx(pitch) in kart space), scaled by the vehicle's flame scale; the same
    // frame src/main.cpp gives player 0's flames.
    static EffectFrame pipe(const Input& in,unsigned p){
        EffectFrame f=in.pose;
        if(!in.exhaust||p>=in.exhaust->count)return f;
        const float* q=in.exhaust->pipes[p];
        const float pitch=q[3]*3.14159265f/180.f,yaw=q[4]*3.14159265f/180.f;
        const float sp=std::sin(pitch),cp=std::cos(pitch),sy=std::sin(yaw),cy=std::cos(yaw);
        const Vec3 kx=in.pose.x,ky=in.pose.y,kz=in.pose.z;
        auto axis=[&](float x,float y,float z){const float y1=y*cp-z*sp,z1=y*sp+z*cp;return kx*(x*cy+z1*sy)+ky*y1+kz*(-x*sy+z1*cy);};
        f.origin=in.pose.origin+kx*q[0]+ky*q[1]+kz*q[2];
        f.x=axis(1,0,0);f.y=axis(0,1,0);f.z=axis(0,0,1);f.scale=in.exhaust->flameScale;
        return f;
    }
private:
    static constexpr unsigned AirFrames=10;      // +0xae0 (0x8068EA58)
    static constexpr unsigned SparkDelay=5;      // +0x98c (0x806B11B0)
    static constexpr uint16_t ChipCharge=150;    // +0x988 (0x806B11B0)
    // Drift smoke of a racer that is not the one-screen player (+0xf clear, 0x806B11B0):
    // emission ratio +0x980 (0.1, 0x8089D730) and particle life +0x984 (10, 0x8089D6AC).
    static constexpr float SmokeRatio=.1f;
    static constexpr uint16_t SmokeLife=10;
    static constexpr uint16_t MushroomFrames=90;   // activateMushroom (0x8057F3D8)
    static constexpr uint32_t FlameIdle=1000;      // +0x1c at init (0x8068EA58) and after a flame (0x8069C040)
    static constexpr unsigned FlameSlot=6;         // block slots 6..9: the pipes' flames
    struct State {
        unsigned air[2]{};Vec3 prev[2]{};   // +0x80/+0x84, +0x7d4/+0x7e0
        unsigned prevState=0,counter=0;     // +0x7a0, +0x34
        bool wasLod=false;                  // +8
        std::array<float,6> eggScale{};     // EGG setScale (+0x70) survives on a running instance
        int block=-1;                       // the slot block while outside the LOD
        bool lastLod=true;                  // +7 when the next step's physics runs (set for a remote racer at race init, 0x8067CA4C)
        unsigned lastDrift=0;uint32_t startSerial=0; // for the boost detection
        uint16_t flameFrames=0;uint32_t flameElapsed=FlameIdle; // +0x1a, +0x1c
        bool panel=false,panelLatched=false,panelPrev=false,panelRising=false,panelFalling=false; // +0x25, +0x29, +0x26, +0x27, +0x28
        std::array<bool,Pipes> underwater{}; // +0x96d
    };
    struct Ctx { RaceEffects& e;const Input& in;State& s;unsigned base;uint8_t group; };
    int acquire(State& s){
        if(s.block<0)for(unsigned b=0;b<Blocks;++b)if(!used_[b]){used_[b]=true;s.block=int(b);break;}
        return s.block<0?-1:int(FirstSlot+unsigned(s.block)*SlotsPerBlock);
    }
    // 0x806958F4: every handle of the racer is killed; the block returns to the pool.
    void release(RaceEffects& e,State& s){
        if(s.block<0)return;
        for(unsigned j=0;j<SlotsPerBlock;++j)e.kill(FirstSlot+unsigned(s.block)*SlotsPerBlock+j);
        used_[unsigned(s.block)]=false;s.block=-1;
    }
    // The frames KartMove passed to 0x806A3AC4 this step, from what the port observes (0: none).
    // releaseMt (0x80582F9C): a drift charged to 2/3 at the last step, the MT boost on now and its
    // voice cue; the stats' MT frames, x3 for an SMT. applyStartBoost (0x8058212C): its frames.
    // activateMushroom (0x8057F3D8): 90, unless BeforeRespawn or InAction. Two in one step: the longer.
    static uint16_t boostFrames(const Input& in,const State& s){
        uint16_t frames=0;
        if(s.lastDrift>=2&&in.driftState==0&&in.boostVoice&&in.allMtActive)
            frames=uint16_t(s.lastDrift==3?in.miniTurbo*3u:in.miniTurbo);
        if(in.startSerial!=s.startSerial&&in.startFrames>0)frames=std::max<uint16_t>(frames,uint16_t(in.startFrames));
        if(in.mushroom&&!in.beforeRespawn&&!in.inAction)frames=std::max(frames,MushroomFrames);
        return frames;
    }
    // 0x806A3AC4: kill the pipes' flames, create them on the pipes above water (0x806A4580),
    // and restart the clock. The new instance's first matrix is set by the same frame's update.
    void startFlame(RaceEffects& e,const Input& in,State& s,uint16_t frames,uint8_t group){
        const int base=acquire(s);
        const unsigned pipes=in.exhaust?in.exhaust->count:0;
        if(base>=0)for(unsigned p=0;p<pipes;++p){
            const unsigned slot=unsigned(base)+FlameSlot+p;
            e.kill(slot);
            if(!s.underwater[p]){const EffectFrame f=pipe(in,p);e.create(slot,spec_[Flame],f,group);}
        }
        s.flameFrames=frames;s.flameElapsed=0;++flameStarts_;
    }
    // 0x8069BAF8 / 0x8069C040: the flames follow the pipes above water while the clock runs,
    // then followFade every frame, the clock parked at 1000.
    void flameStep(Ctx& c){
        const unsigned pipes=c.in.exhaust?c.in.exhaust->count:0;
        for(unsigned p=0;p<pipes;++p)if(!c.s.underwater[p]&&c.e.alive(c.base+FlameSlot+p))c.e.move(c.base+FlameSlot+p,pipe(c.in,p));
        if(c.s.flameFrames>c.s.flameElapsed)return;
        for(unsigned p=0;p<pipes;++p)c.e.followFade(c.base+FlameSlot+p);
        c.s.flameElapsed=FlameIdle;
    }
    // Slot of a handle: smoke at its wheel's slot (kart 0/1, bike rear 1), kart sparks 2..5,
    // bike sparks L/R/C 2..4.
    static unsigned slotOf(Fx fx,unsigned wheel=0){
        switch(fx){case Smoke:return wheel;case BlueL:case BikeL:return 2;case BlueR:case BikeR:return 3;case OrangeL:case BikeC:return 4;default:return 5;}
    }
    bool emits(const Ctx& c,unsigned w) const { return c.in.floor[w]||c.s.air[w]<AirFrames; }
    // 0x806975D4 (create, setMtx, setTranslate, setScale when +0xb0, update) and 0x80697788
    // (the same on a live instance after followFade). The Spark01S handles take setScale.
    void spark(Ctx& c,Fx fx,EffectFrame f,bool emit){
        const unsigned slot=c.base+slotOf(fx);float& scale=c.s.eggScale[slotOf(fx)];
        if(emit){
            if(!c.e.alive(slot))scale=1;
            if(c.in.scaled)scale=c.in.scale;
            f.scale=scale;c.e.create(slot,spec_[fx],f,c.group);c.e.move(slot,f);
            sparked_|=c.e.alive(slot);
        }else if(c.e.alive(slot)){
            if(c.in.scaled)scale=c.in.scale;
            f.scale=scale;c.e.followFade(slot);c.e.move(slot,f);
        }
    }
    void kill(Ctx& c,Fx fx){c.e.kill(c.base+slotOf(fx));}
    // 0x806A19D8 / 0x806A1B68: rk_driftSmoke at the wheel's previous position (+0x7d4) with
    // the kart's axes, its ratio and life, and the particle scale +0x738 unless pressed.
    void smoke(Ctx& c,unsigned w,bool emit){
        const unsigned slot=c.base+slotOf(Smoke,w);
        EffectFrame f=c.in.pose;f.origin=c.s.prev[w];f.scale=1;
        if(emit&&emits(c,w)){
            c.e.create(slot,spec_[Smoke],f,c.group);c.e.move(slot,f);
            c.e.setEmissionRatio(slot,SmokeRatio);c.e.setParticleLife(slot,SmokeLife);
            smoked_|=c.e.alive(slot);
        }else{
            if(!c.e.alive(slot))return;
            c.e.followFade(slot);c.e.move(slot,f);
        }
        if(c.in.scaled&&!c.in.pressed)c.e.setParticleScale(slot,c.in.scale,c.in.scale);
    }
    EffectFrame at(const Ctx& c,Vec3 origin,bool upright=false) const {
        EffectFrame f=c.in.pose;f.origin=origin;
        if(upright){
            // Inside-drift bike +0x818 (0x80693E94 -> 0x806B41E0): up = the rear floor
            // normal, forward = the pose's Z made perpendicular to it, X = up x forward.
            const Vec3 up=c.in.rearFloorNormal;
            f.z=normalized(tangent(c.in.pose.z,up),c.in.pose.z);f.y=up;f.x=cross(up,f.z);
        }
        return f;
    }
    // 0x80698028, the kart's drift group for a remote racer's handles.
    void kartStep(Ctx& c){
        const Input& in=c.in;State& s=c.s;
        auto side=[&](Fx fx,unsigned w,bool emit){spark(c,fx,at(c,in.wheel[w]),emit&&emits(c,w));};
        if(in.driftState!=1&&!in.driftAuto){
            smoke(c,0,false);smoke(c,1,false);
            if(in.driftState==2){
                if(s.prevState==1)s.counter=0;
                if(s.counter>SparkDelay){side(BlueL,0,true);side(BlueR,1,true);}
                ++s.counter;
                side(OrangeL,0,false);side(OrangeR,1,false);
            }else if(in.driftState==3){
                if(s.prevState==2)s.counter=0;
                if(s.counter>SparkDelay){side(OrangeL,0,true);side(OrangeR,1,true);}
                ++s.counter;
                kill(c,BlueL);kill(c,BlueR);
            }else{
                side(BlueL,0,false);side(BlueR,1,false);side(OrangeL,0,false);side(OrangeR,1,false);
            }
        }else{
            smoke(c,0,true);smoke(c,1,true);
            side(BlueL,0,false);side(BlueR,1,false);side(OrangeL,0,false);side(OrangeR,1,false);
        }
    }
    // 0x806A2F20 for the remote handles: direction -1 and 1 reach LB/RB only in the
    // (1,3,3,5) call (cpuSides), 0 always reaches CB; emitted while the rear wheel is down
    // or briefly airborne, otherwise followFade.
    void bikeSpark(Ctx& c,int dir,bool cpuSides,Vec3 origin){
        const Fx fx=dir==0?BikeC:!cpuSides?FxCount:dir<0?BikeL:BikeR;
        if(fx!=FxCount)spark(c,fx,at(c,origin,c.in.insideBike),emits(c,1));
    }
    // 0x806A3184 x3: followFade of the three sides at the rear wheel.
    void bikeFadeAll(Ctx& c){for(Fx fx:{BikeR,BikeL,BikeC})spark(c,fx,at(c,c.in.wheel[1],c.in.insideBike),false);}
    // 0x80698F70, the bike's drift group for a remote racer's handles (wheel 1 is the rear).
    void bikeStep(Ctx& c){
        const Input& in=c.in;State& s=c.s;
        const int dir=in.hopStickX;
        const Vec3 rear=in.wheel[1],kartRearY{in.pose.origin.x,rear.y,in.pose.origin.z};
        if(in.driftState!=1&&!in.driftAuto){
            if(in.driftState==2||in.driftState==3){
                if(!in.insideBike)smoke(c,1,false);
                else bikeSpark(c,dir,false,kartRearY);
                if(s.prevState==1)s.counter=0;
                ++s.counter;
                if(s.counter>SparkDelay)bikeSpark(c,in.insideBike?-dir:dir,true,rear);
            }else{
                if(!in.insideBike)smoke(c,1,false);
                bikeFadeAll(c);
            }
        }else{
            if(!in.insideBike){
                smoke(c,1,true);
                if(in.mtCharge>=ChipCharge)bikeSpark(c,dir,false,rear);
            }else{
                bikeSpark(c,dir,false,kartRearY);bikeSpark(c,dir,false,rear);
                if(in.mtCharge>=ChipCharge)bikeSpark(c,-dir,false,rear);
            }
            bikeFadeAll(c);
        }
    }
    std::array<int,FxCount> spec_{};
    std::array<State,MaxRacers> state_{};
    std::array<bool,Blocks> used_{};
    unsigned sparkFrames_=0,smokeFrames_=0,flameStarts_=0;
    bool sparked_=false,smoked_=false;
};
}
