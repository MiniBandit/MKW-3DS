#pragma once

namespace Kinoko::Kart {

// RMCU 8057af14/8057afc0 and 8057ab84. Keep these clocks separate:
// damage protection, eligible zero-balloon ticks, then the vanish/respawn wait.
// This is pure state so its timing can be checked without booting a race.
class BattleRecovery {
public:
    enum class Event { None, Vanish, Hidden, Wipe, Respawn };
    static constexpr int ProtectionTicks = 150, RespawnTicks = 200;

    void protect() { protection_ = ProtectionTicks; blinkTicks_ = 0; visible_ = false; }
    void empty() { waiting_ = true; refillTicks_ = 0; protect(); }
    void reset() { *this = {}; }

    Event step(bool eligible) {
        Event event = Event::None;
        // Retail calcVanish precedes calcBattleRecovery in KartMove::calc.
        if (shrinking_) {
            scale_ -= 0.05f; // RMCU 8088d530
            if (scale_ < 0.001f) { // RMCU 8088d5b0
                scale_ = 0.001f;
                shrinking_ = false;
                hidden_ = true;
                event = Event::Hidden;
            }
        }
        if (respawning_ && eligible) {
            // USA8057acac..accc: type0 wipe starts with90 ticks left.
            // Check the old counter before its increment, as the retail call does.
            if(respawnTicks_ == RespawnTicks-90)event=Event::Wipe;
            if(++respawnTicks_ >= RespawnTicks){respawning_=false;event=Event::Respawn;}
        }
        if (protection_ > 0) {
            if (--protection_ == 0) { visible_ = true; blinkTicks_ = 0; }
            else if (++blinkTicks_ >= 20) { visible_ = !visible_; blinkTicks_ = 0; }
        }
        if (waiting_ && eligible && ++refillTicks_ >= ProtectionTicks) {
            waiting_ = false;
            shrinking_ = respawning_ = true;
            refillTicks_ = respawnTicks_ = 0;
            event = Event::Vanish;
        }
        return event;
    }

    bool protectedFromDamage() const { return protection_ > 0; }
    bool emptyPending() const { return waiting_ || respawning_ || hidden_; }
    bool hidden() const { return hidden_; }
    bool visible() const { return !hidden_ && visible_; }
    float scale() const { return scale_; }

private:
    int protection_ = 0, blinkTicks_ = 0, refillTicks_ = 0, respawnTicks_ = 0;
    bool waiting_ = false, shrinking_ = false, respawning_ = false, hidden_ = false;
    bool visible_ = true;
    float scale_ = 1.0f;
};

} // namespace Kinoko::Kart
