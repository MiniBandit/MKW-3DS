#pragma once
#include <mkw/driver_controller.hpp>

namespace Kinoko::Kart { class KartObject; }
namespace mkw {
class RaceItems;
// Reads one Kinoko kart the way the retail driver model reads its KartObject
// each frame, for the player and the opponents alike. Presentation only.
class DriverSignalReader {
public:
    void reset(){*this=DriverSignalReader{};}
    // After the race step. The trick events are the rising edges of InATrick
    // (KartJump::start -> setTrick(variant, trick)) and ZipperTrick
    // (KartHalfPipe::activateTrick -> setTrick(2, trick)).
    DriverSignals read(const Kinoko::Kart::KartObject& kart);
private:
    bool wasTrick_=false,wasZipperTrick_=false,wasCharged_=false,mtBoost_=false;
};
// This step's item events for one racer: a held item released (the Release cue, fn_1_2C23E4)
// and an item of this racer starting another racer's action (gut, fn_1_2C1614 from
// PlayerSub18_checkItemCollision 0x8057257C).
void readItemEvents(DriverSignals& signals,const RaceItems& items,unsigned player);
// The Blooper ink age fn_1_6ECA8 reads: frames since the ink landed, -1 without ink.
int inkFrames(const RaceItems& items,unsigned player);
}
