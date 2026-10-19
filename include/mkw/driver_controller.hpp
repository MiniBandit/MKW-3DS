#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mkw/math.hpp>
#include <mkw/driver_motion_blend.hpp>

namespace mkw {
// Presentation signals only. This class never changes simulation state.
struct DriverSignals {
    float steering=0; // Same sign as Kinoko stick.x: negative left, positive right.
    bool burnout=false,inCannon=false,frozenInIce=false;
    unsigned vehicleId=0; // Retail KartId; cannon pose excludes 26, 29 and 35.
    float burnoutPitch=0;
    float speed=0;    // Signed forward speed, not the magnitude of velocity.
    int driftDirection=0; // Kinoko hopStickX convention: +1 left, -1 right.
    bool drift=false,boost=false,trick=false,hit=false,finished=false;
    bool bike=false,wheelie=false,airborne=false,hopping=false;
    bool handleRelativeBike=false; // Retail axle/body type 1; excludes Quacker.
    bool accelerating=false,braking=false,countdown=false;
    int trickVariant=-1; // Optional authored sjump_a/b/c selection, 0..2.
    int finishType=0; // 0 first, 1 good, 2 bad; cutoff depends on field size.
    int itemThrowDirection=0; // Rising edge: +1 forward, -1 backward.
    bool ceremonyVictory=false; // Authored demo Eflag2, without the race finish delay.
    bool celebrate=false; // Rising edge: successful-hit fist pump (gut).
    bool blink=false;     // Caller cadence, restored before the random wink change.
    bool hasLookTarget=false;
    Vec3 lookTarget{};    // World position requested by the original item actor update.
};
struct DriverPose {
    const char* clipName="wait";
    float frame=0;
    const char* faceName=nullptr;
    float faceFrame=0;
    uint16_t state=7,previousState=7;
    float blendWeight=1;
    bool transitioning=false;
};

// The controller uses common trick names; bike banks carry the original
// medium-trick names. Physics evaluation and rendering must resolve alike.
inline const char* driverVehicleClipName(const char* name,bool bike){
    if(bike){
        if(std::strcmp(name,"sjump_a")==0)return "sjump_ma";
        if(std::strcmp(name,"sjump_b")==0)return "sjump_mb";
        if(std::strcmp(name,"sjump_c")==0)return "sjump_mc";
    }
    return name;
}

// USA 807bda08..807bda84: only bikes, except Wario Bike/Shooting Star/Phantom.
inline bool driverCannonJump(bool bike,unsigned vehicle){
    return bike&&vehicle!=26&&vehicle!=29&&vehicle!=35;
}

// DriverMgr finish classification, USA 807840e4..8078411c,
// threshold table 808cd0b8. The winner always uses the first-place clip.
inline int driverFinishType(unsigned rank,unsigned racers){
    constexpr unsigned good[]={1,1,2,2,3,3,4,4,5,5,5,5};
    return rank<=1?0:rank<=good[std::clamp(racers,1u,12u)-1]?1:2;
}

// Source clip names, CHR0 durations, sweep centers and steering smoothing are
// recovered. The priority policy, reverse/idle epsilon, deterministic trick
// fallback and face selection are native adaptations; the original full
// animation state machine / runtime limb IK have not been recovered.
class DriverController {
public:
    enum Clip : unsigned {
        First,Back,Bad,Damage,Dash,DriftLeft,DriftRight,Drive,Good,Gut,
        TrickA,TrickB,TrickC,ThrowBack,ThrowForward,ThrowForwardEnd,Wait,Wheelie,JumpStart,Jump,JumpEnd,BikeTrickStart,BikeTrickEnd,Count
    };
    inline static constexpr const char* names[Count]={
        "1st","back","bad","damage","dash","drift_l","drift_r","drive",
        "good","gut","sjump_a","sjump_b","sjump_c","throw_b","throw_f",
        "throw_f_ed","wait","wheelie","jump_st","jump","jump_ed","sjump_st","sjump_ed"
    };
    void reset() {
        motionBlend_.reset();posePresent_=false;previousMotion_=Wait;lastBaseState_=7;
        steering_=0;steeringEnvelope_=1;previousBurnoutPitch_=0;active_=Wait;clipFrame_=0;
        oneShot_=Count;oneShotFrame_=0;nextTrick_=0;blinkFrame_=8;
        finishFrames_=0;finishMotionFrames_=0;previous_={};pendingTrick_=TrickA;airTrick_=false;jumpAirborne_=false;
    }
    // Populate this from each character's CHR0 metadata after loading. Defaults
    // below are the original Mario Standard Kart M bank, not universal timings.
    void setDuration(const char* name,float frames) {
        if(!name||!std::isfinite(frames)||frames<1)return;
        for(unsigned i=0;i<Count;++i)if(std::strcmp(names[i],name)==0)durations_[i]=frames;
    }
    DriverPose step(const DriverSignals& input) {
        DriverSignals s=input;
        s.steering=std::isfinite(s.steering)?std::clamp(s.steering,-1.0f,1.0f):0;
        // DriverController::vf_1c (PAL 807CD32C), also recovered in
        // Kinoko Render/KartModel.cc: burnout pitch, cannon response and
        // Sherbet Land ice rescue all alter the normal steering sweep.
        if(s.burnout){
            steeringEnvelope_=1;
            const float pitch=std::isfinite(s.burnoutPitch)?s.burnoutPitch:0;
            const float response=pitch+75.f*(pitch-previousBurnoutPitch_);
            const float amount=std::min(.2f,.04f*std::fabs(response));
            steering_+=response>0?amount:-amount;
            previousBurnoutPitch_=pitch;
        }else{
            previousBurnoutPitch_=0;
            steering_*=.9f;
        }
        const float previousSteering=steering_;
        const float stick=s.frozenInIce?0:s.steering;
        const float inputRate=s.inCannon?.02f:.1f;
        if(stick<-.2f)steering_-=inputRate;
        else if(stick>.2f)steering_+=inputRate;
        const float envelope=s.inCannon?.8f*std::fabs(stick):std::fabs(stick);
        steeringEnvelope_+=(s.inCannon?.05f:.1f)*(envelope-steeringEnvelope_);
        if(previousSteering < -steeringEnvelope_ || steeringEnvelope_ < previousSteering) {
            if(steering_ < -steeringEnvelope_)steering_+=0.1f;
            else if(steering_ > steeringEnvelope_)steering_-=0.1f;
        } else steering_=std::clamp(steering_,-steeringEnvelope_,steeringEnvelope_);
        steering_=std::clamp(steering_,-1.0f,1.0f);

        // A drift hop is already present in the vehicle's physics pose. Playing
        // the full takeoff/landing body clips on every hop adds a second lurch.
        const bool cannonJump=s.inCannon&&driverCannonJump(s.bike,s.vehicleId);
        if(cannonJump){oneShot_=Count;airTrick_=false;jumpAirborne_=true;}
        // Retail 807c1108..807c1188 leaves state 40 through jump_st (29).
        if(!s.inCannon&&previous_.inCannon&&active_==Jump)startOneShot(JumpStart);
        if(!s.inCannon&&s.bike&&s.airborne&&!previous_.airborne){
            jumpAirborne_=!s.hopping;
            if(jumpAirborne_&&!s.trick)startOneShot(JumpStart);
        }
        if(!s.inCannon&&s.bike&&!s.airborne&&previous_.airborne&&!s.trick){
            if(airTrick_)startOneShot(BikeTrickEnd);
            else if(jumpAirborne_&&(oneShot_==Count||oneShot_==JumpStart))startOneShot(JumpEnd);
            airTrick_=false;jumpAirborne_=false;
        }
        if(s.trick&&!previous_.trick) {
            const unsigned variant=s.trickVariant>=0?unsigned(s.trickVariant)%3:nextTrick_++%3;
            pendingTrick_=Clip(TrickA+variant);
            airTrick_=s.bike;
            startOneShot(s.bike?BikeTrickStart:pendingTrick_);
        } else if(s.itemThrowDirection!=0&&previous_.itemThrowDirection==0) {
            startOneShot(s.itemThrowDirection>0?ThrowForward:ThrowBack);
        } else if(s.celebrate&&!previous_.celebrate)startOneShot(Gut);

        if(s.blink&&!previous_.blink)blinkFrame_=0;

        // Retail controller waits until its finish counter exceeds 60
        // (USA 807bdc9c..807bdcd4) before selecting the result motion.
        finishFrames_=s.finished?std::min(finishFrames_+1,61u):0;
        Clip wanted=Drive;
        bool parameterized=false;
        if(cannonJump){
            wanted=Jump;oneShot_=Count;
        } else if((s.finished&&finishFrames_>60)||s.ceremonyVictory) {
            // AI controller 80742320 sets flag 4 when |CPU stick.x| > 0.5
            // (808c6e2c). Driver 807bdcd8 prioritizes that drive request,
            // then the drift flags, then the result motion. A newly started
            // result motion gets 30 ticks before a drift can interrupt it.
            if(std::fabs(s.steering)>.5f){
                wanted=Drive;parameterized=true;finishMotionFrames_=0;
            }else if(s.drift&&(finishMotionFrames_==0||finishMotionFrames_>30)){
                wanted=s.driftDirection>0?DriftLeft:DriftRight;
                parameterized=true;finishMotionFrames_=0;
            }else{
                wanted=s.finishType==0?First:s.finishType==1?Good:Bad;
                finishMotionFrames_=std::min(finishMotionFrames_+1,31u);
            }
            oneShot_=Count;
        } else if(s.hit) {
            wanted=Damage;oneShot_=Count;
        } else if(oneShot_!=Count)wanted=oneShot_;
        else if(s.bike&&s.airborne&&(jumpAirborne_||airTrick_))wanted=JumpStart;
        else if(s.bike&&s.wheelie){wanted=Wheelie;parameterized=true;}
        else if(s.speed < -0.05f) {wanted=Back;parameterized=true;}
        else if(s.drift) {wanted=s.driftDirection>0?DriftLeft:DriftRight;parameterized=true;}
        else if(s.boost) {wanted=Dash;parameterized=true;}
        else if(s.countdown||(!s.accelerating&&std::fabs(s.speed)<0.05f))wanted=Wait;
        else {wanted=Drive;parameterized=true;}

        // 807c3108: base motions wait for the current blend to finish.
        // Event motions retain their existing priority; their source rates
        // below are separate from the base-state selector.
        const bool base=retailState(wanted)<=10;
        // Jump starts (807c1120/12a0) and landings (807c1730/1860)
        // also wait. Keep the pending one-shot at frame zero until accepted.
        const bool queuedJump=wanted==JumpStart||wanted==BikeTrickStart||wanted==JumpEnd||wanted==BikeTrickEnd;
        if(wanted!=active_&&(base||queuedJump)&&motionBlend_.active())wanted=active_;
        if(wanted!=active_){
            const uint16_t state=retailState(wanted);
            float rate;
            if(state<=10){rate=driverBaseBlendRate(lastBaseState_,state,s.handleRelativeBike);lastBaseState_=state;}
            else if(wanted==Damage)rate=.07f; // 807bcd14 / 807c1d60
            else if(wanted==Jump)rate=.05f; // 807bda80
            else if(wanted==JumpStart)rate=active_==Jump?.05f:.1f; // 807c1174 versus 807c12e0
            else if(wanted==TrickA||wanted==TrickB||wanted==TrickC)rate=.15f; // 807c0d24 / 807c1578
            else if(wanted==BikeTrickStart)rate=.1f; // state 30 through 807c12e0
            else rate=.1f; // gut, throw and landing requests
            previousMotion_=active_;active_=wanted;clipFrame_=0;
            motionBlend_.start(rate,posePresent_);
        }
        motionBlend_.advance();posePresent_=true;
        parameterized=wanted==Drive||wanted==Dash||wanted==Back||wanted==DriftLeft||wanted==DriftRight||wanted==Wheelie;
        DriverPose pose;pose.clipName=names[wanted];
        pose.state=retailState(wanted);pose.previousState=retailState(previousMotion_);
        pose.blendWeight=motionBlend_.weight();pose.transitioning=motionBlend_.active();
        // drive/back/dash/drift_l/drift_r are steering sweeps: authored keys
        // at 0 / 15 / 30, with neutral at 15. They are not 31-frame loops.
        // The sign maps the model's anatomical +X-left to input's +right.
        if(parameterized)pose.frame=(durations_[wanted]-1)*0.5f*(1-steering_);
        else if(wanted==oneShot_)pose.frame=oneShotFrame_;
        // Preserve elapsed time for independent CHR0 / PAT0 playback. Their
        // lengths and loop flags can differ (Peach good: body 62, face 100).
        // DriverAnimation samples each bank using its own authored metadata.
        else pose.frame=clipFrame_;
        if(wanted==First||wanted==Good||wanted==Bad||wanted==Damage) {
            pose.faceName=names[wanted];pose.faceFrame=pose.frame;
        } else if(blinkFrame_<8) {pose.faceName="wink";pose.faceFrame=float(blinkFrame_);}
        // Retail FaceController (USA807b7fe4): drive/drift states 3/7
        // stop PAT(type3) and SRT(type2). left/right are special look-back
        // faces, not a response to ordinary steering input.
        ++clipFrame_;
        if(blinkFrame_<8)++blinkFrame_;
        if(wanted==oneShot_ && ++oneShotFrame_>=durations_[oneShot_]) {
            if(oneShot_==BikeTrickStart)startOneShot(pendingTrick_);
            else if(oneShot_==ThrowForward)startOneShot(ThrowForwardEnd);
            else {oneShot_=Count;oneShotFrame_=0;}
        }
        previous_=s;
        return pose;
    }
private:
    static uint16_t retailState(Clip c){
        constexpr uint16_t states[Count]={8,6,10,26,1,2,3,0,9,20,31,32,33,23,21,22,7,4,29,40,27,30,28};
        return states[c];
    }
    DriverMotionBlend motionBlend_;
    bool posePresent_=false;
    Clip previousMotion_=Wait;
    uint16_t lastBaseState_=7;
    void startOneShot(Clip clip) {oneShot_=clip;oneShotFrame_=0;}
    std::array<float,Count> durations_{{50,31,49,19,31,31,31,31,49,31,50,50,55,20,20,35,30,31,30,30,21,6,20}};
    float steering_=0,steeringEnvelope_=1,previousBurnoutPitch_=0,clipFrame_=0,oneShotFrame_=0;
    Clip active_=Wait,oneShot_=Count,pendingTrick_=TrickA;
    unsigned nextTrick_=0,blinkFrame_=8,finishFrames_=0,finishMotionFrames_=0;
    bool airTrick_=false,jumpAirborne_=false;
    DriverSignals previous_{};
};
} // namespace mkw
