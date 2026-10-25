#pragma once
#include <cstdint>

namespace mkw {
// The options each license keeps: the notification LED's mode and brightness
// the player's dynamic shadow and the speedometer. Saved as text lines (led=, bright=, shadows=, speedometer=)
// in sdmc:/3ds/mkw-3ds/settings<license>.txt; changed on the pause menu's lower screen.
enum class LedMode : uint8_t { Off, Items, ItemsEvents, ItemsEventsDrifts };
struct GameSettings {
    LedMode led=LedMode::ItemsEvents;
    uint8_t brightness=100; // percent: 50, 60 ... 100
    bool shadows=true;
    bool speedometer=true; // SELECT during a race
};
GameSettings& gameSettings();
// License n's settings (the defaults when it has none), and the license later saves go to.
void loadGameSettings(unsigned license);
void saveGameSettings();
// The pause menu's buttons: the next LED mode, the next brightness step (wrapping).
void cycleLedMode();
void cycleLedBrightness();
const char* ledModeLabel(LedMode mode);
}
