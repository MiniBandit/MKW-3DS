#pragma once
#include "mkw/race_effects.hpp"
#include "mkw/kart_wall_contact.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mkw {
// One racer's KartEffect power-up and slipstream emitters (RMCU KartEffect,
// docs/POWERUPS_SOURCE.md, docs/VFX_RESEARCH.md "Slipstream"). Presentation only: it reads
// kart status and item events and tells a sink which effect instances to create, move,
// colour, fade and kill. The sink picks the S variants for other racers.
enum class KartFx : uint8_t {
    Star,          // 0x204 rk_star
    Killer,        // 0x208 rk_killer
    Killer1T,      // 0x20c rk_killer1T
    Thunder,       // 0x210 rk_thunder
    KaminariGumo,  // 0x214 rk_kaminariGumo
    BiriLightning, // 0x218 rk_thunderBiri
    BiriCloud,     // 0x21c rk_thunderBiri
    Kyodai,        // 0x22c rk_kyodai
    Pow,           // 0x230 rk_pow
    Pow1,          // 0x234 rk_pow1
    PowUp,         // 0x238 rk_powUp
    SlipStreamPre, // 0x1fc rk_slipStreamPre, drafting
    SlipStream,    // 0x200 rk_slipStream, the slipstream boost
    CollisionHit,  // 0x29c rk_hitA, kart contact / wall burst
    SpinHalfL,     // 0x260 rk_spinHalfL, one spin, positive direction
    SpinL,         // 0x264 rk_spinL, multiple spins, positive direction
    SpinHalfR,     // 0x268 rk_spinHalfR, one spin, negative direction
    SpinR,         // 0x26c rk_spinR, multiple spins, negative direction
    WaterWave,     // 0x1f4 rk_wave, surface wake
    WaterBubbles,  // 0x28c rk_awa, submerged kart
    WaterSpinHalf, // 0x274 rk_spinWaterHalf
    WaterSpin,     // 0x278 rk_spinWater
    FakeBoxBreak,  // 0x248 rk_niseBox, hit fragments follow the kart pose
    FakeBoxVanish, // 0x244 rk_niseBoxVan, immunity smoke follows translation
    ItemBoxBreak,  // 0x27c rk_itemBox, pickup shards follow the contacted racer
    FakeBoxSpark,  // 0x240 rk_niseBoxBiri, the original smooth electrical ribbon
    Crash,         // 0x1f0 rk_crash, launch-reaction stars
    WaterCrash,    // 0x270 rk_crashWater, launch reaction while Diving
    WaterLanding,  // 0x24c rk_pochaWaterMini, floor contact in water
    WaterWheelL,   // 0x330 rk_water, rear-wheel sprays (bike uses the right only)
    WaterWheelR,   // 0x334 rk_water
    WaterFall,     // course pocha archive: large drowning splash, fixed world origin
    Catapult,      // 0x1f8 rk_uchiage, Poihana's first throw contact
    RainbowEntry,  // 0x228 rk_entry, original course-only reentry flame
    Count
};
inline constexpr unsigned KartFxCount=unsigned(KartFx::Count);
// Each effect's index in KartEffect's handle table at +0x1f0: (offset above - 0x1f0) / 4.
inline constexpr uint8_t KartFxTableIndex[KartFxCount]={5,6,7,8,9,10,11,15,16,17,18,3,4,43,28,29,30,31,1,39,33,34,22,21,35,20,0,32,23,80,81,255,2,14};
// 0x8069146c with argument 0, the kart effect LOD's kill, spares indices 5..12, 14..19, 27
// and 35: every power-up here and the item box hit; it kills the slipstream (3, 4).
constexpr bool keptByLodKill(unsigned tableIndex){
    return (tableIndex>=5&&tableIndex<=12)||(tableIndex>=14&&tableIndex<=19)||tableIndex==27||tableIndex==35||tableIndex==255;
}

