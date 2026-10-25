#include <mkw/notification_led.hpp>
#include <mkw/game_settings.hpp>
#include <3ds.h>

namespace mkw {
namespace {
constexpr unsigned FlashFrames=40; // a one-shot flash keeps priority this long (60 Hz frames)

// A flash: the colour, fading out over the pattern, played once.
InfoLedPattern flash(uint8_t r,uint8_t g,uint8_t b,uint8_t delay){
    InfoLedPattern p{};p.delay=delay;p.smoothing=0x40;p.loopDelay=0xFF;
    for(unsigned i=0;i<32;++i){
        const unsigned k=i<4?255:255*(31-i)/27;
        p.redPattern[i]=uint8_t(r*k/255);p.greenPattern[i]=uint8_t(g*k/255);p.bluePattern[i]=uint8_t(b*k/255);
    }
    return p;
}
// A steady colour, looping.
InfoLedPattern steady(uint8_t r,uint8_t g,uint8_t b){
    InfoLedPattern p{};p.delay=8;p.smoothing=0;p.loopDelay=0;
    for(unsigned i=0;i<32;++i){p.redPattern[i]=r;p.greenPattern[i]=g;p.bluePattern[i]=b;}
    return p;
}
// Two colours fading into each other, looping.
InfoLedPattern pulse(const uint8_t (&a)[3],const uint8_t (&b)[3],uint8_t delay){
    InfoLedPattern p{};p.delay=delay;p.smoothing=0x30;p.loopDelay=0;
    for(unsigned i=0;i<32;++i){
        const unsigned k=i<16?i*255/15:(31-i)*255/15;
        p.redPattern[i]=uint8_t((a[0]*(255-k)+b[0]*k)/255);
        p.greenPattern[i]=uint8_t((a[1]*(255-k)+b[1]*k)/255);
        p.bluePattern[i]=uint8_t((a[2]*(255-k)+b[2]*k)/255);
    }
    return p;
}
// Hue around the colour wheel, looping.
InfoLedPattern rainbow(uint8_t delay){
    InfoLedPattern p{};p.delay=delay;p.smoothing=0x20;p.loopDelay=0;
    for(unsigned i=0;i<32;++i){
        const unsigned h=i*6*256/32,sector=h>>8,f=h&255;
        uint8_t r=0,g=0,b=0;
        switch(sector){case 0:r=255;g=f;break;case 1:r=255-f;g=255;break;case 2:g=255;b=f;break;
                       case 3:g=255-f;b=255;break;case 4:r=f;b=255;break;default:r=255;b=255-f;break;}
        p.redPattern[i]=r;p.greenPattern[i]=g;p.bluePattern[i]=b;
    }
    return p;
}
}

NotificationLed& notificationLed(){static NotificationLed led;return led;}

bool NotificationLed::start(){
    if(open_)return true;
    open_=R_SUCCEEDED(mcuHwcInit());
    return open_;
}
void NotificationLed::stop(){
    if(!open_)return;
    clear();mcuHwcExit();open_=false;
}
void NotificationLed::write(Show show){
    const uint8_t brightness=gameSettings().brightness;
    if(!open_||(show==showing_&&brightness==brightness_))return;
    static constexpr uint8_t Red[3]={255,0,0},Grey[3]={70,70,70},White[3]={255,255,255};
    InfoLedPattern p{};
    switch(show){
    case Show::DriftBlue:p=steady(0,60,255);break;
    case Show::DriftOrange:p=steady(255,90,0);break;
    case Show::Countdown:p=steady(255,0,200);break;
    case Show::Bullet:p=pulse(Grey,White,3);break;
    case Show::Mega:p=pulse(Red,Grey,3);break;
    case Show::Star:p=rainbow(1);break;
    case Show::Warning:{static constexpr uint8_t Off[3]={0,0,0},Alert[3]={255,50,0};p=pulse(Off,Alert,1);break;}
    case Show::RainbowLap:p=rainbow(6);break;
    case Show::ReleaseBlue:p=flash(0,255,255,0);break;
    case Show::ReleaseOrange:p=flash(255,110,0,0);break;
    case Show::Boost:p=flash(255,96,0,0);break;
    case Show::Go:p=flash(0,0,255,1);break;
    case Show::FinalLap:p=flash(255,255,255,1);break;
    case Show::Hit:p=flash(255,0,0,1);break;
    case Show::Shocked:p=flash(255,200,0,1);break;
    case Show::None:p.loopDelay=0xFF;break; // all zero: off
    }
    for(unsigned i=0;i<32;++i){
        p.redPattern[i]=uint8_t(p.redPattern[i]*brightness/100);
        p.greenPattern[i]=uint8_t(p.greenPattern[i]*brightness/100);
        p.bluePattern[i]=uint8_t(p.bluePattern[i]*brightness/100);
    }
    MCUHWC_WriteRegister(0x2D,&p,sizeof p);
    showing_=show;brightness_=brightness;
}
void NotificationLed::clear(){
    hold_=0;last_={};
    if(showing_!=Show::None)write(Show::None);
}
void NotificationLed::update(const LedFrame& f){
    if(!open_)return;
    const LedMode mode=gameSettings().led;
    if(mode==LedMode::Off||!(f.racing||f.countdown)){clear();return;}
    const bool events=mode>=LedMode::ItemsEvents,drifts=mode>=LedMode::ItemsEventsDrifts;
    // New events, highest first.
    Show next=Show::None;
    if(f.shocked&&!last_.shocked)next=Show::Shocked;
    else if(events&&f.hit&&!last_.hit)next=Show::Hit;
    else if(events&&f.finalLap&&!last_.finalLap&&!f.rainbowRoad)next=Show::FinalLap;
    else if(events&&last_.countdown&&!f.countdown)next=Show::Go;
    else if(drifts&&last_.drift&&!f.drift&&f.boost)next=last_.drift>1?Show::ReleaseOrange:Show::ReleaseBlue;
    else if(f.boost&&!last_.boost&&!f.star)next=Show::Boost;
    last_=f;
    // A warning outranks the item flashes (not hits or lightning).
    const bool warning=f.warning&&!(events&&f.finalLap&&f.rainbowRoad);
    if(warning&&next!=Show::Hit&&next!=Show::Shocked){hold_=0;write(Show::Warning);return;}
    if(next!=Show::None&&(hold_==0||next>=showing_)){hold_=FlashFrames;write(next);return;}
    if(hold_){--hold_;return;}
    // Otherwise what lasts, highest first.
    Show held=Show::None;
    if(events&&f.finalLap&&f.rainbowRoad)held=Show::RainbowLap;
    else if(f.star)held=Show::Star;
    else if(f.mega)held=Show::Mega;
    else if(f.bullet)held=Show::Bullet;
    else if(events&&f.countdown)held=Show::Countdown;
    else if(drifts&&f.drift)held=f.drift>1?Show::DriftOrange:Show::DriftBlue;
    write(held);
}
}
