#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace mkw {
// Presentation signals only. This class never changes simulation state.
struct DriverSignals {
    float steering=0; // Same sign as Kinoko stick.x: negative left, positive right.
    float speed=0;    // Signed forward speed, not the magnitude of velocity.
    int driftDirection=0; // Kinoko hopStickX convention: +1 left, -1 right.
    bool drift=false,boost=false,trick=false,hit=false,finished=false;
    bool bike=false,wheelie=false,airborne=false,hopping=false,inCannon=false;
    bool accelerating=false,braking=false,countdown=false;
    int trickVariant=-1; // Optional authored sjump_a/b/c selection, 0..2.
    int finishType=0; // 0 first, 1 good (2nd..5th), 2 bad (6th..12th).
    int itemThrowDirection=0; // Rising edge: +1 forward, -1 backward.
    bool celebrate=false; // Rising edge: successful-hit fist pump (gut).
    bool blink=false;     // Caller supplies cadence; the authored wink lasts 8 frames.
};
struct DriverPose {
    const char* clipName="wait";
    float frame=0;
    const char* faceName=nullptr;
    float faceFrame=0;
};

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
        steering_=0;steeringEnvelope_=1;active_=Wait;clipFrame_=0;
        oneShot_=Count;oneShotFrame_=0;nextTrick_=0;blinkFrame_=8;
        previous_={};pendingTrick_=TrickA;airTrick_=false;jumpAirborne_=false;
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
        // Normal-driving branch of DriverController::vf_1c (PAL 807CD32C),
        // reproduced in Kinoko Render/KartModel.cc. Burnout/cannon variants
        // are intentionally left to the caller's future richer signal path.
        steering_*=0.9f;
        const float previousSteering=steering_;
        if(s.steering<-0.2f)steering_-=0.1f;
        else if(s.steering>0.2f)steering_+=0.1f;
        steeringEnvelope_+=0.1f*(std::fabs(s.steering)-steeringEnvelope_);
        if(previousSteering < -steeringEnvelope_ || steeringEnvelope_ < previousSteering) {
            if(steering_ < -steeringEnvelope_)steering_+=0.1f;
            else if(steering_ > steeringEnvelope_)steering_-=0.1f;
        } else steering_=std::clamp(steering_,-steeringEnvelope_,steeringEnvelope_);
        steering_=std::clamp(steering_,-1.0f,1.0f);

        // Cannon launch uses the takeoff clip; ordinary airborne transitions
        // use the sustained jump pose instead.
        if(s.bike&&s.airborne&&!previous_.airborne){
            jumpAirborne_=!s.hopping;
        }
        if(s.inCannon&&!previous_.inCannon)startOneShot(JumpStart);
        if(s.bike&&!s.airborne&&previous_.airborne&&!s.trick){
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

        Clip wanted=Drive;
        bool parameterized=false;
        if(s.finished) {
            wanted=s.finishType==0?First:s.finishType==1?Good:Bad;
            oneShot_=Count;
        } else if(s.hit) {
            wanted=Damage;oneShot_=Count;
        } else if(oneShot_!=Count)wanted=oneShot_;
        else if(s.bike&&s.airborne&&(jumpAirborne_||airTrick_))wanted=Jump;
        else if(s.bike&&s.wheelie){wanted=Wheelie;parameterized=true;}
        else if(s.speed < -0.05f) {wanted=Back;parameterized=true;}
        else if(s.drift) {wanted=s.driftDirection>0?DriftLeft:DriftRight;parameterized=true;}
        else if(s.boost) {wanted=Dash;parameterized=true;}
        else if(s.countdown||(!s.accelerating&&std::fabs(s.speed)<0.05f))wanted=Wait;
        else {wanted=Drive;parameterized=true;}

        if(wanted!=active_) {active_=wanted;clipFrame_=0;}
        DriverPose pose;pose.clipName=names[wanted];
        // drive/back/dash/drift_l/drift_r are steering sweeps: authored keys
        // at 0 / 15 / 30, with neutral at 15. They are not 31-frame loops.
        // The sign maps the model's anatomical +X-left to input's +right.
        if(parameterized)pose.frame=(durations_[wanted]-1)*0.5f*(1-steering_);
        else if(wanted==oneShot_)pose.frame=oneShotFrame_;
        else pose.frame=std::fmod(clipFrame_,durations_[wanted]);
        if(wanted==First||wanted==Good||wanted==Bad||wanted==Damage) {
            pose.faceName=names[wanted];pose.faceFrame=pose.frame;
        } else if(blinkFrame_<8) {pose.faceName="wink";pose.faceFrame=float(blinkFrame_);}
        else if(steering_<-0.2f)pose.faceName="left";
        else if(steering_>0.2f)pose.faceName="right";
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
    void startOneShot(Clip clip) {oneShot_=clip;oneShotFrame_=0;}
    std::array<float,Count> durations_{{50,31,49,19,31,31,31,31,49,31,50,50,55,20,20,35,30,31,30,30,21,6,20}};
    float steering_=0,steeringEnvelope_=1,clipFrame_=0,oneShotFrame_=0;
    Clip active_=Wait,oneShot_=Count,pendingTrick_=TrickA;
    unsigned nextTrick_=0,blinkFrame_=8;
    bool airTrick_=false,jumpAirborne_=false;
    DriverSignals previous_{};
};
} // namespace mkw