// What KartEffect copies from the kart (0x8068fa0c) and the item events of this step.
struct KartFxInput {
    Vec3 position{},right{1,0,0},up{0,1,0},forward{0,0,1}; // +0x6d0 and the pose +0x6f4
    float scale=1;        // +0x72c getScale
    float initialY=0;     // +0x74 bsp().initialYPos
    float bulletOffset=0; // the Bullet model's centre along up (KartKiller matrix)
    // Status 63 Star, 79 Mega, 91 Bullet, 71 Shocked, 4 BeforeRespawn.
    bool star=false,mega=false,bullet=false,shocked=false,beforeRespawn=false;
    bool hasRainbowEntry=false; // Original course entry dependency.
    // KartAction: the current action, its frame and the POW dodge (+0xcc bit 1);
    // powPending is KartMove::calcPow's POW-target-in-window test (0x8057a844).
    int action=-1;uint32_t actionFrame=0;bool powDodged=false,powPending=false;
    uint32_t spinSerial=0,spinRotations=0;bool spinLeft=false;
    // Item events: a Lightning bolt (fn_1_288374), the Thunder Cloud's strike (0x80579d88)
    // and a POW stomp wave on a target (stomps 1 and 2, 0x807d9b10).
    bool lightningBolt=false,cloudBolt=false,powWave=false;
    // HandleFibCollision (8056e40c) starts these on the contacted racer,
    // regardless of the box owner's identity. The existing sound event
    // records the same immunity decision before the item disappears.
    bool fakeBoxHit=false,fakeBoxVanish=false;
    bool itemBoxHit=false;
    // DriverController::get-effect-matrix (807c4e08) supplies only the
    // origin; KartEffect overwrites its axes with the kart pose. False uses
    // the original pose fallback (806a6b24). Local driver animation supplies
    // its cached skeletal root plus the physics-position delta and velocity.
    bool hasFakeBoxDriverOrigin=false;Vec3 fakeBoxDriverOrigin{};
    // Slipstream (0x806972c0): state bits 64 drafting and 65 boost (KartState +0xc bits 0
    // and 1; Kinoko KartMove::slipstreamCharging/Boost) and status 32 InAction (+0xb9).
    bool drafting=false,slipBoost=false,inAction=false;
    // The effect matrix +0xc4 (0x80690234) and scale (0x806aefd4): the velocity +0x6dc
    // (KartDynamics, Kart::Link::GetSpeed), getScale before it is made uniform, and +0x49:
    // action 14 or status 80 (Crushed).
    Vec3 velocity{},kartScale{1,1,1};
    float baseSpeed=50; // Stats speed, KartEffect +0x6f0 (minimum 50)
    bool pressed=false;
    // Effects::Player hits during this step's kart collision: a counted wall contact
    // (0x8069d1d8, countedWallContact) and a bush (0x806a221c) with the capped speed ratio.
    bool wallHit=false,bushHit=false;
    bool kartContact=false,otherContactFast=false;
    Vec3 contact{};
    bool objectHit=false,poihanaThrow=false;
    Vec3 objectContact{};
    float speedRatio=0;
    // UpdateWaterEffects (80698368), the original height probe 8058ab84
    // and rear wheel edges. Sea presence is separate from its height.
    bool diving=false,hasSea=false;
    bool hasPocha=false;
    float pochaHeight=-60000.f; // Effects::Mgr::Init height-table sentinel.
    float seaHeight=0,waterBodyHeight=0,waterRearHeight=0,waterBaseSpeed=50;
    // KartEffect +0xc halves emission for the reduced screen mode;
    // +0xb suppresses remote wakes in a split-screen mode, not LOD +7.
    bool waterReducedScreen=false,waterSplitScreen=false;
    bool bike=false,groundStart=false,hardLanding=false,jumpPad=false;
    bool waterKcl=false,waterWheelKcl[2]{},waterDeep=false,chargingSSMT=false,countdownAccelerate=false;
    Vec3 waterRear[2]{};
    uint32_t crashSerial=0;
};

// EGG::Effect calls on one racer's slots.
class KartFxSink {
public:
    virtual ~KartFxSink()=default;
    virtual bool alive(KartFx fx) const=0;
    virtual void create(KartFx fx,const EffectFrame& frame)=0; // no-op while the instance runs
    virtual void setFrame(KartFx fx,const EffectFrame& frame)=0;
    virtual void followFade(KartFx fx)=0;
    virtual void fade(KartFx fx){followFade(fx);}
    virtual void kill(KartFx fx)=0;
    virtual void setColor(KartFx fx,const uint8_t rgba[4])=0; // +0x50, active managers only
    virtual void setParticleLife(KartFx,uint16_t){}
    virtual void setParticleScale(KartFx,float,float){}
    virtual void setEmissionRatio(KartFx,float){}
    virtual void setSpecificVelocity(KartFx,float){}
};

class KartItemEffects {
public:
    // Timer resets of KartEffect init (0x8068a790): bolts idle at 690 and 540, POW at 1000.
    static constexpr int32_t BoltEnd=690,CloudEnd=540,PowIdle=1000,PowEnd=690;
    static constexpr int32_t BoltWindow=70,BoltRecent=64,CrackleDelay=71;
    static constexpr int PowAction=11;
    static constexpr uint32_t PowHitFrame=8; // .data 0x808b11fc
    // KartEffect +0x7b4..+0x7b7: frames since the last wall, object, kart-contact and bush
    // hit effect, counted to 100 by 0x80690234.
    enum Hit : uint8_t {WallHit,ObjectHit,KartHit,BushHit,HitCount};
    static constexpr int8_t HitFramesMax=100,HitGap=30,HitSettle=60;
    static constexpr int32_t SlipFadeFrames=10; // +0x8c runs 0..9, alpha 255 - 25 * frame
    static constexpr float PressScale=0.7f;     // +0xac4 (0x8068a804)

