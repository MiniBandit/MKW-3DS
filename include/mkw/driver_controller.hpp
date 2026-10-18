#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace mkw {
// One frame of the kart state the retail driver model (PlayerModel, PAL StaticR
// 0x807C6A0C..0x807D4400) reads, plus the events other systems send it.
// Presentation signals only. This class never changes simulation state.
struct DriverSignals {
    // Kart status bits (Kinoko eStatus / retail KartState).
    bool accelerate=false,brake=false;      // 0, 1
    bool drifting=false;                    // 3 DriftManual | 28 DriftAuto
    bool beforeRespawn=false;               // 4
    bool anyWheel=false,allWheels=false;    // 11, 12
    bool stickLeft=false,stickRight=false;  // 13, 24
    bool touchingGround=true,hop=false;     // 18, 19
    bool boost=false,rampBoost=false;       // 20, 31
    bool wheelie=false;                     // 29
    bool inAction=false,inCannon=false;     // 32, 36
    bool inATrick=false,overZipper=false;   // 38, 42
    bool burnout=false;                     // 50
    bool mtBoost=false;                     // 52: a drift mini-turbo's boost (KartMove +0x102 timer)
    bool chargingSsmt=false;                // 57
    bool slipstreamBoost=false;             // 65
    bool frozen=false;    // 77/78 InRespawn|AfterRespawn on N64 Sherbet Land: the wait clip stops
    int inkFrames=-1;     // fn_1_6ECA8 0x8057F05C: frames since Blooper ink landed, -1 without ink
    float speed=0;        // KartMove speed (signed)
    float speedRatio=0;   // KartMove speedRatioCapped
    int hopStickX=0;      // KartMove +0xCC (Kinoko hopStickX): +1 left, -1 right
    int appliedHopStickX=0; // KartMove::getAppliedHopStickX
    float steering=0;     // PlayerModel +0x58 (Kinoko Render::KartModel::steeringInput)
    float stickX=0;       // Enemy::AI::getStick: the kart's input stick
    bool accelButton=false; // the accelerate button itself (input +0x2C & 1)
    bool lookBack=false;    // the look-behind button (input +0x2C & 0x20)
    bool countdown=false;   // RaceInfo_getCountdown() > 0
    bool finished=false;    // RaceManager player +0x38 bit 1
    int finishType=4;       // fn_1_27CBF0 0x8078CFA4: 0 1st, 1 good, 2 bad, otherwise none
    bool far=false;         // PlayerModel +0xB4 (fn_1_2BADE4 0x807CB2D4), see farFromCamera
    // Events of this frame.
    int trickVariant=-1,trickDirection=0; // setTrick 0x807D18EC: SurfaceVariant, System::Trick
    int itemThrow=0;        // fn_1_2C23E4 0x807D2798: +1 thrown forward, -1 backward
    bool gut=false;         // fn_1_2C1614 0x807D19C8: this racer's item hit another racer
};
struct DriverPose {
    const char* clipName="drive";
    float frame=0;
    // Crossfade: fromClip keeps its own frame; weight is clipName's share (1 = none).
    const char* fromClip=nullptr;
    float fromFrame=0,weight=1;
    const char* faceName=nullptr;  // texture pattern (and the eye SRT of the same name)
    float faceFrame=0;
};

