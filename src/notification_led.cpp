#include <mkw/notification_led.hpp>
#include <3ds.h>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace {
constexpr const char* SettingPath="sdmc:/3ds/mkw-3ds/led.txt";
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
// The Star: hue around the colour wheel, looping.
InfoLedPattern rainbow(){
    InfoLedPattern p{};p.delay=1;p.smoothing=0x20;p.loopDelay=0;
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
    // On unless the player switched it off (led.txt holds 0).
    enabled_=true;
    if(FILE* f=std::fopen(SettingPath,"r")){enabled_=std::fgetc(f)!='0';std::fclose(f);}
    return open_;
}
void NotificationLed::stop(){
    if(!open_)return;
    clear();mcuHwcExit();open_=false;
}
void NotificationLed::setEnabled(bool on){
    if(on==enabled_)return;
    enabled_=on;
    if(!on)clear();
    if(FILE* f=std::fopen(SettingPath,"w")){std::fputc(on?'1':'0',f);std::fclose(f);}
}
void NotificationLed::write(Show show){
    if(!open_||show==showing_)return;
    InfoLedPattern p{};
    switch(show){
    case Show::Hit:p=flash(255,0,0,1);break;
    case Show::Shocked:p=flash(255,200,0,1);break;
    case Show::Boost:p=flash(255,96,0,0);break;
    case Show::Star:p=rainbow();break;
    case Show::None:p.loopDelay=0xFF;break; // all zero: off
    }
    MCUHWC_WriteRegister(0x2D,&p,sizeof p);
    showing_=show;
}
void NotificationLed::clear(){
    hold_=0;last_={};
    if(showing_!=Show::None)write(Show::None);
}
void NotificationLed::update(const LedFrame& f){
    if(!open_||!enabled_)return;
    if(!f.racing){clear();return;}
    // New events, highest first: a hit, lightning, then a boost; the Star cycles
    // while it lasts unless a flash is showing over it.
    Show next=Show::None;
    if(f.hit&&!last_.hit)next=Show::Hit;
    else if(f.shocked&&!last_.shocked)next=Show::Shocked;
    else if(f.boost&&!last_.boost&&!f.star)next=Show::Boost;
    last_=f;
    if(next!=Show::None&&(hold_==0||next>=showing_)){hold_=FlashFrames;showing_=Show::None;write(next);return;}
    if(hold_){--hold_;return;}
    write(f.star?Show::Star:Show::None);
}
}
