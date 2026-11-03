#pragma once
#include <algorithm>
#include <cmath>

namespace mkw {
// Values of retail Input::State::controlMotionFlick, consumed directly by
// KartJump/Bike/POW. This is independent of the HID key/repeat machinery.
enum class MotionTrick {None,Up,Down,Left,Right};

// Wii Remote Core's adaptive re-trigger thresholds, not a fixed debounce.
// RMCU80717fcc updates each threshold,80718548 tests crossings,80718700
// arms both signs. UpdateForCore80736044 loads both recovery counts=15
// from808c6254. First update after a flick skips the recovery decrement.
struct WiiMotionThreshold {
    float base=0,sign=1,current=0,previous=0,peak=0;
    int direction=0,remaining=0;bool justTriggered=false;
    explicit WiiMotionThreshold(float b):base(b),sign(b>0?1.f:-1.f),current(b),previous(b){}
    void step(float value){
        previous=current;peak=std::max(peak,std::abs(value));current=base;
        if(remaining>0&&!justTriggered){
            const float rise=direction>0?1.5f*(peak-1.f):2.4f*(peak-.6f);
            const float limit=(direction>0?2.5f:4.f)*sign;
            current=base+std::max(0.f,rise)*sign;
            current=sign>0?std::clamp(current,0.f,limit):std::clamp(current,limit,0.f);
            --remaining;
        }
        justTriggered=false;
    }
    void trigger(int d){peak=0;direction=d;remaining=15;justTriggered=true;}
};
struct WiiMotionAxis {
    WiiMotionThreshold positive,negative;float previousValue=0;
    explicit WiiMotionAxis(float threshold=.8f):positive(threshold),negative(-threshold){}
    int step(float value){
        positive.step(value);negative.step(value);
        int direction=0;
        if(previousValue>=negative.previous&&value<negative.current)direction=-1;
        else if(previousValue<=positive.previous&&value>positive.current)direction=1;
        previousValue=value;
        if(direction){positive.trigger(direction);negative.trigger(-direction);}
        return direction;
    }
};
class GyroTrick {
public:
    // Wii accelerometer g values and a 3DS angular rate are different sensors.
    // This front-end sensitivity is a 3DS adaptation; the following threshold
    // state/15 simulation-tick recovery comes from retail Core above.
    static constexpr float DegreesPerUnit=75.f;
    static constexpr float PitchDegreesPerUnit=45.f;
    void sample(float pitchDps,float rollDps){
        pitch_=std::isfinite(pitchDps)?-pitchDps/PitchDegreesPerUnit:0;
        roll_=std::isfinite(rollDps)?rollDps/DegreesPerUnit:0;
    }
    void reset(){*this=GyroTrick{};}
    MotionTrick step(){
        if(!primed_){pitchAxis_.previousValue=pitch_;rollAxis_.previousValue=roll_;primed_=true;return MotionTrick::None;}
        const int horizontal=rollAxis_.step(roll_),vertical=pitchAxis_.step(pitch_);
        // Unlike Wii acceleration, the angular-rate ramp crosses each lower
        // threshold on its way up. Accept and arm the vertical gesture at the
        // same threshold; arming at1.2 then rejecting below1.3 lost ordinary
        // wrist flicks. The pitch sensitivity is specific to holding a3DS.
        if(vertical)return vertical<0?MotionTrick::Up:MotionTrick::Down;
        return horizontal<0?MotionTrick::Left:horizontal>0?MotionTrick::Right:MotionTrick::None;
    }
private:
    WiiMotionAxis pitchAxis_{1.3f},rollAxis_{.8f};float pitch_=0,roll_=0;bool primed_=false;
};
}