    void reset(unsigned index,bool local,bool ghost=false){
        *this={};index_=uint8_t(index);local_=local;ghost_=ghost;
    }
    // One KartEffect update (0x8068f8fc) after the kart and item updates of the step. lod is
    // this frame's kart effect LOD flag (+7, KartEffectLod), set before the update.
    void step(const KartFxInput& in,KartFxSink& sink,bool lod=false){
        // The state copy (0x8068fa0c) clears +9, which the updates below set while one of the
        // power-up effects is active (retail sets it too under LIVE_VIEW, +0x13b, never in a
        // race). It also marks BeforeRespawn's first frame (+0x98).
        keepAlive_=false;
        if(ghost_)return;
        // Poihana's contact callback (806a325c) creates +1f8 exactly once
        // per throw request and releases +1f4's wave emission. It does not
        // restart the finite effect on every frame of action8.
        if(in.poihanaThrow&&!lod){
            sink.create(KartFx::Catapult,pose(in));
            if(sink.alive(KartFx::WaterWave))sink.fade(KartFx::WaterWave);
        }
        // The original updater806a332c copies the kart pose and calls Calc;
        // no separate emitter scale override is applied to this effect.
        if(sink.alive(KartFx::Catapult)){
            if(in.beforeRespawn)sink.kill(KartFx::Catapult);
            else sink.setFrame(KartFx::Catapult,pose(in));
        }
        const bool crash=in.crashSerial!=crashSerial_;
        crashSerial_=in.crashSerial;
        if(crash&&!lod&&!in.beforeRespawn&&in.action>=2&&in.action<=6){
            auto f=pose(in);float scale=1;if(effectScale(in,scale))f.scale=scale;
            sink.create(KartFx::Crash,f);
            if(in.diving)sink.create(KartFx::WaterCrash,scaled(world(in),in));
        }
        if(sink.alive(KartFx::Crash)){
            if(in.beforeRespawn)sink.kill(KartFx::Crash);
            else {auto f=pose(in);float scale=1;if(effectScale(in,scale))f.scale=scale;sink.setFrame(KartFx::Crash,f);keepAlive_=true;}
        }
        // Original +0xad is an actual box-contact event, not an inventory
        // roulette edge. It restarts +0x27c and follows the kart even under
        // item-effect LOD (80698a94 / 80698c98, table index 35).
        if(in.itemBoxHit&&!in.beforeRespawn){
            sink.kill(KartFx::ItemBoxBreak);sink.create(KartFx::ItemBoxBreak,pose(in));
        }
        if(sink.alive(KartFx::ItemBoxBreak)){
            if(in.beforeRespawn)sink.kill(KartFx::ItemBoxBreak);
            else {
                sink.setSpecificVelocity(KartFx::ItemBoxBreak,boxFragmentSpeed(in));
                sink.setFrame(KartFx::ItemBoxBreak,pose(in));keepAlive_=true;
            }
        }
        // Original hit callback 806a685c / 806a6d4c. These finite emitters
        // stay attached to the racer after the item object has been freed.
        // Fragment and immunity-smoke creation are both independent of +7;
        // only the separate electrical ribbon uses that LOD gate.
        if(!in.beforeRespawn){
            if(in.fakeBoxHit)sink.create(KartFx::FakeBoxBreak,pose(in));
            if(in.fakeBoxHit&&!lod){
                if(!sink.alive(KartFx::FakeBoxSpark))fakeBoxSparkScale_=1;
                float scale=1;if(effectScale(in,scale))fakeBoxSparkScale_=scale;
                sink.create(KartFx::FakeBoxSpark,fakeBoxSparkFrame(in));
            }
            if(in.fakeBoxVanish){
                sink.kill(KartFx::FakeBoxVanish);sink.create(KartFx::FakeBoxVanish,world(in));
            }
        }
        if(sink.alive(KartFx::FakeBoxSpark)){
            if(in.beforeRespawn)sink.kill(KartFx::FakeBoxSpark);
            else {sink.setFrame(KartFx::FakeBoxSpark,fakeBoxSparkFrame(in));keepAlive_=true;}
        }
        for(KartFx fx:{KartFx::FakeBoxBreak,KartFx::FakeBoxVanish})if(sink.alive(fx)){
            if(in.beforeRespawn)sink.kill(fx);
            else {
                if(fx==KartFx::FakeBoxBreak)sink.setSpecificVelocity(fx,boxFragmentSpeed(in));
                sink.setFrame(fx,fx==KartFx::FakeBoxBreak?pose(in):world(in));
                keepAlive_=true;
            }
        }
        // RMCU startRotation 0x80563b98 -> CreateSpinEffect 0x806a2370.
        // This is a start event, not a per-frame action test: POW can begin
        // its rotation after its hit, and a second spin must retrigger it.
        bool waterSpinStart=false;
        if(in.spinSerial!=spinSerial_){
            spinSerial_=in.spinSerial;
            if(in.spinRotations&&!lod&&!in.beforeRespawn){
                const KartFx fx=in.spinLeft?(in.spinRotations==1?KartFx::SpinHalfL:KartFx::SpinL):
                                              (in.spinRotations==1?KartFx::SpinHalfR:KartFx::SpinR);
                auto f=pose(in);float scale=1;if(effectScale(in,scale))f.scale=scale;
                sink.create(fx,f);
                waterSpinStart=true;
            }
        }
        const bool respawnStart=in.beforeRespawn&&!wasBeforeRespawn_;
        wasBeforeRespawn_=in.beforeRespawn;
        // Effects::Player USA8069a424: 35-frame delay, drop >700 units,
        // and local downward velocity <-60. Original orientation is Rx(pi/2).
        if(respawnStart){entryHeight_=in.position.y;entryFrames_=0;entryStarted_=false;}
        if(in.hasRainbowEntry&&in.beforeRespawn){
            if(entryFrames_<36)++entryFrames_;
            auto f=world(in);f.x={1,0,0};f.y={0,0,1};f.z={0,-1,0};f.scale=1;
            if(!entryStarted_&&entryFrames_>=35&&in.position.y<entryHeight_-700.f&&(!local_||in.velocity.y<-60.f)){
                sink.create(KartFx::RainbowEntry,f);entryStarted_=true;
            }
            if(entryStarted_)sink.setFrame(KartFx::RainbowEntry,f);
        }else if(entryStarted_){sink.kill(KartFx::RainbowEntry);entryStarted_=false;}

        if(respawnStart&&(in.hasPocha||(in.hasSea&&in.diving))){
            // CreatePochaEffect 806a7708: predicted X/Z and authored
            // surface Y. The splash stays where the kart entered the sea.
            auto f=world(in);f.origin=in.position+in.velocity;f.origin.y=in.hasPocha?in.pochaHeight:in.seaHeight;f.scale=in.scale;
            sink.kill(KartFx::WaterFall);sink.create(KartFx::WaterFall,f);
        }
        // During the kart collision, before this update: Effects::Player counts the wall
        // contacts (+0x7b0, 0x8069d1d8) and OnBushCollision (0x806a221c) restarts the bush
        // timer for the player's own kart.
        wallContacts_+=in.wallHit;
        if(local_&&!in.beforeRespawn&&in.objectHit&&hitFrames_[ObjectHit]>HitGap){
            auto f=pose(in);f.origin=in.objectContact;float scale=1;
            if(effectScale(in,scale))f.scale=scale;
            if(sink.alive(KartFx::CollisionHit))sink.kill(KartFx::CollisionHit);
            sink.create(KartFx::CollisionHit,f);hitFrames_[ObjectHit]=0;
        }
        // OnKartCollision (USA 806a1fa0): local racer, either kart above
        // speed 30, more than ten frames since the burst, at the contact.
        if(local_&&!in.beforeRespawn&&in.kartContact&&
           (length(in.velocity)>30.f||in.otherContactFast)&&kartContactFrames_>10){
            auto f=pose(in);f.origin=in.contact;float scale=1;
            if(effectScale(in,scale))f.scale=scale;
            if(sink.alive(KartFx::CollisionHit))sink.kill(KartFx::CollisionHit);
            sink.create(KartFx::CollisionHit,f);kartContactFrames_=0;hitFrames_[KartHit]=0;
        }
        kartContactFrames_=std::min(kartContactFrames_+1,100);
        if(local_&&in.bushHit&&hitFrames_[BushHit]>HitGap&&in.speedRatio>=0.5f)hitFrames_[BushHit]=0;
        // KartMove::calcPow (0x8057a844): the hit at action 11 frame 8 restarts +0x54 unless
        // dodged (0x806a6044); a pending POW keeps rk_powUp alive (0x806a5d40).
        if(in.action==PowAction&&in.actionFrame==PowHitFrame&&!in.powDodged&&!in.beforeRespawn)powHit_=0;
        if(in.powPending){
            powUp_=true;
            if(!in.beforeRespawn)sink.create(KartFx::PowUp,scaled(pose(in),in));
        }
        if(in.powWave&&!in.beforeRespawn)powWave_=0;  // 0x806a6080
        if(in.lightningBolt)lightning_=0;             // fn_1_288374
        if(in.cloudBolt)cloud_=0;                      // 0x80579d88
        if(in.mega&&!wasMega_)sink.create(KartFx::Kyodai,scaled(pose(in),in)); // 0x806a6720
        wasMega_=in.mega;
        // While the Bullet runs its update (0x80698c98) ends with its own wall hit and the
        // update returns: no LOD edge, no matrix, no slipstream, no reset of +0x7b0.
        if(bulletStep(in,sink)){wallHit(in,sink);return;}
        normalStep(in,sink);
        // +7's rising edge (+8) kills every effect but the power-ups (0x8069146c, argument 0).
        if(lod&&!wasLod_)lodKill(sink);
        wasLod_=lod;
        if(lod)return;
        // 0x80690234 counts the hit timers and builds the effect matrix; 0x80691720 updates
        // the slipstream first and checks for a wall hit last; 0x80692868 clears +0x7b0.
        for(auto& f:hitFrames_)f=int8_t(std::min<int>(f+1,HitFramesMax));
        slipstream(in,respawnStart,sink);
        water(in,waterSpinStart,sink);
        wallHit(in,sink);
        wallContacts_=0;
    }
    // +9: an effect that keeps this racer's effect group calculated and drawn under the LOD
    // (KartEffectLod::calcs/draws): the Star, a bolt or crackle, or a live rk_pow, rk_pow1,
    // rk_powUp or rk_kyodai. The Bullet's own +9 (0x806a4aa4) is set before the state copy
    // clears it.
    bool keepAlive() const { return keepAlive_; }
    // The slipstream emitters' matrix (+0xc4, 0x80690234): the pose, or above speed 1 and
    // 60 frames after every hit effect, a frame along the velocity (0x806af81c). The
    // translation is the kart position.
    EffectFrame effectFrame(const KartFxInput& in) const {
        const float speed=length(in.velocity); // +0x6e8
        bool recent=false;
        for(auto f:hitFrames_)recent|=f<HitSettle;
        if(speed<=1.f||recent)return pose(in);
        const Vec3 v=in.velocity*(1.f/speed);
        // Z is the direction with |y| raised to 0.001; X is horizontal, to the right of Z.
        Vec3 z=v;
        if(std::fabs(z.y)<0.001f)z.y=0.001f;
        const Vec3 flat{v.x,0.f,v.z};
        if(length(flat)==0.f)return pose(in); // straight up or down: retail's X would be zero
        const Vec3 h=flat*(1.f/length(flat));
        Vec3 x=cross(h,z);
        if(z.y>0.f)x=-x;
        x=x*(1.f/length(x));
        return frame(in.position,x,cross(z,x),z);
    }
    // 0x806aefd4: whether the slipstream emitters get setScale (+0xb0) and the uniform
    // scale (+0x72c) they get.
    static bool effectScale(const KartFxInput& in,float& scale){
        if(in.pressed){scale=PressScale;return true;}
        const bool scaled=std::fabs(in.kartScale.y-1.f)>=0.05f;
        if(!scaled||std::fabs(in.kartScale.x-1.f)>=0.05f)scale=in.kartScale.x;
        else scale=in.kartScale.y>=PressScale?in.kartScale.y:PressScale;
        return scaled;
    }
    int8_t hitFrames(Hit hit) const { return hitFrames_[hit]; }
    uint32_t wallContacts() const { return wallContacts_; }
    // Original +0xaac=200, +0xab0=0 from 80898fd0's constant table:
    // box shards inherit specific-direction velocity, not emission count.
    static float boxFragmentSpeed(const KartFxInput& in){
        const float top=std::max(in.baseSpeed,50.f);
        return std::clamp(length(in.velocity),0.f,top)*(200.f/top);
    }
    int32_t slipFade() const { return slipFade_; }
    int32_t lightningTimer() const { return lightning_; }
    int32_t cloudTimer() const { return cloud_; }
    int32_t powHitTimer() const { return powHit_; }
    int32_t powWaveTimer() const { return powWave_; }
private:
    // Original UpdateWaterEffects / CreateWave (80698368 / 806a33d0).
    // Wave is world aligned at the current sea; bubbles follow the kart.
    // Particle size uses EGG's manager modifier, not emitter/world scale.
    void water(const KartFxInput& in,bool spinStart,KartFxSink& sink){
        EffectFrame wave;wave.origin={in.position.x,in.seaHeight,in.position.z};
        const bool contact=in.hasSea&&in.diving;
        const bool surface=contact&&in.waterBodyHeight>=in.seaHeight&&in.waterRearHeight<in.seaHeight;
        float size=1;effectScale(in,size);
        if(surface&&(!in.waterSplitScreen||local_)){
            const float top=std::max(in.waterBaseSpeed,50.f);
            const float speed=std::clamp(length(in.velocity),0.f,top);
            const float maxEmit=local_&&!in.waterSplitScreen?.25f:.15f;
            float ratio=.075f+speed*((maxEmit-.075f)/top);
            if(in.waterReducedScreen)ratio*=.5f;
            const uint16_t life=uint16_t(60.f-speed*((60.f-12.f)/top));
            sink.create(KartFx::WaterWave,wave);sink.setFrame(KartFx::WaterWave,wave);
            sink.setEmissionRatio(KartFx::WaterWave,ratio);
            sink.setParticleLife(KartFx::WaterWave,life);
            sink.setParticleScale(KartFx::WaterWave,size,size);
        }else if(sink.alive(KartFx::WaterWave)){
            if(contact)sink.followFade(KartFx::WaterWave);
            else sink.fade(KartFx::WaterWave);
            // With no Diving flag retail Fade() leaves the last transform.
            if(contact)sink.setFrame(KartFx::WaterWave,wave);
        }
        auto bubbles=world(in);bubbles.scale=size;
        if(contact&&!surface){
            sink.create(KartFx::WaterBubbles,bubbles);
            sink.setFrame(KartFx::WaterBubbles,bubbles);
        }else if(sink.alive(KartFx::WaterBubbles))sink.fade(KartFx::WaterBubbles);
        // Original 806960a8: a hard floor contact splashes in water;
        // a regular GroundStart splashes while only partly immersed.
        const bool splash=(in.hardLanding&&(in.waterKcl||contact))||
                          (in.groundStart&&(in.waterKcl||(contact&&!in.waterDeep)));
        auto splashFrame=world(in);splashFrame.scale=size;
        if(splash&&!in.beforeRespawn)sink.create(KartFx::WaterLanding,splashFrame);
        for(KartFx fx:{KartFx::WaterCrash,KartFx::WaterLanding})if(sink.alive(fx)){
            if(in.beforeRespawn)sink.kill(fx);
            else sink.setFrame(fx,splashFrame);
        }
        // Original CreateWaterEffect 806ab2e0: existing rk_water at
        // rear-wheel edges, speed emission 0..0.5 (local), 0..0.25 (S).
        const float top=std::max(in.waterBaseSpeed,50.f);
        float speed=std::clamp(length(in.velocity),0.f,top);
        if(!in.waterSplitScreen&&(in.chargingSSMT||in.countdownAccelerate))speed=top*.5f;
        float ratio=speed/top*(local_?.5f:.25f);
        if(in.waterReducedScreen)ratio*=.5f;
        for(unsigned wheel=0;wheel<2;++wheel){
            const KartFx fx=wheel?KartFx::WaterWheelR:KartFx::WaterWheelL;
            auto f=pose(in);f.origin=in.waterRear[wheel];
            const bool spray=!in.beforeRespawn&&(!in.bike||wheel==1)&&
                             (in.waterWheelKcl[wheel]||(surface&&!in.waterDeep));
            if(spray){sink.create(fx,f);sink.setEmissionRatio(fx,ratio);}
            else if(sink.alive(fx))sink.followFade(fx);
            if(sink.alive(fx)){sink.setFrame(fx,f);sink.setParticleScale(fx,size,size);}
        }
        if(contact&&spinStart){
            const KartFx fx=in.spinRotations==1?KartFx::WaterSpinHalf:KartFx::WaterSpin;
            sink.create(fx,bubbles);
        }
        // The finite spin splashes finish even when the kart leaves water.
        for(KartFx fx:{KartFx::WaterSpinHalf,KartFx::WaterSpin})
            if(sink.alive(fx))sink.setFrame(fx,bubbles);
    }
    // The wall hit (sparks and rumble; the end of 0x80691720 and 0x80699444 in the Bullet
    // update): a counted contact since +0x7b0 was last cleared, speed above 30 and 30 frames
    // since the last.
    void wallHit(const KartFxInput& in,KartFxSink& sink){
        if(wallImpactReady(in.beforeRespawn,wallContacts_>0,length(in.velocity),hitFrames_[WallHit])){
            hitFrames_[WallHit]=0;
            if(local_){
                auto f=pose(in);float scale=1;if(effectScale(in,scale))f.scale=scale;
                if(sink.alive(KartFx::CollisionHit))sink.kill(KartFx::CollisionHit);
                sink.create(KartFx::CollisionHit,f);
            }
        }
    }
    void lodKill(KartFxSink& sink) const {
        for(unsigned fx=0;fx<KartFxCount;++fx)
            if(!keptByLodKill(KartFxTableIndex[fx])&&sink.alive(KartFx(fx)))sink.kill(KartFx(fx));
    }
    static EffectFrame frame(Vec3 origin,Vec3 x,Vec3 y,Vec3 z,float scale=1){
        EffectFrame f;f.origin=origin;f.x=x;f.y=y;f.z=z;f.scale=scale;return f;
    }
    // The pose matrix (+0x6f4), as Mega, rk_powUp and the POW frames use it.
    static EffectFrame pose(const KartFxInput& in){return frame(in.position,in.right,in.up,in.forward);}
    EffectFrame fakeBoxSparkFrame(const KartFxInput& in) const {
        auto f=pose(in);if(in.hasFakeBoxDriverOrigin)f.origin=in.fakeBoxDriverOrigin;
        f.scale=fakeBoxSparkScale_;return f;
    }
    // EGG keeps setScale apart from setMtx, so a scaled instance stays scaled as it follows.
    static EffectFrame scaled(EffectFrame f,const KartFxInput& in){f.scale=in.scale;return f;}
    // Star and crackle: up, forward and right as the axes (0x806a464c, 0x806a5244).
    static EffectFrame body(const KartFxInput& in){return frame(in.position,in.up,in.forward,in.right,in.scale);}
    // A world-aligned frame at the kart: create, then only setTranslate (0x8069971c).
    static EffectFrame world(const KartFxInput& in){EffectFrame f;f.origin=in.position;return f;}
    // POW frames ride the kart's bottom: position - up * initialYPos (0x806a60a8).
    static EffectFrame bottom(const KartFxInput& in){
        auto f=pose(in);f.origin=in.position-in.up*in.initialY;f.scale=in.scale;return f;
    }
    // Bullet: forward, up and -right at the Bullet's height over the kart (0x806a4be8).
    EffectFrame bulletFrame(const KartFxInput& in) const {
        return frame(in.position+Vec3{0,bulletHeight_,0},in.forward,in.up,-in.right);
    }
    // A finished effect: the player's own kart lets it fade while following,
    // any other kart's is killed.
    void stop(KartFx fx,const EffectFrame& follow,KartFxSink& sink) const {
        if(!sink.alive(fx))return;
        if(!local_){sink.kill(fx);return;}
        sink.followFade(fx);sink.setFrame(fx,follow);
    }
    // 0x80698c98: returns true while the Bullet runs (the normal update is skipped).
    bool bulletStep(const KartFxInput& in,KartFxSink& sink){
        if(in.bullet){
            // The Bullet object creates rk_killer and sets its matrix (0x805b05e8 ->
            // 0x806a4aa4), which records the height over the kart.
            const Vec3 centre=in.position+in.up*in.bulletOffset;
            bulletHeight_=centre.y-in.position.y;
            const EffectFrame f=frame(centre,in.forward,in.up,-in.right);
            sink.create(KartFx::Killer,f);sink.setFrame(KartFx::Killer,f);
            bolts(in,sink);powHit(in,sink);powWave(in,sink);powUp(in,sink);
        }
        if(!in.bullet&&wasBullet_)sink.create(KartFx::Killer1T,scaled(bulletFrame(in),in));
        if(!in.bullet)stop(KartFx::Killer,bulletFrame(in),sink);
        if(sink.alive(KartFx::Killer1T))sink.setFrame(KartFx::Killer1T,scaled(bulletFrame(in),in));
        wasBullet_=in.bullet;
        return in.bullet;
    }
    // 0x80690884.
    void normalStep(const KartFxInput& in,KartFxSink& sink){
        if(in.star){sink.create(KartFx::Star,body(in));sink.setFrame(KartFx::Star,body(in));keepAlive_=true;}
        else stop(KartFx::Star,body(in),sink);
        bolts(in,sink);powHit(in,sink);powWave(in,sink);powUp(in,sink);
        // Normal update (0x8069bfa0) uses the kart pose and keeps a finite
        // spin effect following until its particles finish. Lightning and
        // POW-up replace these particles immediately.
        for(KartFx fx:{KartFx::SpinHalfL,KartFx::SpinL,KartFx::SpinHalfR,KartFx::SpinR})if(sink.alive(fx)){
            if(in.beforeRespawn||lightning_<BoltWindow||cloud_<BoltWindow||sink.alive(KartFx::PowUp))sink.kill(fx);
            else sink.setFrame(fx,scaled(pose(in),in));
        }
        if(sink.alive(KartFx::Kyodai)){sink.setFrame(KartFx::Kyodai,scaled(pose(in),in));keepAlive_=true;} // 0x806a6770
    }
    // 0x8069971c: Lightning (+0x4c) and Thunder Cloud (+0x50) bolts and the shocked crackle.
    void bolts(const KartFxInput& in,KartFxSink& sink){
        if(in.beforeRespawn)lightning_=cloud_=BoltEnd;
        const int32_t stagger=int32_t(index_%6)+1;
        const bool lightningRecent=lightning_<BoltRecent;
        if(lightningRecent)cloud_=CloudEnd;
        if(stagger<=lightning_&&lightning_<BoltWindow){sink.create(KartFx::Thunder,world(in));sink.setFrame(KartFx::Thunder,world(in));keepAlive_=true;}
        else if(sink.alive(KartFx::Thunder))sink.kill(KartFx::Thunder);
        const bool cloudRecent=cloud_<BoltRecent;
        if(cloudRecent)lightning_=BoltEnd;
        if(stagger<=cloud_&&cloud_<BoltWindow){sink.create(KartFx::KaminariGumo,world(in));sink.setFrame(KartFx::KaminariGumo,world(in));keepAlive_=true;}
        else stop(KartFx::KaminariGumo,world(in),sink);
        // The crackle follows a bolt while the kart stays shocked and the other bolt is over.
        const int32_t crackle=int32_t(index_)+CrackleDelay;
        if(in.shocked&&!lightningRecent&&cloud_==CloudEnd&&crackle<=lightning_&&lightning_<BoltEnd)crackleOn(KartFx::BiriLightning,in,sink);
        else stop(KartFx::BiriLightning,body(in),sink);
        lightning_=std::min(lightning_+1,BoltEnd);
        if(in.shocked&&!cloudRecent&&lightning_==BoltEnd&&crackle<=cloud_&&cloud_<CloudEnd)crackleOn(KartFx::BiriCloud,in,sink);
        else stop(KartFx::BiriCloud,body(in),sink);
        cloud_=std::min(cloud_+1,CloudEnd);
    }
    void crackleOn(KartFx fx,const KartFxInput& in,KartFxSink& sink){
        sink.create(fx,body(in));sink.setFrame(fx,body(in));keepAlive_=true; // 0x806a5244, 0x806a56c0
    }
    // 0x806a60a8: rk_pow bursts once, staggered by the racer index, at the kart's bottom.
    void powHit(const KartFxInput& in,KartFxSink& sink){
        const bool start=int32_t(index_%6)+1==powHit_;
        powHit_=std::min(powHit_+1,PowEnd);
        if(start)sink.create(KartFx::Pow,bottom(in));
        if(sink.alive(KartFx::Pow)){sink.setFrame(KartFx::Pow,bottom(in));keepAlive_=true;}
    }
    // 0x806a62fc: rk_pow1 bursts once at index + 1 after a stomp wave.
    void powWave(const KartFxInput& in,KartFxSink& sink){
        const bool start=int32_t(index_)+1==powWave_;
        powWave_=std::min(powWave_+1,PowEnd);
        if(start)sink.create(KartFx::Pow1,bottom(in));
        if(sink.alive(KartFx::Pow1)){sink.setFrame(KartFx::Pow1,bottom(in));keepAlive_=true;}
    }
    // rk_powUp follows while the POW is pending, then fades or is killed (0x80690884); +9
    // while it was alive this update.
    void powUp(const KartFxInput& in,KartFxSink& sink){
        keepAlive_|=sink.alive(KartFx::PowUp);
        if(powUp_){if(sink.alive(KartFx::PowUp))sink.setFrame(KartFx::PowUp,scaled(pose(in),in));}
        else stop(KartFx::PowUp,scaled(pose(in),in),sink);
        powUp_=false;
    }
    // 0x806972c0: rk_slipStreamPre while drafting, faded (on every kart) when drafting
    // stops; rk_slipStream through the boost, then ten frames fading out by setColor alpha
    // before the kill. InAction and BeforeRespawn hide both.
    void slipstream(const KartFxInput& in,bool respawnStart,KartFxSink& sink){
        const EffectFrame m=effectFrame(in);
        float scale=1;
        const bool scaled=effectScale(in,scale);
        // One EGG::Effect call sequence: create (+0xc), setMtx (+0x78), setColor (+0x50),
        // setScale (+0x70) when +0xb0, update (+0x80). Creating a new instance resets the
        // wrapper (+0x84), dropping an earlier setScale; a running instance keeps it.
        auto egg=[&](KartFx fx,bool create,const uint8_t* color){
            EggScale& s=eggScale_[fx==KartFx::SlipStream];
            if(create){if(!sink.alive(fx))s={};sink.create(fx,withScale(m,s));}
            if(color)sink.setColor(fx,color);
            if(scaled){s.set=true;s.value=scale;}
            sink.setFrame(fx,withScale(m,s));
        };
        if(in.drafting&&!in.inAction&&!in.beforeRespawn)egg(KartFx::SlipStreamPre,true,nullptr);
        else if(sink.alive(KartFx::SlipStreamPre)){sink.followFade(KartFx::SlipStreamPre);egg(KartFx::SlipStreamPre,false,nullptr);}
        const bool visible=in.slipBoost&&!in.inAction&&!in.beforeRespawn;
        if(visible)egg(KartFx::SlipStream,true,nullptr);
        if((!visible&&slipVisible_)||(respawnStart&&in.slipBoost))slipFade_=0;
        if(slipFade_!=-1){
            const uint8_t color[4]={255,255,255,uint8_t(255-25*slipFade_)};
            egg(KartFx::SlipStream,true,color);
            ++slipFade_;
        }
        if(slipFade_>=SlipFadeFrames){
            if(sink.alive(KartFx::SlipStream))sink.kill(KartFx::SlipStream);
            slipFade_=-1;
        }
        slipVisible_=visible;
    }
    struct EggScale { bool set=false; float value=1; };
    static EffectFrame withScale(EffectFrame f,const EggScale& s){f.scale=s.set?s.value:1.f;return f;}
    int32_t lightning_=BoltEnd,cloud_=CloudEnd,powHit_=PowIdle,powWave_=PowIdle;
    float bulletHeight_=0;
    uint8_t index_=0;
    bool local_=false,ghost_=false,wasBullet_=false,wasMega_=false,powUp_=false;
    // Object callbacks (+0x7b5) are handled by their course presenters.
    std::array<int8_t,HitCount> hitFrames_{};
    int kartContactFrames_=0; // +0x7b8, separate from the effect-matrix timer
    int32_t slipFade_=-1;          // +0x8c, -1 idle
    bool slipVisible_=false;       // +0x89
    float entryHeight_=0;unsigned entryFrames_=0;bool entryStarted_=false;
    bool wasBeforeRespawn_=false;  // +0x97
    bool wasLod_=false;            // +8, +7 at the last update outside a Bullet
    bool keepAlive_=false;         // +9
    uint32_t wallContacts_=0;      // +0x7b0
    uint32_t spinSerial_=0;
    uint32_t crashSerial_=0;
    float fakeBoxSparkScale_=1;
    std::array<EggScale,2> eggScale_{};
};