// Port of the retail PlayerModel animation logic: per-frame update fn_1_2BB17C
// (0x807CB530), state machine fn_1_2BBDC0 (0x807CC174), kart / bike air and
// trick handlers (0x807CF510 / 0x807CF9F0), steering sweep (0x807CD32C), the
// face controller (0x807C6A44) and the blend engine (crossfade of 1/rate
// frames, both clips keep animating). Clip IDs are the retail clip table's
// (0x808A68E8). Not ported: head look-at, body-reaction springs and limb IK
// (findings 16), the distance LOD flag (+0xB4), the CPU AI's model flags
// (Enemy::AI +0x1C) and online-only state bits.
class DriverController {
public:
    enum Clip : uint8_t {
        Drive,Dash,DriftL,DriftR,Wheelie,Wait2,Back,Wait,First,Good,Bad,
        Gut=20,ThrowF,ThrowFEd,ThrowB,FireF,FireB,Damage,JumpEd,SjumpEd,JumpSt,SjumpSt,
        SjumpSa,SjumpSb,SjumpSc,SjumpMa,SjumpMb,SjumpMc,SjumpLa,SjumpLb,SjumpLc,Jump,
        Count,None=Count
    };
    // Retail clip name (fn_1_2C043C 0x807D07F0: karts take the alternative name).
    static const char* clipName(unsigned clip,bool bike);
    // fn_1_27CBF0 for GP/VS: rank 1 -> 1st, rank <= 0x808D1820[racers-1] -> good, else bad.
    static int finishTypeForRank(unsigned rank,unsigned racers){
        static constexpr unsigned Good[12]={1,1,2,2,3,3,4,4,5,5,5,5};
        if(rank<=1)return 0;
        return rank<=Good[std::clamp(racers,1u,12u)-1]?1:2;
    }
    // PlayerModel +0xB4, set each frame by fn_1_2BADE4 (0x807CB2D4): +0xAC squared is
    // below the racer's render-group distance (fn_1_277964 0x80787D18), which is
    // |kart - eye|^2 x (min(fovy,55)/55)^2 per screen (fn_1_2773C0 0x80787774,
    // fn_1_276CC8 0x8078707C). +0xAC is 4000 for a CPU (fn_1_27CBA8 0x8078CF5C) and
    // 100000 for the local human and ghosts with one local player (PlayerModel_init
    // 0x807C8758). A far racer's state machine shows drive and skips the rest,
    // the finish-pose counter included (0x807CC174).
    static bool farFromCamera(float distanceSquared,float fovy,bool cpu){
        const float radius=cpu?4000.f:100000.f,scale=std::min(fovy,55.f)*(1.f/55.f);
        return radius*radius<scale*scale*distanceSquared;
    }
    // Vehicle and character are retail IDs; insideDrift is the bike's drift type.
    void configure(bool bike,unsigned vehicle,unsigned character,bool insideDrift,uint32_t seed);
    // Back to the model's initial state (PlayerModel init: drive, blinking).
    void reset();
    // Register the loaded bank's clips (CHR) and face patterns (PAT) after configure().
    void setDuration(const char* name,float frames,bool loop=false);
    void setFace(const char* name,float frames,bool loop=false);
    // Every clip and face pattern of a loaded DriverAnimation bank.
    template<class Bank> void registerBank(const Bank& bank){
        for(unsigned c=0;c<bank.count();++c)setDuration(bank.name(c),bank.duration(c),bank.loop(c));
        for(unsigned f=0;f<bank.faceCount();++f)setFace(bank.faceName(f),bank.faceDuration(f),bank.faceLoop(f));
    }
    DriverPose step(const DriverSignals& input);
private:
    struct Track {uint8_t clip=Drive;float frame=0,rate=1;};
    struct Face {int8_t id=-1;float frame=0;};
    bool has(unsigned clip) const {return clip<Count&&present_[clip];}
    float length(unsigned clip) const {return clip<Count?durations_[clip]:1;}
    unsigned anim() const {return cur_.clip;}
    bool done() const {return length(cur_.clip)<=cur_.frame;}
    unsigned random(unsigned n);
    bool setAnimation(float rate,unsigned clip);
    void cut(unsigned clip);
    void change();
    bool canChange(const DriverSignals& s) const;
    void setTrick(int variant,int direction);
    unsigned trickClip() const;
    void stateMachine(const DriverSignals& s);
    void selectState(const DriverSignals& s);
    void face(const DriverSignals& s);
    void faceCategory(unsigned category);
    void setFaceClip(int id);
    void kartAir(const DriverSignals& s);
    void bikeAir(const DriverSignals& s);
    bool landed(const DriverSignals& s) const;
    void steer(const DriverSignals& s);
    void blink();
    void advance(Track& t) const;
    // Configuration and bank contents.
    bool bike_=false,bikeIk_=false,insideDrift_=false,cannonJump_=false;
    uint32_t seed_=1,random_=1;
    std::array<float,Count> durations_{};
    std::array<bool,Count> present_{},loop_{};
    // Face patterns by name index (see faceNames in driver_controller.cpp).
    std::array<float,48> faceDurations_{};
    std::array<bool,48> faceLoop_{},facePresent_{};
    // Animation layer 0: current clip (+0xFA) and the clip it fades from.
    Track cur_,prev_;
    float weight_=1,blendRate_=.1f;
    bool blending_=false;
    // State machine (offsets in the retail PlayerModel).
    uint8_t applied_=Drive,wanted_=Drive;   // +0xF4, +0xF6
    bool badFlag_=false,dashFlag_=false;    // +0x10, +0x4F
    bool driftL_=false,driftR_=false;       // +0x4D, +0x4E
    int finished_=0,poseFrames_=0,stickNeutral_=0,backHold_=0,lookBack_=0; // +0x2D8 +0x2DC +0x2E0 +0x6D8 +0x14
    int eyeRight_=0,eyeLeft_=0;             // +0x18, +0x1C
    float lean_=0,leanStep_=0;              // +0x5C, +0x64
    // Air / trick handler.
    int air_=0,prevAir_=0,ground_=0,prevGround_=0; // +0x2C8 +0x2CC +0x2D0 +0x2D4
    int trickRequest_=0,trickThreshold_=0,cooldown_=0; // +0x2C, +0x30, +0x34
    int trickVariant_=0,trickDirection_=0;  // +0x38, +0x3C
    uint8_t trickStart_=JumpSt,pendingTrick_=None,trickLanding_=JumpEd; // +0x40, +0x42, +0x44
    float trickRate_=1;                     // +0x48
    bool bikeToggle_=false;                 // +0x4C
    // Face controller (+0x2C4): category, blink timer; the pattern on the face layer.
    unsigned faceCategory_=3;int blinkTimer_=10;
    Face face_{};
    int clipFace_=-1;                       // +0x1C8: the clip whose own face is applied
};
} // namespace mkw
