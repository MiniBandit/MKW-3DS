#include <mkw/driver_controller.hpp>
#include <mkw/trick_presentation.hpp>
#include <cmath>
#include <cstring>

namespace mkw {
namespace {
using DC=DriverController;
// Clip table 0x808A68E8 (0x18 bytes each): name, kart name (+0x4), face category
// (+0x10). Entries 11..19 are the face patterns / eye SRTs; the per-clip face
// lookup (fn_1_2C1E44 0x807D21F8) skips them and the flagged (+0x8) bad / damage.
struct ClipInfo {const char* name;const char* kart;uint8_t category;};
constexpr ClipInfo Clips[DC::Count]={
    {"drive",nullptr,3},{"dash",nullptr,7},{"drift_l",nullptr,7},{"drift_r",nullptr,7},
    {"wheelie",nullptr,7},{"wait2","wait",3},{"back",nullptr,8},{"wait",nullptr,3},
    {"1st",nullptr,1},{"good",nullptr,2},{"bad",nullptr,4},
    {"wink",nullptr,3},{"damage",nullptr,0},{"bad",nullptr,4},{"bad2",nullptr,5},{"drift",nullptr,7},
    {"right",nullptr,8},{"left",nullptr,9},{"right",nullptr,8},{"left",nullptr,9},
    {"gut",nullptr,2},{"throw_f",nullptr,10},{"throw_f_ed",nullptr,10},{"throw_b",nullptr,11},
    {"fire_f",nullptr,3},{"fire_b",nullptr,3},{"damage",nullptr,0},{"jump_ed",nullptr,3},
    {"sjump_ed",nullptr,3},{"jump_st",nullptr,3},{"sjump_st",nullptr,3},
    {"sjump_sa",nullptr,3},{"sjump_sb",nullptr,3},{"sjump_sc",nullptr,3},
    {"sjump_ma",nullptr,3},{"sjump_mb",nullptr,3},{"sjump_mc",nullptr,3},
    {"sjump_la","sjump_a",3},{"sjump_lb","sjump_b",3},{"sjump_lc","sjump_c",3},{"jump",nullptr,6},
};
// Face pattern indices: 0..40 the clip table's names, 41.. the karts' alternatives.
constexpr unsigned KartAlt[4]={DC::Wait2,DC::SjumpLa,DC::SjumpLb,DC::SjumpLc};
constexpr int FaceWink=11,FaceDamage=12,FaceBad=13,FaceBad2=14,FaceRight=16,FaceLeft=17;
const char* faceName(int id){
    if(id<0)return nullptr;
    return id<DC::Count?Clips[id].name:Clips[KartAlt[id-DC::Count]].kart;
}
// The blink interval table 0x808A6CC0.
constexpr int BlinkFrames[11]={10,10,30,50,80,90,100,130,170,190,270};
// setTrick 0x807D18EC: playback rate and airtime gate by SurfaceVariant.
constexpr float KartTrickRate[3]={.70f,.70f,.75f},BikeTrickRate[3]={.75f,.70f,.65f}; // 0x808A6D10 / 0x808A6D04
bool busy(unsigned c){return (c>=DC::DriftL&&c<=DC::Wheelie)||(c>=DC::Gut&&c<=DC::Jump&&c!=DC::ThrowFEd&&c!=DC::JumpSt);}
bool resetsFinishCounter(unsigned c){return c<DC::First||(c>=DC::Gut&&c<=DC::Jump);}
}

const char* DriverController::clipName(unsigned clip,bool bike){
    if(clip>=Count)return nullptr;
    return !bike&&Clips[clip].kart?Clips[clip].kart:Clips[clip].name;
}
void DriverController::configure(bool bike,unsigned vehicle,unsigned character,bool insideDrift,uint32_t seed){
    bike_=bike;insideDrift_=bike&&insideDrift;
    // Cannon clip: bikes except Wario Bike, Shooting Star and Phantom (0x807CC174).
    cannonJump_=bike&&vehicle!=0x1a&&vehicle!=0x1d&&vehicle!=0x23;
    // The wait rule's hand-target flag (+0x144) is set for every vehicle the port offers
    // (kartDriverDispParam); King Boo (0x13) is excluded by the rule itself.
    bikeIk_=bike&&character!=0x13;
    seed_=seed?seed:1;
    present_.fill(false);loop_.fill(false);durations_.fill(1);
    facePresent_.fill(false);faceLoop_.fill(false);faceDurations_.fill(1);
    reset();
}
void DriverController::setDuration(const char* name,float frames,bool loop){
    if(!name||!std::isfinite(frames)||frames<1)return;
    for(unsigned i=0;i<Count;++i)if(!(i>=11&&i<=19)&&!std::strcmp(clipName(i,bike_),name)){present_[i]=true;durations_[i]=frames;loop_[i]=loop;}
}
void DriverController::setFace(const char* name,float frames,bool loop){
    if(!name||!std::isfinite(frames)||frames<1)return;
    for(unsigned i=0;i<Count+4;++i)if(!std::strcmp(faceName(int(i)),name)){facePresent_[i]=true;faceDurations_[i]=frames;faceLoop_[i]=loop;}
}
void DriverController::reset(){
    // PlayerModel ctor 0x807C7364 and init (fn_1_2B7474 hard-sets drive, 0x807C8758 the wink).
    cur_=prev_={};weight_=1;blendRate_=.1f;blending_=false;
    applied_=wanted_=Drive;badFlag_=dashFlag_=driftL_=driftR_=false;
    finished_=poseFrames_=stickNeutral_=backHold_=lookBack_=eyeRight_=eyeLeft_=0;
    lean_=leanStep_=0;
    air_=prevAir_=ground_=prevGround_=0;trickRequest_=trickThreshold_=cooldown_=0;
    trickVariant_=trickDirection_=0;trickStart_=JumpSt;pendingTrick_=None;trickLanding_=JumpEd;trickRate_=1;bikeToggle_=false;
    faceCategory_=3;blinkTimer_=10;face_={};clipFace_=-1;random_=seed_;
    setFaceClip(FaceWink);
}
unsigned DriverController::random(unsigned n){
    // Retail draws from RaceManager's Random; a private generator keeps the race's own untouched.
    random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;
    return n?random_%n:0;
}
void DriverController::setFaceClip(int id){
    if(id>=0&&!facePresent_[unsigned(id)])return; // missing patterns leave the layer as it is
    face_={int8_t(id),0};
}

// PlayerModel_setAnimation 0x807CC018: a crossfade of 1/rate frames.
bool DriverController::setAnimation(float rate,unsigned clip){
    if(!has(clip))return false;
    if(resetsFinishCounter(clip))finished_=0;
    blendRate_=rate;
    // fn_1_472D0: the clip already playing continues unchanged.
    if(cur_.clip!=clip){prev_=cur_;cur_={uint8_t(clip),0,1};weight_=0;blending_=true;}
    return true;
}
// fn_1_47018 with the callers' bookkeeping (throws, landings): a hard cut.
void DriverController::cut(unsigned clip){
    if(!has(clip))return;
    if(resetsFinishCounter(clip))finished_=0;
    prev_=cur_;cur_={uint8_t(clip),0,1};weight_=1;blending_=false;clipFace_=-1;
}
// fn_1_2C16AC 0x807D1A60: apply the wanted state with its blend rate.
void DriverController::change(){
    float rate=.1f;
    if(wanted_>=First&&wanted_<=Bad)rate=.05f;
    else if(!bike_&&(wanted_==DriftL||wanted_==DriftR))rate=.05f;
    else if(wanted_==Dash)rate=bike_?.1f:.07f;
    else if(applied_==Dash&&(wanted_==Drive||wanted_==Wait2))rate=.05f;
    if(wanted_==Back||wanted_==Wait)rate=.07f;
    // A running crossfade is never interrupted by a state change.
    if(blending_)return;
    if(setAnimation(rate,wanted_))applied_=wanted_;
}
// vtable +0x20: fn_1_2C1950 0x807D1D04 (kart), fn_1_2C1A80 0x807D1E34 (bike).
bool DriverController::canChange(const DriverSignals& s) const{
    const unsigned a=anim();
    if(a>=JumpEd&&a<=Jump)return false;
    if(a>=Gut&&a<=FireB&&a!=ThrowFEd&&!done())return false;
    if(s.inAction||s.beforeRespawn)return false;
    return !bike_||trickRequest_<1;
}
// vtable +0x1C, fn_1_2C1538 0x807D18EC (PlayerTrick_start, KartHalfPipe::activateTrick).
void DriverController::setTrick(int variant,int direction){
    const unsigned v=unsigned(std::clamp(variant,0,2));
    trickRequest_=30;trickVariant_=int(v);trickDirection_=direction;
    trickRate_=bike_?BikeTrickRate[v]:KartTrickRate[v];
    trickThreshold_=TrickPresentation::threshold(bike_,v);
}
// vtable +0x24: fn_1_2BF56C 0x807CF920 (kart), fn_1_2BF5AC 0x807CF960 (bike).
unsigned DriverController::trickClip() const{
    if(!bike_){
        if(trickDirection_==3||trickDirection_==4)return SjumpLb;
        return trickDirection_==2?SjumpLc:SjumpLa;
    }
    const bool second=trickVariant_==0||bikeToggle_;
    switch(trickDirection_){
    case 1:return second?SjumpSa:SjumpMa;
    case 3:return second?SjumpSb:SjumpMb;
    case 4:return SjumpMb;
    case 2:return second?SjumpSc:SjumpMc;
    default:return SjumpSa;
    }
}

DriverPose DriverController::step(const DriverSignals& input){
    DriverSignals s=input;
    if(!std::isfinite(s.steering))s.steering=0;
    s.steering=std::clamp(s.steering,-1.f,1.f);
    if(!std::isfinite(s.stickX))s.stickX=0;
    if(!std::isfinite(s.speed))s.speed=0;
    // The trick start reaches the model during the kart update, before its own update.
    if(s.trickVariant>=0)setTrick(s.trickVariant,s.trickDirection);
    // fn_1_2BB17C 0x807CB530. fn_1_2BADE4 0x807CB198: airtime history and drift lean flags.
    prevAir_=air_;prevGround_=ground_;
    driftL_=driftR_=false;
    const bool drifting=s.drifting||(!bike_&&s.appliedHopStickX!=0);
    if(drifting||(s.chargingSsmt&&(s.stickLeft||s.stickRight))){
        if(s.hopStickX==1)driftL_=true;
        else if(s.hopStickX==-1)driftR_=true;
        else if(!s.stickLeft)driftR_=true;
        else driftL_=true;
    }
    // A finished gut / throw / fire clip: throw_b holds back 45 frames, throw_f ends
    // with throw_f_ed unless drifting, everything else returns to the wanted state.
    if(anim()>=Gut&&anim()<=FireB&&done()){
        if(anim()==ThrowB)backHold_=45;
        if(anim()==ThrowF&&!driftL_&&!driftR_&&has(ThrowFEd))cut(ThrowFEd);
        else applied_=None;
    }
    // Damage 0x807CB70C: actions, a pit fall, the first 44 frames of Blooper ink
    // (not started while far, +0xB4).
    const bool inked=s.inkFrames>=1&&s.inkFrames<=44;
    if(!s.inAction&&!s.beforeRespawn&&!inked){if(anim()==Damage)applied_=None;}
    else if(anim()!=Damage&&!s.far)setAnimation(.07f,Damage);
    stateMachine(s);
    if(bike_)bikeAir(s);else kartAir(s);
    steer(s);
    blink();
    // Item events come from the item update, after the karts and their models.
    if(s.itemThrow!=0){
        // fn_1_2C23E4 0x807D2798: a hard cut, also over finish poses and tricks.
        const unsigned clip=s.itemThrow>0?ThrowF:ThrowB;
        cut(clip);
    }
    if(s.gut){
        // fn_1_2C1614 0x807D19C8: not over a finish pose, throw or jump/trick clip.
        const unsigned a=anim();
        if(!((a>=First&&a<=Bad)||(a>Gut&&a<=Jump&&a!=ThrowFEd)))setAnimation(.1f,Gut);
    }
    DriverPose pose;
    pose.clipName=clipName(cur_.clip,bike_);pose.frame=cur_.frame;
    if(blending_&&weight_<1){pose.fromClip=clipName(prev_.clip,bike_);pose.fromFrame=prev_.frame;pose.weight=weight_;}
    pose.faceName=faceName(face_.id);pose.faceFrame=face_.frame;
    advance(cur_);
    if(blending_)advance(prev_);
    if(face_.id>=0){
        face_.frame+=1;
        const float d=faceDurations_[unsigned(face_.id)];
        if(faceLoop_[unsigned(face_.id)])face_.frame=std::fmod(face_.frame,d);else face_.frame=std::min(face_.frame,d);
    }
    return pose;
}
void DriverController::advance(Track& t) const{
    const float d=length(t.clip);
    t.frame+=t.rate;
    if(loop_[t.clip<Count?t.clip:0])t.frame=std::fmod(t.frame,d);else t.frame=std::min(t.frame,d);
}

// fn_1_2BBDC0 0x807CC174.
void DriverController::stateMachine(const DriverSignals& s){
    selectState(s);
    // The wait clip stops while frozen in N64 Sherbet Land's respawn ice.
    if(anim()==Wait)cur_.rate=s.frozen?0:1;
    face(s);
    if(applied_!=wanted_&&canChange(s))change();
}
void DriverController::selectState(const DriverSignals& s){
    const bool boosting=s.boost||s.rampBoost||s.slipstreamBoost;
    // Karts (and the three bikes without a cannon clip) show dash in the cannon.
    const bool cannonDash=s.inCannon&&!cannonJump_;
    const bool dash=(boosting||cannonDash)&&s.speedRatio>.3f;
    if(!dash||s.mtBoost)dashFlag_=false;
    if(anim()==ThrowB||anim()==Back){if(backHold_>0)--backHold_;}
    else backHold_=0;
    if(!s.stickLeft&&!s.stickRight)++stickNeutral_;else stickNeutral_=0;
    if(s.countdown&&s.stickX*s.stickX>=.04f)stickNeutral_=0;
    // Far from the camera (+0xB4): drive, and nothing else runs, so the finish-pose
    // counter +0x2D8 does not count (0x807CC174, before the cannon check).
    if(s.far){wanted_=Drive;return;}
    if(s.inCannon&&cannonJump_){setAnimation(.05f,Jump);return;}
    if(s.wheelie){wanted_=Wheelie;return;}
    auto backing=[&]{return (s.speed< -6.f&&s.brake&&!s.accelerate)||backHold_>=1;};
    lookBack_=s.lookBack?lookBack_+1:0;
    if(backing()&&!boosting&&!(anim()>=Dash&&anim()<=Wheelie)&&s.appliedHopStickX==0){wanted_=Back;return;}
    // The finish pose waits for 60 settled frames (setAnimation resets the count).
    bool pose=false;
    if(s.finished)pose=++finished_>60;
    if(!pose){
        if(!s.finished)badFlag_=false;
        bool moving=std::fabs(s.speed)>=6.f||s.accelerate||s.wheelie;
        if(!moving){
            const bool back=backing();
            lookBack_=s.lookBack?lookBack_+1:0; // retail counts the button again on this path
            moving=(back&&!boosting)||((stickNeutral_<21||s.stickLeft||s.stickRight)&&(!bikeIk_||std::fabs(s.speed)>=1.f))||s.accelButton;
        }
        if(!moving)wanted_=Wait;
        else if(driftL_)wanted_=DriftL;
        else if(driftR_)wanted_=DriftR;
        else wanted_=(leanStep_<=std::fabs(lean_)||stickNeutral_<21)?Drive:Wait2;
    }else{
        const bool early=poseFrames_>=1&&poseFrames_<=30;
        if(driftL_&&!early){wanted_=DriftL;poseFrames_=0;}
        else if(driftR_&&!early){wanted_=DriftR;poseFrames_=0;}
        else{
            ++poseFrames_;
            switch(s.finishType){
            case 0:wanted_=First;badFlag_=false;break;
            case 1:wanted_=Good;badFlag_=false;break;
            case 2:wanted_=Bad;badFlag_=true;break;
            default:wanted_=Drive;poseFrames_=0;break;
            }
        }
    }
    if(!dash)return;
    const bool landingDone=(anim()==JumpEd||anim()==SjumpEd)&&done();
    if(!dashFlag_){
        const bool blocked=busy(wanted_)||(busy(anim())&&!landingDone&&!s.mtBoost);
        if(blocked&&!cannonDash)dashFlag_=true;
        else wanted_=Dash;
    }else if(landingDone){dashFlag_=false;wanted_=Dash;}
}
// Face selection at the end of the state machine (0x807CC174, fn_1_2B6690 0x807C6A44).
void DriverController::face(const DriverSignals& s){
    const unsigned a=anim();
    unsigned category=Clips[a].category;
    if((badFlag_||s.burnout)&&category!=0&&category!=4)category=5;
    // The clip's own face pattern (fn_1_2C1E44: same name, not bad / damage).
    int own=-1;
    if(a!=Bad&&a!=Damage){
        own=int(a);
        if(!bike_)for(unsigned i=0;i<4;++i)if(KartAlt[i]==a)own=int(Count+i);
        if(!facePresent_[unsigned(own)])own=-1;
    }
    if(own>=0&&!badFlag_){
        if(clipFace_!=own){clipFace_=own;setFaceClip(own);faceCategory_=12;}
        return;
    }
    if(category==3||category==7){
        bool cancelLookBack=false;
        if(s.chargingSsmt&&(s.stickLeft||s.stickRight)){
            if(s.steering>.5f){category=8;cancelLookBack=true;}
            else if(s.steering< -.5f){category=9;cancelLookBack=true;}
        }else if(category==7){
            // Pushing into a drift: the eyes follow for 30 frames, then 120 frames' rest.
            if(s.hopStickX==-1){
                if(s.steering>.7f&&s.stickX>.9f){if(eyeRight_==0)eyeRight_=30;else if(eyeRight_==1)eyeRight_=2;}
                if(eyeRight_<0)++eyeRight_;
            }
            if(eyeRight_>0){category=8;if(--eyeRight_==0)eyeRight_=-120;}
            if(s.hopStickX==1){
                if(s.steering< -.7f&&s.stickX< -.9f){if(eyeLeft_==0)eyeLeft_=30;else if(eyeLeft_==1)eyeLeft_=2;}
                if(eyeLeft_<0)++eyeLeft_;
            }
            if(eyeLeft_>0){category=9;if(--eyeLeft_==0)eyeLeft_=-120;}
        }else if(lookBack_>=1&&lookBack_<=59)category=8;
        else if(lookBack_==60){setFaceClip(FaceWink);blinkTimer_=0;faceCategory_=3;}
        if(cancelLookBack)lookBack_=999;
    }
    faceCategory(category);
    clipFace_=-1;
}
void DriverController::faceCategory(unsigned category){
    if(category==faceCategory_)return;
    switch(category){
    case 0:setFaceClip(FaceDamage);break;
    case 1:case 2:case 3:case 6:case 7:case 10:
        face_={};blinkTimer_=BlinkFrames[random(11)]+1;break;
    case 5:
        if(facePresent_[FaceBad2]){setFaceClip(FaceBad2);break;}
        setFaceClip(FaceBad);break;
    case 4:setFaceClip(FaceBad);break;
    case 8:case 11:setFaceClip(FaceRight);break;
    case 9:setFaceClip(FaceLeft);break;
    default:break;
    }
    faceCategory_=category;
}
// The blink at the end of fn_1_2BB17C: only in the normal and drift categories.
void DriverController::blink(){
    if(faceCategory_!=7&&faceCategory_!=3)return;
    if(blinkTimer_==0){
        if(face_.id>=0&&faceDurations_[unsigned(face_.id)]<=face_.frame)blinkTimer_=BlinkFrames[random(11)];
    }else if(--blinkTimer_==0)setFaceClip(FaceWink);
}

bool DriverController::landed(const DriverSignals& s) const{
    if(!s.anyWheel)return s.allWheels;
    if(prevAir_>=11)return true;
    if(prevGround_>10&&anim()>=JumpEd&&anim()<=Jump)return true;
    return s.allWheels;
}
// fn_1_2BF15C 0x807CF510.
void DriverController::kartAir(const DriverSignals& s){
    if(!has(Drive))return;
    if(!s.touchingGround)++air_;
    else{if(air_>60)cooldown_=40;air_=0;}
    --cooldown_;
    if(--trickRequest_>0){trickRequest_=0;trickStart_=None;pendingTrick_=uint8_t(trickClip());trickLanding_=None;}
    bool fire=false;
    if(pendingTrick_!=None){
        if(!s.inATrick&&!s.overZipper)trickThreshold_=TrickPresentation::Never;
        fire=trickThreshold_<prevAir_;
    }
    if(pendingTrick_!=None&&!has(pendingTrick_)){if(fire)pendingTrick_=None;return;}
    if(fire){setAnimation(.15f,pendingTrick_);pendingTrick_=None;}
    if(anim()>SjumpSt&&anim()<Jump)cur_.rate=trickRate_;
    ground_=s.anyWheel?ground_+1:0;
    if(!landed(s))return;
    pendingTrick_=None;
    if(anim()>SjumpSt&&anim()<=Jump){
        if(s.boost||s.rampBoost||s.slipstreamBoost){dashFlag_=false;wanted_=Dash;}
        change();
    }
}
// fn_1_2BF63C 0x807CF9F0.
void DriverController::bikeAir(const DriverSignals& s){
    if(!has(Drive))return;
    if(!s.touchingGround)++air_;
    else{if(air_>60)cooldown_=40;air_=0;}
    --cooldown_;--trickRequest_;
    const unsigned a=anim();
    if(a==Jump&&!s.inCannon){
        // Leaving the cannon: the take-off clip, held until landing.
        trickStart_=JumpSt;pendingTrick_=None;trickLanding_=JumpEd;
        if(!blending_&&setAnimation(.05f,JumpSt))trickRequest_=0;
    }else if(pendingTrick_==None&&(a<=Back||(a>=First&&a<=Bad)||a==JumpEd||a==SjumpEd)){
        bool play=false;
        if(trickRequest_<1){
            // Airtime over 3 frames (20 in a hop or wheelie), not within 40 frames of a long flight.
            const int gate=s.hop||a==Wheelie?20:3;
            if(gate<air_&&cooldown_<0&&!s.inCannon){trickStart_=JumpSt;play=true;pendingTrick_=None;trickLanding_=JumpEd;}
        }else{trickStart_=SjumpSt;pendingTrick_=uint8_t(trickClip());play=true;trickLanding_=SjumpEd;}
        if(play&&!blending_&&setAnimation(.1f,trickStart_))trickRequest_=0;
    }
    bool fire=false;
    if(pendingTrick_!=None){
        if(!s.inATrick&&!s.overZipper)trickThreshold_=TrickPresentation::Never;
        fire=trickThreshold_<prevAir_;
    }
    if(!has(trickStart_))return;
    if(pendingTrick_!=None&&!has(pendingTrick_)){if(fire)pendingTrick_=None;return;}
    if(!has(trickLanding_))return;
    if(pendingTrick_==None&&trickRequest_>0){trickRequest_=0;pendingTrick_=uint8_t(trickClip());}
    if(fire){
        if(!done())setAnimation(.15f,pendingTrick_);
        else cut(pendingTrick_);
        pendingTrick_=None;bikeToggle_=!bikeToggle_;
    }
    if(anim()>SjumpSt&&anim()<Jump)cur_.rate=trickRate_;
    ground_=s.anyWheel?ground_+1:0;
    if(landed(s)){
        pendingTrick_=None;
        const unsigned b=anim();
        if(b>SjumpSt&&b<=Jump){
            if(!done()){if(!blending_)setAnimation(.1f,trickLanding_);}
            else if(!blending_)cut(trickLanding_);
        }else if(b==trickStart_){
            if(!done())change();
            else if(!blending_)cut(trickLanding_);
        }
    }
    if((anim()==JumpEd||anim()==SjumpEd)&&done())change();
}
// fn_1_2BCF78 0x807CD32C after the steering value (+0x58, from Kinoko's copy of this
// function): the sweep clips' frame, the body lean the wait2 rule reads, the blend.
void DriverController::steer(const DriverSignals& s){
    const unsigned a=anim();
    if(a<Wait2||a==Back){cur_.frame=15.f*(1.f-s.steering);cur_.rate=0;}
    const bool drifting=s.drifting||(!bike_&&s.appliedHopStickX!=0);
    const float range=!bike_?(drifting?30.f:20.f):drifting?(insideDrift_?5.f:20.f):15.f;
    leanStep_=.1f*range;
    float target=0;
    if(!bike_||a<=Bad||(a>=ThrowF&&a<=ThrowB)){
        target=-s.steering*range;
        const float offset=!bike_||insideDrift_?5.f:10.f;
        if(driftL_)target+=offset;else if(driftR_)target-=offset;
    }
    lean_=target<=lean_?std::max(lean_-leanStep_,target):std::min(lean_+leanStep_,target);
    // fn_1_4EED8 -> fn_1_49114 0x805594C8: the new clip's weight grows by the rate.
    if(blending_){weight_=std::min(1.f,weight_+blendRate_);if(weight_>=1)blending_=false;}
}
} // namespace mkw
