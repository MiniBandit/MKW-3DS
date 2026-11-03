#include "ObjectHighwayManager.hh"
#include "ObjectTollGate.hh"

#include "game/field/ObjectDirector.hh"
#include "game/field/obj/ObjectCarTGE.hh"

#include "game/system/RaceConfig.hh"

namespace Kinoko::Field {

/// @addr{0x806D2908}
ObjectHighwayManager::ObjectHighwayManager()
    : ObjectCollidable("HighwayManager", EGG::Vector3f::zero, EGG::Vector3f::ez,
              EGG::Vector3f::unit) {
    size_t carCount = 0;
    auto *objDir = ObjectDirector::Instance();

    for (auto *const &obj : objDir->managedObjects()) {
        const char *objName = obj->getName();

        if ((strcmp(objName, "car_body") == 0) || (strcmp(objName, "kart_truck") == 0)) {
            ++carCount;
        }
    }

    m_cars = owning_span<ObjectCarTGE *>(carCount);
    size_t idx = 0;

    for (auto *&obj : objDir->managedObjects()) {
        const char *objName = obj->getName();

        if ((strcmp(objName, "car_body") == 0) || (strcmp(objName, "kart_truck") == 0)) {
            auto *carObj = reinterpret_cast<ObjectCarTGE *>(obj);
            m_cars[idx++] = carObj;
            carObj->setHighwayManager(this);
        }
    }
}

/// @addr{0x806D2FE8}
ObjectHighwayManager::~ObjectHighwayManager() = default;

/// @addr{0x806D332C}
void ObjectHighwayManager::init() {
    m_squashTimer.fill(SQUASH_MAX);
}

/// @addr{0x806D345C}
void ObjectHighwayManager::calc() {
    calcSquash();
    // Retail USA806c6650..66e4: each road car checks the stationary gate volume.
    for(auto* object:ObjectDirector::Instance()->managedObjects()) {
        if(object->id()!=static_cast<ObjectId>(0x164))continue;
        auto* gate=static_cast<ObjectTollGate*>(object);
        for(auto* car:m_cars) {
            if(car->carType()==ObjectCarTGE::CarType::BombCar&&!car->bombShown())continue;
            car->calcCollisionTransform();gate->calcCollisionTransform();
            EGG::Vector3f depth;
            if(car->checkCollision(gate->collision(),depth))gate->open();
        }
    }
}

/// @addr{0x806D50AC}
/// @details Retail (USA 0x806C64DC) keeps a timer per player and a squashed flag per player
/// on every car: each player's timer restarts when any car squashed that player while the
/// player was vulnerable, then every car's flags clear and every timer counts on.
void ObjectHighwayManager::calcSquash() {
    constexpr u32 SQUASH_INVULNERABILITY = 200;

    const size_t players = std::min<size_t>(
            System::RaceConfig::Instance()->raceScenario().playerCount, MAX_PLAYERS);

    for (size_t player = 0; player < players; ++player) {
        for (auto *&obj : m_cars) {
            if (obj->squashed(player) && m_squashTimer[player] >= SQUASH_INVULNERABILITY) {
                m_squashTimer[player] = 0;
            }
        }
    }

    for (auto *&obj : m_cars) {
        obj->reset();
    }

    for (size_t player = 0; player < players; ++player) {
        m_squashTimer[player] = std::min<u32>(m_squashTimer[player] + 1, SQUASH_MAX);
    }
}

} // namespace Kinoko::Field
