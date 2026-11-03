#include "game/field/obj/ObjectKarehayama.hh"
#include "game/field/obj/ObjectChimney.hh"
#include "game/field/obj/ObjectLeafEffect.hh"
#include "game/field/obj/ObjectTollGate.hh"
#include "game/field/obj/ObjectVenice.hh"
#include "game/field/obj/ObjectQuicksand.hh"
#include "game/field/obj/ObjectTwanwan.hh"
#include "game/field/obj/ObjectCasinoRoulette.hh"
#include "ObjectDirector.hh"
#include "game/field/obj/ObjectKoopaFigure.hh"
#include "game/field/obj/ObjectCannon.hh"
#include "game/field/obj/ObjectCarB.hh"
#include "game/field/obj/ObjectBGTeresa.hh"
#include "game/field/obj/ObjectTeresa.hh"
#include "game/field/obj/ObjectFactoryEffect.hh"
#include <cstring>

#include "game/field/BoxColManager.hh"
#include "game/field/ObjectDrivableDirector.hh"
#include "game/field/ObjectCollisionSphere.hh"
#include "game/field/obj/ObjectRegistry.hh"
#include "game/field/obj/ObjectBlockStatue.hh"

#include "game/kart/KartObject.hh"

#include "game/system/CourseMap.hh"
#include "game/system/RaceConfig.hh"
#include "game/system/RaceManager.hh"

