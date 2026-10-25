#pragma once
#include <cstdint>

namespace mkw {
// The 3DS notification LED during races. The LED is driven by the MCU
// (mcu::HWC register 0x2D, a 32-step pattern), so a pattern is written only when
// what it shows changes. What it reacts to follows the license's LED mode
// (game_settings.hpp), scaled by its brightness:
//  - Items: boosts (orange), lightning (yellow), the Star (rainbow), the Mega
//    Mushroom (red and grey) and the Bullet Bill (grey and white);
//  - Events as well: hits (red), the countdown (magenta) and GO (blue), the
//    final lap (a white flash; on Rainbow Road a rainbow for the whole lap);
//  - Drifts as well: blue or orange while a mini-turbo is charged, a cyan or
//    orange flash when it is released. Drifts have the lowest priority.
struct LedFrame {
    bool racing=false;    // the player's race is running (not the intro, results or pause)
    bool countdown=false; // the start countdown
    bool hit=false;       // spun out, crushed or blown up
    bool shocked=false;   // struck by lightning (shrunk)
    bool star=false;      // Star invincibility
    bool mega=false;      // Mega Mushroom
    bool bullet=false;    // Bullet Bill
    bool boost=false;     // mushroom, boost panel or mini-turbo
    bool finalLap=false;
    bool rainbowRoad=false;
    uint8_t drift=0;      // charged mini-turbo: 0 none, 1 blue, 2 orange
};

class NotificationLed {
public:
    // Opens mcu::HWC; without it (emulator, older exheader) every call does nothing.
    bool start();
    void stop();
    // Once per presented frame.
    void update(const LedFrame& frame);
    // The LED off (race end, pause, leaving the race).
    void clear();
private:
    // Sustained colours first, then one-shot flashes; later ones win.
    enum class Show : uint8_t { None, DriftBlue, DriftOrange, Countdown, Bullet, Mega, Star, RainbowLap,
                                ReleaseBlue, ReleaseOrange, Boost, Go, FinalLap, Hit, Shocked };
    void write(Show show);
    bool open_=false;
    Show showing_=Show::None;
    uint8_t brightness_=0; // the brightness the shown pattern was written at
    LedFrame last_{};
    unsigned hold_=0; // frames the current one-shot flash keeps priority
};
NotificationLed& notificationLed();
}
