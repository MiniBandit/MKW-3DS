#include "ObjectCarB.hh"
#include "game/system/RaceManager.hh"
namespace Kinoko::Field {
void ObjectCarB::init() {
    // USA80814100: independently select CarB1, CarB2 or CarB once per load.
    m_variant = System::RaceManager::Instance()->random().getU32(3);
}
}