namespace Kinoko::Field {

/// @addr{0x8082A2B4}
void ObjectDirector::init() {
    for (auto *&obj : m_objects) {
        obj->init();
        obj->calcModel();
    }

    ObjectDrivableDirector::Instance()->init();
}

/// @addr{0x8082A8F4}
void ObjectDirector::calc() {
    for (auto *&obj : m_calcObjects) {
        obj->calc();
    }

    for (auto *&obj : m_calcObjects) {
        obj->calcModel();
    }

    ObjectDrivableDirector::Instance()->calc();
}

/// @addr{0x8082B0E8}
void ObjectDirector::addObject(ObjectCollidable *obj) {
    u32 loadFlags = obj->loadFlags();

    if (loadFlags & 1) {
        m_calcObjects.push_back(obj);
    }

    const auto *set = m_flowTable.set(m_flowTable.slot(obj->id()));

    // In the base game, it's possible an object here will access slot -1 (e.g. Moonview Highway
    // cars). We add a nullptr check here to prevent this.
    if (set && set->mode != 0) {
        if (obj->collision()) {
            m_collisionObjects.push_back(obj);
        }
    }

    m_objects.push_back(obj);
}

void ObjectDirector::addObjectNoImpl(ObjectBase *obj) {
    m_objects.push_back(obj);
}

/// @addr{0x806C4ED4}
void ObjectDirector::addManagedObject(ObjectCollidable *obj) {
    m_managedObjects.push_back(obj);
}

/// @addr{0x8082AB04}
// USA808172a8: collect every source primitive hit and object callback before
// the item's controller applies its own fate. Reuse the completed kart-pass
// scratch arrays; no additional per-item collision pool is needed.
size_t ObjectDirector::checkItemObjectCollision(s16 type, u8 owner, const EGG::Vector3f &position,
        const EGG::Vector3f &velocity, f32 radius, bool held) {
    if (type < 0 || type >= 15 || radius <= 0.0f) return 0;
    m_itemOwner = owner < 12 ? owner : 12;
    BoxColFlag flag;
    flag.setBit(eBoxColFlag::Object);
    BoxColManager::Instance()->search(radius, position, flag);
    ObjectCollisionSphere sphere(radius, position);
    sphere.transform(EGG::Matrix34f::ident, EGG::Vector3f(1.0f, 1.0f, 1.0f));
    size_t count = 0;
    while (ObjectCollidable *obj = BoxColManager::Instance()->getNextObject()) {
        if (!obj->collision()) continue;
        const s16 itemSlot = m_hitTableItem.slot(obj->id());
        const s16 objectSlot = m_hitTableItemObject.slot(obj->id());
        if (itemSlot < 0 || objectSlot < 0) continue;
        s16 itemResponse = static_cast<s16>(m_hitTableItem.reaction(itemSlot, type));
        s16 objectResponse = static_cast<s16>(m_hitTableItemObject.reaction(objectSlot, type));
        // Held objects ignore bounce and object-only contacts (source bit0).
        if ((!itemResponse && (!objectResponse || objectResponse == 7)) ||
                (held && (itemResponse == 0 || itemResponse == 1))) continue;
        obj->calcCollisionTransform();
        if (!sphere.check(*obj->collision(), m_hitDepths[count])) continue;
        obj->processItemReactions(type, false, itemResponse, objectResponse);
        itemResponse = obj->onItemCollision(position, velocity, itemResponse,
                objectResponse, m_hitDepths[count]);
        m_collidingObjects[count] = obj;
        m_reactions[count] = static_cast<Kart::Reaction>(itemResponse);
        if (++count == MAX_UNIT_COUNT) break;
    }
    return count;
}

// USA80817780 visits the full object registry, including temporarily
// intangible objects. Neither the spatial query nor an item sphere applies.
void ObjectDirector::broadcastItemCollision(s16 type) {
    if (type < 0 || type >= 15) return;
    const u8 previousOwner = m_itemOwner;
    m_itemOwner = 12; // The original callback receives a null item.
    for (auto *obj : m_objects) {
        const s16 itemSlot = m_hitTableItem.slot(obj->id());
        const s16 objectSlot = m_hitTableItemObject.slot(obj->id());
        if (itemSlot < 0 || objectSlot < 0) continue;
        s16 itemResponse = static_cast<s16>(m_hitTableItem.reaction(itemSlot, type));
        s16 objectResponse = static_cast<s16>(m_hitTableItemObject.reaction(objectSlot, type));
        obj->processItemReactions(type, true, itemResponse, objectResponse);
        obj->onGlobalItemCollision(itemResponse, objectResponse);
    }
    m_itemOwner = previousOwner;
}

size_t ObjectDirector::checkKartObjectCollision(Kart::KartObject *kartObj,
        ObjectCollisionConvexHull *convexHull) {
    // Dad's requested Bullet policy: course primitive hazards are ignored.
    // KCL terrain/drivable support and the separate kart-vs-kart pass remain.
    if (kartObj->status().onBit(Kart::eStatus::Bullet)) return 0;
    size_t count = 0;

    while (ObjectCollidable *obj = BoxColManager::Instance()->getNextObject()) {
        auto *objCollision = obj->collision();
        if (!objCollision) {
            continue;
        }

        obj->calcCollisionTransform();
        if (!obj->checkCollision(convexHull, m_hitDepths[count])) {
            continue;
        }

        // We have a collision, process it. Star, Mega and Bullet read their own column.
        const s16 column = ObjectHitTable::KartColumn(kartObj);
        Kart::Reaction reactionOnKart =
                m_hitTableKart.reaction(m_hitTableKart.slot(obj->id()), column);
        Kart::Reaction reactionOnObj =
                m_hitTableKartObject.reaction(m_hitTableKartObject.slot(obj->id()), column);

        // The object might change the reaction states
        obj->processKartReactions(kartObj, reactionOnKart, reactionOnObj);

        Kart::Reaction reaction =
                obj->onCollision(kartObj, reactionOnKart, reactionOnObj, m_hitDepths[count]);
        m_reactions[count] = reaction;

        if (reaction == Kart::Reaction::WallAllSpeed || reaction == Kart::Reaction::WallSpark) {
            obj->onWallCollision(kartObj, m_hitDepths[count]);
        } else {
            obj->onObjectCollision(kartObj);
        }

        m_collidingObjects[count] = obj;
        if (m_hitDepths[count].y < 0.0f) {
            m_hitDepths[count].y = 0.0f;
        }

        ++count;
    }

    return count;
}

/// @addr{0x8082B3EC}
f32 ObjectDirector::distAboveRisingWater(f32 offset) const {
    ASSERT(m_psea);
    return offset - m_psea->pos().y;
}

/// @addr{0x8082B400}
f32 ObjectDirector::risingWaterKillPlaneHeight() const {
    ASSERT(m_psea);
    return m_psea->pos().y - 260.0f;
}

/// @addr{0x8082A784}
ObjectDirector *ObjectDirector::CreateInstance() {
    ASSERT(!s_instance);
    s_instance = EGG::egg_new<ObjectDirector>();

    ObjectDrivableDirector::CreateInstance();

    s_instance->createObjects();

    return s_instance;
}

/// @addr{0x8082A824}
void ObjectDirector::DestroyInstance() {
    ASSERT(s_instance);
    auto *instance = s_instance;
    s_instance = nullptr;
    EGG::egg_delete(instance);

    ObjectDrivableDirector::DestroyInstance();
}

/// @addr{0x8082A38C}
ObjectDirector::ObjectDirector()
    : m_flowTable("ObjFlow.bin"), m_hitTableKart("GeoHitTableKart.bin"),
      m_hitTableKartObject("GeoHitTableKartObj.bin"), m_hitTableItem("GeoHitTableItem.bin"),
      m_hitTableItemObject("GeoHitTableItemObj.bin"), m_objects(MAX_UNIT_COUNT),
      m_calcObjects(MAX_UNIT_COUNT), m_collisionObjects(MAX_UNIT_COUNT), m_psea(nullptr),
      m_managedObjects(MAX_MANAGED_OBJECTS) {}

/// @addr{0x8082A694}
ObjectDirector::~ObjectDirector() {
    if (s_instance) {
        s_instance = nullptr;
        WARN("ObjectDirector instance not explicitly handled!");
    }

    for (auto *&obj : m_objects) {
        EGG::egg_delete(obj);
    }
}

/// @addr{0x80826E8C}
void ObjectDirector::createObjects() {
    const auto *courseMap = System::CourseMap::Instance();
    size_t objectCount = courseMap->getGeoObjCount();

    auto *objDrivableDir = ObjectDrivableDirector::Instance();
    auto course = System::RaceConfig::Instance()->raceScenario().course;
    bool rGV2 = course == Course::SNES_Ghost_Valley_2;
    bool sun = false;

    // Retail (USA 0x80813378) builds no item boxes and no Bob-omb cars while ObjectDirector
    // +0x55 is set, as in time trials. Kinoko's time trials replay a ghost as player 0.
    const auto &scenario = System::RaceConfig::Instance()->raceScenario();
    const bool timeTrial = scenario.playerCount > 0 &&
            scenario.players[0].type == System::RaceConfig::Player::Type::Ghost;

    // The max pitch of the Chain Chomp only needs to be set once,
    // so we set it here instead of in the Chain Chomp constructor.
    s_wanwanMaxPitch = course == Course::GCN_Mario_Circuit ? -20.0f : -30.0f;

    for (size_t i = 0; i < objectCount; ++i) {
        const auto *pObj = courseMap->getGeoObj(i);
        ASSERT(pObj);

        // Assume one player - if the presence flag isn't set, don't construct it
        if (!(pObj->presenceFlag() & 1)) {
            continue;
        }

        // Prevent construction of objects with disabled or no collision
        if (IsObjectBlacklisted(pObj->id())) {
            continue;
        }

        const auto objectId = static_cast<ObjectId>(pObj->id());
        if (timeTrial && (objectId == ObjectId::KBombCar || objectId == ObjectId::KBombCar1)) {
            continue;
        }

        // rGV2's blocks are created outside of the factory function
        if (rGV2) {
            switch (static_cast<ObjectId>(pObj->id())) {
            case ObjectId::ObakeBlockSFCc:
            case ObjectId::ObakeBlock2SFCc:
            case ObjectId::ObakeBlock3SFCc: {
                // Create the manager if this is the first block.
                auto *obakeManager = objDrivableDir->obakeManager();
                if (!obakeManager) {
                    objDrivableDir->createObakeManager(*pObj);
                } else {
                    obakeManager->addBlock(*pObj);
                }
            } break;
            default: {
                // Only the blocks belong to ObakeManager. Ghosts and other
                // placements still need their normal native controllers.
                ObjectBase *object = createObject(*pObj);
                object->load();
            } break;
            }
        } else {
            ObjectBase *object = createObject(*pObj);
            object->load();
        }

        if (static_cast<ObjectId>(pObj->id()) == ObjectId::SunDS) {
            sun = true;
        }
    }

    if (course == Course::Moonview_Highway) {
        auto *highwayMgr = EGG::egg_new<ObjectHighwayManager>();
        highwayMgr->load();
    }

    if (sun) {
        auto *sunMgr = EGG::egg_new<ObjectSunManager>();
        sunMgr->load();
    }

    if (course == Course::GBA_Shy_Guy_Beach) {
        auto *shipMgr = EGG::egg_new<ObjectHeyhoShipManager>();
        shipMgr->load();
    }
}

/// @addr{0x80821E14}
ObjectBase *ObjectDirector::createObject(const System::MapdataGeoObj &params) {
    const auto *flow = m_flowTable.set(m_flowTable.slot(static_cast<ObjectId>(params.id())));
    if (flow && (!std::strcmp(flow->name, "truckChimSmk") || !std::strcmp(flow->name, "truckChimSmkW")))
        return EGG::egg_new<ObjectChimney>(params);
    if (flow && !std::strcmp(flow->name, "karehayama"))
        return EGG::egg_new<ObjectKarehayama>(params);
    if (flow && !std::strcmp(flow->name, "leaf_effect"))
        return EGG::egg_new<ObjectLeafEffect>(params);
    if (flow && (!std::strcmp(flow->name, "Steam") || !std::strcmp(flow->name, "Alarm")))
        return EGG::egg_new<ObjectFactoryEffect>(params);
    if (flow && !std::strcmp(flow->name, "b_teresa"))
        return EGG::egg_new<ObjectTeresa>(params);
    if (flow && !std::strcmp(flow->name, "BGteresaSFC"))
        return EGG::egg_new<ObjectBGTeresa>(params);
    if (flow && !std::strcmp(flow->name, "CarB"))
        return EGG::egg_new<ObjectCarB>(params);
    if (flow && ObjectCannon::matches(flow->name))
        return EGG::egg_new<ObjectCannon>(params);
    if (flow && !std::strcmp(flow->name, "koopaFigure"))
        return EGG::egg_new<ObjectKoopaFigure>(params);
    ObjectId id = static_cast<ObjectId>(params.id());
    switch (id) {
    case ObjectId::Psea:
        return EGG::egg_new<ObjectPsea>(params);
    case ObjectId::Woodbox:
        return EGG::egg_new<ObjectWoodbox>(params);
    case ObjectId::WLWallGC:
        return EGG::egg_new<ObjectWLWallGC>(params);
    case ObjectId::CarA1:
    case ObjectId::CarA2:
    case ObjectId::CarA3:
        return EGG::egg_new<ObjectCarA>(params);
    case ObjectId::Basabasa:
        return EGG::egg_new<ObjectBasabasa>(params);
    case ObjectId::HeyhoShipGBA:
        return EGG::egg_new<ObjectHeyhoShip>(params);
    case ObjectId::KartTruck:
    case ObjectId::CarBody:
    case ObjectId::KBombCar:
    case ObjectId::KBombCar1:
        return EGG::egg_new<ObjectCarTGE>(params);
    case ObjectId::KoopaBall:
        return EGG::egg_new<ObjectKoopaBall>(params);
    case ObjectId::W_Woodbox:
        return EGG::egg_new<ObjectWoodboxW>(params);
    case ObjectId::SunDS:
        return EGG::egg_new<ObjectSunDS>(params);
    case ObjectId::ItemboxLine:
        return EGG::egg_new<ObjectItemboxLine>(params);
    case ObjectId::VolcanoBall:
        return EGG::egg_new<ObjectVolcanoBallLauncher>(params);
    case ObjectId::PenguinS:
        return EGG::egg_new<ObjectPenguinS>(params);
    case ObjectId::PenguinM:
        return EGG::egg_new<ObjectPenguin>(params);
    case ObjectId::PenguinL:
        return EGG::egg_new<ObjectPenguinL>(params);
    case ObjectId::Dossunc:
        return EGG::egg_new<ObjectDossunc>(params);
    case ObjectId::Boble:
        return EGG::egg_new<ObjectBoble>(params);
    case ObjectId::Hanachan:
        return EGG::egg_new<ObjectHanachan>(params);
    case ObjectId::Seagull:
        return EGG::egg_new<ObjectBird>(params);
    case ObjectId::Crab:
        return EGG::egg_new<ObjectCrab>(params);
    case ObjectId::Hwanwan:
        return EGG::egg_new<ObjectHwanwanManager>(params);
    case ObjectId::HeyhoBallGBA:
        return EGG::egg_new<ObjectHeyhoBall>(params);
    case ObjectId::DokanSFC:
        return EGG::egg_new<ObjectDokan>(params);
    case ObjectId::Pylon:
        return EGG::egg_new<ObjectPylon>(params);
    case ObjectId::OilSFC:
        return EGG::egg_new<ObjectOilSFC>(params);
    case ObjectId::ParasolR:
        return EGG::egg_new<ObjectParasolR>(params);
    case ObjectId::KoopaFigure64:
        return EGG::egg_new<ObjectKoopaFigure64>(params);
    case ObjectId::Kuribo:
        return EGG::egg_new<ObjectKuribo>(params);
    case ObjectId::Choropu:
    case ObjectId::Choropu2:
        return EGG::egg_new<ObjectChoropu>(params);
    case ObjectId::Cow:
        return EGG::egg_new<ObjectCowHerd>(params);
    case ObjectId::PakkunF:
        return EGG::egg_new<ObjectPakkunF>(params);
    case ObjectId::WLFirebarGC:
    case ObjectId::KoopaFirebar:
        return EGG::egg_new<ObjectFirebar>(params);
    case ObjectId::Wanwan:
        return EGG::egg_new<ObjectWanwan>(params);
    case ObjectId::Poihana:
        return EGG::egg_new<ObjectPoihana>(params);
    case ObjectId::Propeller:
        return EGG::egg_new<ObjectPropeller>(params);
    case ObjectId::DKRockGC:
        return EGG::egg_new<ObjectRock>(params);
    case ObjectId::Sanbo:
        return EGG::egg_new<ObjectSanbo>(params);
    case ObjectId::TruckWagon:
        return EGG::egg_new<ObjectTruckWagon>(params);
    case ObjectId::Heyho:
        return EGG::egg_new<ObjectHeyho>(params);
    case ObjectId::Press:
        return EGG::egg_new<ObjectPress>(params);
    case ObjectId::WLFireRingGC:
        return EGG::egg_new<ObjectFireRing>(params);
    case ObjectId::FireSnake:
        return EGG::egg_new<ObjectFireSnake>(params);
    case ObjectId::FireSnakeV:
        return EGG::egg_new<ObjectFireSnakeV>(params);
    case ObjectId::PuchiPakkun:
        return EGG::egg_new<ObjectPuchiPakkun>(params);
    case ObjectId::KinokoUd:
        return EGG::egg_new<ObjectKinokoUd>(params);
    case ObjectId::KinokoBend:
        return EGG::egg_new<ObjectKinokoBend>(params);
    case ObjectId::VolcanoRock:
        return EGG::egg_new<ObjectVolcanoRock>(params);
    case ObjectId::BulldozerL:
    case ObjectId::BulldozerR:
        return EGG::egg_new<ObjectBulldozer>(params);
    case ObjectId::KinokoNm:
        return EGG::egg_new<ObjectKinokoNm>(params);
    case ObjectId::Crane:
        return EGG::egg_new<ObjectCrane>(params);
    case static_cast<ObjectId>(0x166):case static_cast<ObjectId>(0x186):
    case static_cast<ObjectId>(0x187):case static_cast<ObjectId>(0x188):
        if(System::RaceConfig::Instance()->raceScenario().course==Course::Block_Plaza)
            return EGG::egg_new<ObjectBlockStatue>(params);
        return nullptr;
    case static_cast<ObjectId>(0x20d): // Retail bblock derives from VolcanoPiece.
    case ObjectId::VolcanoPiece:
        return EGG::egg_new<ObjectVolcanoPiece>(params);
    case ObjectId::FlamePole:
        return EGG::egg_new<ObjectFlamePoleFoot>(params);
    case ObjectId::TwistedWay:
        return EGG::egg_new<ObjectTwistedWay>(params);
    case ObjectId::TownBridge:
        return EGG::egg_new<ObjectTownBridge>(params);
    case ObjectId::DKShip64:
        return EGG::egg_new<ObjectShip64>(params);
    case ObjectId::Turibashi:
        return EGG::egg_new<ObjectTuribashi>(params);
    case ObjectId::Aurora:
        return EGG::egg_new<ObjectAurora>(params);
    case ObjectId::DCPillar:
        return EGG::egg_new<ObjectPillar>(params);
    case ObjectId::Sandcone:
        return EGG::egg_new<ObjectSandcone>(params);
    case ObjectId::FlamePoleV:
    case ObjectId::FlamePoleVBig:
        return EGG::egg_new<ObjectFlamePoleV>(params);
    case ObjectId::Ami:
        return EGG::egg_new<ObjectAmi>(params);
    case ObjectId::BeltEasy:
        return EGG::egg_new<ObjectBeltEasy>(params);
    case ObjectId::BeltCrossing:
        return EGG::egg_new<ObjectBeltCrossing>(params);
    case ObjectId::BeltCurveA:
        return EGG::egg_new<ObjectBeltCurveA>(params);
    case ObjectId::Escalator:
        return EGG::egg_new<ObjectEscalator>(params);
    case ObjectId::EscalatorGroup:
        return EGG::egg_new<ObjectEscalatorGroup>(params);

    // Non-specified objects are stock collidable objects by default
    // However, we need to specify an impl, so we don't use default
    case ObjectId::DummyPole:
    case ObjectId::CastleTree1c:
    case ObjectId::MarioTreeGCc:
    case ObjectId::PeachTreeGCc:
    case ObjectId::MarioGo64c:
    case ObjectId::KinokoT1:
    case ObjectId::PalmTree:
    case ObjectId::Parasol:
    case ObjectId::HeyhoTreeGBAc:
    case ObjectId::GardenTreeDSc:
    case ObjectId::DKtreeA64c:
    case ObjectId::DKTreeB64c:
    case ObjectId::TownTreeDsc:
    case ObjectId::PakkunDokan:
        return EGG::egg_new<ObjectCollidable>(params);
    case static_cast<ObjectId>(0x148):
    case static_cast<ObjectId>(0x164):
        return EGG::egg_new<ObjectTollGate>(params);
    case static_cast<ObjectId>(0xeb):return EGG::egg_new<ObjectTwanwan>(params);
    case static_cast<ObjectId>(0x20c):return EGG::egg_new<ObjectQuicksand>(params);
    case static_cast<ObjectId>(0x4):return EGG::egg_new<ObjectVeniceWater>(params);
    case static_cast<ObjectId>(0x205):return EGG::egg_new<ObjectVeniceGate>(params);
    case static_cast<ObjectId>(0x20a):return EGG::egg_new<ObjectVeniceBridge>(params);
    case static_cast<ObjectId>(0x206):
        return EGG::egg_new<ObjectCasinoRoulette>(params);
    case ObjectId::WLDokanGC:
    case ObjectId::Mdush:
        return EGG::egg_new<ObjectKCL>(params);
    default:
        return EGG::egg_new<ObjectNoImpl>(params);
    }
}

f32 ObjectDirector::s_wanwanMaxPitch; ///< @addr{0x808C70E8}

ObjectDirector *ObjectDirector::s_instance = nullptr; ///< @addr{0x809C4330}

} // namespace Kinoko::Field
