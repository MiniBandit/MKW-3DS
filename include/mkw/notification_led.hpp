#pragma once
#include <cstdint>

namespace mkw {
// The 3DS notification LED during races: a short colour flash for hits,
// lightning and boosts, a colour cycle while a Star lasts. The LED is driven by
// the MCU (mcu::HWC register 0x2D, a 32-step pattern), so a pattern is written
// only when what it shows changes. Off/on is saved in sdmc:/3ds/mkw-3ds/led.txt.
struct LedFrame {
    bool racing=false;   // the player's race is running (not the intro, results or pause)
    bool hit=false;      // spun out, crushed or blown up
    bool shocked=false;  // struck by lightning (shrunk)
    bool star=false;     // Star invincibility
    bool boost=false;    // mushroom, boost panel or mini-turbo
};

class NotificationLed {
public:
    // Opens mcu::HWC; without it (emulator, older exheader) every call does nothing.
    bool start();
    void stop();
    bool enabled() const { return enabled_; }
    void setEnabled(bool on);
    // Once per presented frame.
    void update(const LedFrame& frame);
    // The LED off (race end, pause, leaving the race).
    void clear();
private:
    enum class Show : uint8_t { None, Boost, Star, Shocked, Hit };
    void write(Show show);
    bool open_=false,enabled_=false;
    Show showing_=Show::None;
    LedFrame last_{};
    unsigned hold_=0; // frames the current one-shot flash keeps priority
};
NotificationLed& notificationLed();
}