// KartItemEffects on RaceEffects: racer r owns KartFxCount slots from FirstSlot. The player's
// own kart uses the full emitters, every other racer the S variants (KartEffect 0x8068abbc).
// Racer r's instances live in effect group FirstGroup + r, as KartEffect's do (+0x134).
class KartFxSlots final : public KartFxSink {
public:
    static constexpr unsigned FirstSlot=32,MaxRacers=12,FirstGroup=2;
    static constexpr unsigned FirstFreeSlot=FirstSlot+MaxRacers*KartFxCount;
    static_assert(FirstSlot+MaxRacers*KartFxCount<=RaceEffects::SlotCount,"KartEffect slots");
    static_assert(FirstFreeSlot+2<=RaceEffects::SlotCount,"off-road dust slots");
    static_assert(FirstGroup+MaxRacers<=32,"one mask bit per group");
    static constexpr uint32_t groupBit(unsigned racer){return 1u<<(FirstGroup+racer);}
    bool bind(RaceEffects& effects){
        static constexpr const char* names[KartFxCount][2]={
            {"rk_star","rk_starS"},{"rk_killer","rk_killerS"},{"rk_killer1T","rk_killer1TS"},
            {"rk_thunder","rk_thunderS"},{"rk_kaminariGumo","rk_kaminariGumoS"},
            {"rk_thunderBiri","rk_thunderBiri"},{"rk_thunderBiri","rk_thunderBiri"},
            {"rk_kyodai","rk_kyodaiS"},{"rk_pow","rk_powS"},{"rk_pow1","rk_pow1S"},{"rk_powUp","rk_powUp"},
            // 0x8068abbc: the boost's full emitter also goes to split-screen players
            // (+0x11, +0x13); with one screen both pick by the player's own kart (+0xf).
            {"rk_slipStreamPre","rk_slipStreamPreS"},{"rk_slipStream","rk_slipStreamS"},
            {"rk_hitA","rk_hitAS"},
            {"rk_spinHalfL","rk_spinHalfLS"},{"rk_spinL","rk_spinLS"},
            {"rk_spinHalfR","rk_spinHalfRS"},{"rk_spinR","rk_spinRS"},
            {"rk_wave","rk_wave"},{"rk_awa","rk_awa"},
            {"rk_spinWaterHalf","rk_spinWaterHalfS"},{"rk_spinWater","rk_spinWaterS"},
            {"rk_niseBox","rk_niseBoxS"},{"rk_niseBoxVan","rk_niseBoxVan"},
            {"rk_itemBox","rk_itemBoxS"},{"rk_niseBoxBiri","rk_niseBoxBiri"},
            {"rk_crash","rk_crashS"},{"rk_crashWater","rk_crashWaterS"},
            {"rk_pochaWaterMini","rk_pochaWaterMini"},{"rk_water","rk_water"},{"rk_water","rk_water"},
            {"rk_pochaWater","rk_pochaWaterS"},{"rk_uchiage","rk_uchiageS"},{"rk_entry","rk_entry"}};
        effects_=&effects;
        for(unsigned i=0;i<KartFxCount;++i)for(unsigned j=0;j<2;++j)specs_[i][j]=effects.find(names[i][j]);
        for(unsigned i=0;i<KartFxCount;++i){
            if(i==unsigned(KartFx::WaterFall)||i==unsigned(KartFx::RainbowEntry))continue; // Course pocha dependency, checked by loader.
            for(int spec:specs_[i])if(spec<0)return false;
        }
        return effects.ready;
    }
    void select(unsigned racer,bool local){racer_=racer<MaxRacers?racer:0;local_=local;}
    bool alive(KartFx fx) const override { return effects_&&effects_->alive(slot(fx)); }
    void create(KartFx fx,const EffectFrame& frame) override {
        const int spec=specs_[unsigned(fx)][local_?0:1];
        if(effects_&&spec>=0)effects_->create(slot(fx),spec,frame,uint8_t(FirstGroup+racer_));
    }
    void setFrame(KartFx fx,const EffectFrame& frame) override { if(effects_)effects_->move(slot(fx),frame); }
    void followFade(KartFx fx) override { if(effects_)effects_->followFade(slot(fx)); }
    void fade(KartFx fx) override { if(effects_)effects_->fade(slot(fx)); }
    void kill(KartFx fx) override { if(effects_)effects_->kill(slot(fx)); }
    void setColor(KartFx fx,const uint8_t rgba[4]) override { if(effects_)effects_->setColor(slot(fx),rgba); }
    void setParticleLife(KartFx fx,uint16_t life) override { if(effects_)effects_->setParticleLife(slot(fx),life); }
    void setParticleScale(KartFx fx,float x,float y) override { if(effects_)effects_->setParticleScale(slot(fx),x,y); }
    void setEmissionRatio(KartFx fx,float ratio) override { if(effects_)effects_->setEmissionRatio(slot(fx),ratio); }
    void setSpecificVelocity(KartFx fx,float speed) override { if(effects_)effects_->setSpecificVelocity(slot(fx),speed); }
private:
    unsigned slot(KartFx fx) const { return FirstSlot+racer_*KartFxCount+unsigned(fx); }
    RaceEffects* effects_=nullptr;
    int specs_[KartFxCount][2]{};
    unsigned racer_=0;
    bool local_=false;
};
}
