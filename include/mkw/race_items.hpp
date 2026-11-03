#pragma once
#include "mkw/item_ground_reclaim.hpp"
#include "mkw/math.hpp"
#include "mkw/item_karts.hpp"
#include "mkw/item_odds.hpp"
#include "mkw/item_roulette.hpp"
#include "mkw/held_items.hpp"
#include "mkw/item_properties.hpp"
#include "mkw/shell_route.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Kinoko::Field {class RailInterpolator;class ObjectItemboxPress;class ObjectDrivable;}
namespace mkw {
enum class ItemKind : uint8_t {
    GreenShell=0, RedShell=1, Banana=2, FakeBox=3, Mushroom=4,
    TripleMushroom=5, Bobomb=6, BlueShell=7, Lightning=8, Star=9,
    GoldenMushroom=10, MegaMushroom=11, Blooper=12, Pow=13,
    ThunderCloud=14, Bullet=15, TripleGreenShell=16, TripleRedShell=17,
    TripleBanana=18, None=20,
};
const char* itemName(ItemKind kind);
struct ItemInput { bool use=false, backwards=false, forwards=false; };
struct ItemHud {
    ItemKind item=ItemKind::None;
    uint16_t roulette=0, star=0, mega=0, bullet=0, ink=0, cloud=0, golden=0, pow=0;
    uint16_t inkDelay=0;
    uint8_t inkShade=0; // Visual-only fade, updated once per simulation tick.
    uint8_t charges=0;
    bool deployed=false;
    ItemKind rouletteIcon=ItemKind::None;
    uint8_t heldMask=0,heldCharges=0;
    // Retail ItemHolderPlayer +0x78 (its roulette's +0x24): an item that keeps the item
    // window showing and the roulette locked with an empty inventory. Set by the Blue
    // Shell's use for the update until its tether releases it, and by the Bullet Bill
    // until it ends (cancelBullet 0x805B1100). (The Thunder Cloud's 60 updates are its
    // charge-less inventory entry.) Deployed held groups empty the inventory and set none.
    ItemKind slotLock=ItemKind::None;
    // The racer whose Thunder Cloud passed to this one during the current step (12: none).
    uint8_t cloudFrom=12;
    ItemKind heldKind=ItemKind::None;
};
// The item window's picture (CtrlRaceItemWindow_calcSelf 0x807e4c54): the inventory item
// with its count as the variant, else the slot lock with variant 1; kind None hides it.
struct ItemWindowPicture { ItemKind kind=ItemKind::None; uint8_t variant=0; };
inline ItemWindowPicture itemWindowPicture(const ItemHud& h){
    if(h.item!=ItemKind::None)return {h.item,h.charges};
    if(h.slotLock!=ItemKind::None)return {h.slotLock,1};
    return {};
}
struct ItemBox {
    // World pickup center, including the source object's 180-unit hover offset.
    Vec3 position{}, scale{.9f,.9f,.9f};
    uint16_t phase=0;
    uint16_t respawn=0;
    bool active=true;
    uint8_t appearance=19;
    // ObjFlow sphere radius before scale: 215, or 235 for moving boxes.
    float radius=215.f;
};
// Course item boxes that travel along a KMP route (retail RMCP 80828f44
// f_itembox, 8077cec8 sin_itembox, 8076c85c w_itembox lines).
struct ItemBoxMover {
    enum class Kind : uint8_t { Route, Sine, Line };
    Kind kind=Kind::Route;
    std::shared_ptr<Kinoko::Field::RailInterpolator> rail;
    uint16_t box=0;
    float lift=180;            // hover above the route
    int32_t delay=0;           // sine: frames before it starts swinging
    float angle=90,step=1,half=0,previous=0;
    bool riding=false;         // line: launched and travelling
};
// A w_itembox line launches its boxes in turn from the route start.
struct ItemBoxLine { std::vector<uint16_t> movers; int32_t timer=0,period=0; uint16_t next=0; };
// Toad's Factory belt slot (Kinoko ObjectItemboxPress): a box from the route
// point marked 2 to the belt end; collected boxes stay gone for that pass.
struct ItemBoxPress { const Kinoko::Field::ObjectItemboxPress* object=nullptr; uint16_t box=0,reveal=0xffff; bool taken=false; };
struct ItemActor {
    ItemKind kind=ItemKind::None;
    Vec3 position{}, previous{}, velocity{},flightDirection{0,0,1};
    // The object's model up axis (retail matrix column 1): the thrower's up, then the floor's.
    Vec3 up{0,1,0};
    float yaw=0, scale=1, radius=45,terrainRadius=40,travelSpeed=0;
    uint32_t age=0;
    uint16_t lifetime=0;
    uint32_t floorTypes=0;
    int8_t owner=-1, target=-1;
    uint8_t bounces=0;
    uint8_t bounceLimit=10;
    bool active=false, explosion=false,backwards=false;
    bool settled=false, scattered=false;
    int16_t fuse=300;
    uint16_t hitMask=0;
    // RMCU ObjItem +0x78 bit 0x20000: the thrower is immune until the item is older than
    // ownerImmunity frames (30 thrown, 15 trap throw, 7 ejected by damage).
    uint8_t ownerImmunity=30;
    float blastInitialScale=1,bounceImpulse=0;
    uint8_t floorBounces=0;
    ShellGuidance guidance{};
    Vec3 visualPosition()const{return kind==ItemKind::BlueShell&&!explosion&&guidance.phase<4?Vec3{position.x,guidance.visualY,position.z}:position;}
    bool diving()const{return kind==ItemKind::BlueShell&&!explosion&&guidance.phase==5;}
    // Model scale: uniform, except the Blue's dive stretch (fn_28F9D0).
    Vec3 drawScale()const{
        if(!diving())return {scale,scale,scale};
        const float side=2*scale-guidance.stretch;return {side,guidance.stretch,side};
    }
};
struct HeldItemVisual { Vec3 right{1,0,0},up{0,1,0},forward{0,0,1},scale{}; };
// Presentation events: what an item did to a racer this step.
enum class ItemEffect : uint8_t { Mushroom, Spin, Launch, Explosion, Shrink, Crush, Cure, Knockback, Pow, Ink, Break, PowStomp, LightningFlash, Bolt, BlueLaunch, ElectricHit, BlooperLaunch };
// Object events (Break, and Explosion with action -1) carry the item kind in `frames`, its
// position in `direction` and its model scale. PowStomp carries the stomp in `frames` and
// action 1 for a target, 0 for the user. Bolt (KartEffect +0x4c / +0x50 reset) has frames 0
// for a Lightning bolt and 1 for the Thunder Cloud's strike.
struct ItemEvent { ItemEffect effect; uint8_t player; uint16_t frames=0; Vec3 direction{}; int8_t action=-1; float scale=1; uint8_t source=0xff; ItemKind item=ItemKind::None; };
struct ItemSurface { Vec3 correction{}, floorNormal{}, wallNormal{}; bool kill=false; bool cannon=false; uint32_t floorTypes=0;
    // Original CoinUnit ground probe needs the collision result even at exact
    // tangency. Support stays scene-owned; no item owns or frees this pointer.
    bool collided=false;const void* floorObject=nullptr;float floorObjectY=0;
    Vec3 floorMotion{}; // Scene-owned roulette support displacement for this contact.
};
// Sound cues: where retail item objects and holders call Audio::ItemActor, the
// holder's KartActor or its CharacterActor (docs/AUDIO_SOURCE.md "Items").
enum class ItemSoundCue : uint8_t {
    Throw,      // a trap item thrown forward (ObjBanana/Bomb/FIB SetInitialPosition: SE_ITM_BANANA_FLY)
    Land,       // a thrown trap item comes to rest (ObjThrowable::OnFinishThrow: the object's land sound)
    Bounce,     // a Bob-omb hits the floor (fn_1_294794: SE_ITM_BOMB_GND)
    WallBounce, // a shell glances off a wall (SE_ITM_KAME_HANSHA)
    Break,      // destroyed by another object, a wall or old age (Obj::KillFromOtherCollision)
    Hit,        // destroyed against a racer (Obj::KillFromPlayerCollision)
    Explode,    // a Bob-omb or Blue Shell blast starts
    Equip,      // the holder's item settles into place (Item::PlayerObj: SE_ITM_EQUIP)
    Decide,     // the roulette stops; flag: a Thunder Cloud (SE_RC_ITEM_DECIDE(_MISS))
    Release,    // a held item leaves its holder; flag: thrown forward (CharacterActor ITM_TRW / ITM_PUT)
    // The racer's own actors (KartActor, GessoActor) and the global ambience player:
    FakeBox,    // a Fake Item Box touches the racer (HandleFibCollision: SE_ITM_BOX_HIT_FX; flag: passed
                // through by Star, Mega or Bullet, SE_ITM_BOX_SMOKE)
    Barrier,    // a Star, Mega or Bullet racer meets a shell, Banana or Fake Item Box (SE_ITM_STAR_BARRIER)
    Ink,        // Blooper ink lands on the racer (Effects::Player::UpdateInkEffect: SE_ITM_GESO_SUMI_ON)
    Lightning,  // the Lightning strikes (ObjThunder::ApplyToPlayers: SE_ITM_THNDR_USE, global)
    Struck,     // a racer the Lightning reaches (fn_1_288374): its shock effect restarts
    Pow,        // a POW stomp, value 1-3 (PowMgr::CreateShockwave: SE_ITM_POW_n, global)
    BlooperUse, // a Blooper is used (Item::Player::UseBlooper: SE_ITM_GESO_START on each screen)
    BlooperAttack, // the Blooper attacks the racer's screen (GessoMgr::Update: SE_ITM_GESO_ATK)
    // The Thunder Cloud object (ObjKumo) over its carrier:
    CloudOn, CloudAttach, CloudStrike, CloudOff,
    BoxBreak,   // a racer breaks an item box (ObjItemBox kart collision: SE_ITM_BOX_BRK on the box's actor)
    HitSuccess, // CharacterActor USA 808610e8: the attacker's ITM_SCES voice
};
struct ItemSound { ItemSoundCue cue; uint8_t player=0xff, actor=0xff; ItemKind kind=ItemKind::None; Vec3 position{}; bool flag=false; uint8_t value=0; };
using ItemWorldQuery=ItemSurface(*)(Vec3 previous,Vec3 position,float radius);
using ItemTimedWorldQuery=ItemSurface(*)(Vec3 previous,Vec3 position,float radius,unsigned timeOffset);
// Shared native course query: includes moving collision and item-only KCL.
ItemSurface queryCourseItemSurface(Vec3 previous,Vec3 position,float radius);
ItemSurface queryCourseItemSurfaceAt(Vec3 previous,Vec3 position,float radius,unsigned timeOffset);

// Race item manager at 60 Hz. RMCU order: item boxes move with the course objects before the
// karts; each kart resolves its box and item contacts inside its own collision pass
// (kartContacts, RMCU 0x805905e4); the holders (roulette, inventory, use) and the item objects
// update after the karts. Every effect on a kart goes through ItemKarts, which applies the
// retail action gates. See docs/POWERUPS_SOURCE.md.
struct BlooperVisual;
class RaceItems {
public:
    static constexpr unsigned MaxRacers=12, MaxActors=96;
    RaceItems();
    ~RaceItems();
    void reset(const std::vector<ItemBox>& boxes,unsigned racers,uint32_t seed=0x4d4b5749);
    /// The kart side. Host builds default to ItemKartModel; resetFromCourse installs the
    /// native Kinoko link.
    void setKarts(ItemKarts* karts);
    ItemKarts& karts() { return *karts_; }
    bool loadOdds(const void* data,size_t size,unsigned mode=0,bool battle=false,bool coin=false){
        battleOdds_=battle;coinOdds_=coin;battleScores_={};
        woodboxOdds_.load(data,size);
        const unsigned local=battle?(coin?8u:6u):2u;
        return odds_.load(data,size,local,local+1,mode);
    }
    void setBattleScores(const std::array<uint16_t,12>& scores){battleScores_=scores;}
    unsigned oddsRank(unsigned player)const{
        if(!battleOdds_)return rank(player);
        std::array<RaceTeam,12> teams;for(unsigned i=0;i<12;++i)teams[i]=racers_[i].team;
        return battleItemColumn(coinOdds_,player,count_,battleScores_,teams);
    }
    unsigned courseDropWeight(unsigned slot)const;
    bool createCourseDrop(unsigned slot,Vec3 position,Vec3 velocity,uint8_t owner,
            uint32_t randomWord,bool timeTrial=false);
    bool oddsReady()const{return odds_.ready();}
    bool loadShellRoute(const void* bytes,size_t size,unsigned course=0xffff){shellCourse_=course;return shellRoute_.load(bytes,size);}
    const ShellRoute& shellRoute()const{return shellRoute_;}
    void resetFromCourse(unsigned racers,uint32_t seed=0x4d4b5749);
    /// Course-object phase: input latch and item box motion.
    void beforeStep(const ItemRacer* racers,const ItemInput* inputs,unsigned count,bool raceActive,uint16_t bulletNoEndPlayers=0);
    /// Item-manager phase, after the karts.
    void afterStep(const ItemRacer* racers,unsigned count,ItemWorldQuery query=nullptr);
    void beforeStepNative(const ItemInput* inputs,unsigned count,bool raceActive,uint16_t bulletNoEndPlayers=0);
    void afterStepNative(unsigned count);
    /// One racer's box and item contacts against last step's item positions.
    void kartContacts(unsigned player);
    ItemInput autoItemInput(unsigned player) const;
    const ItemHud& hud(unsigned player) const { return hud_[player<MaxRacers?player:0]; }
    // Presentation reads the roulette phase for the item window cube roll.
    const ItemRoulette& roulette(unsigned player) const { return roulettes_[player<MaxRacers?player:0]; }
    const std::vector<ItemBox>& boxes() const { return boxes_; }
    const std::array<ItemActor,MaxActors>& actors() const { return actors_; }
    const std::vector<ItemEvent>& events() const { return events_; }
    // This step's sound cues (cleared with the events).
    const std::vector<ItemSound>& sounds() const { return sounds_; }
    unsigned successfulHits(unsigned player)const{return player<MaxRacers?successfulHits_[player]:0;}
    // Every Mushroom / Golden Mushroom use, for per-press boost presentation.
    uint16_t mushroomUses(unsigned player) const { return mushroomUses_[player<MaxRacers?player:0]; }
    unsigned rank(unsigned player) const;
    uint32_t frame() const { return frame_; }
    BlooperVisual blooperVisual(unsigned player,unsigned weight) const;
    unsigned blooperEndAttackFrame(unsigned player) const {return blooperEndPose_[player<MaxRacers?player:0];}
    bool invincible(unsigned player) const;
    uint16_t bulletPlayers() const;
    Vec3 heldItemPosition(unsigned player,unsigned slot) const;
    HeldItemVisual heldItemVisual(unsigned player,unsigned slot) const;
    float heldItemContactRadius(unsigned player,unsigned slot) const;
    // Used by fixtures and deliberate debug controls; never grants items at reset.
    bool give(unsigned player,ItemKind item);
    // The roulette an item box contact starts (also driven by QA autoplay).
    bool startRoulette(unsigned player);
    /// Live objects and inventory copies of a retail object type (ItemHolderItem_getCount).
    unsigned objectCount(unsigned type) const;
    /// The prize the running roulette will give (decided at the box, RMCU 0x807ab550).
    ItemKind rouletteChoice(unsigned player) const { return player<MaxRacers?rouletteChoice_[player]:ItemKind::None; }
    /// A racer-contact bump (RMCU fn_1_609F4): the attacker's Thunder Cloud may pass to the
    /// victim, whose state at the bump is `target`.
    void cloudBump(unsigned attacker,unsigned victim,const ItemRacer& target);
    static bool segmentHits(Vec3 from,Vec3 to,Vec3 center,float radius);
private:
    ItemOdds odds_;
    WoodboxOdds woodboxOdds_;
    ShellRoute shellRoute_;
    unsigned shellCourse_=0xffff;
    std::unique_ptr<ItemKarts> ownKarts_;
    ItemKarts* karts_=nullptr;
    std::vector<ItemBox> boxes_;
    std::vector<ItemBoxMover> movers_;
    std::vector<ItemBoxLine> lines_;
    std::vector<ItemBoxPress> presses_;
    struct RoadBox {const Kinoko::Field::ObjectDrivable* road=nullptr;Vec3 local{};uint16_t box=0;float lift=180;};
    std::vector<RoadBox> roadBoxes_;
    void stepMovers();
    void resolveCourseObjects(ItemActor& actor);
    void resolveHeldCourseObjects(unsigned player);
    std::array<ItemActor,MaxActors> actors_{};
    std::array<GroundReclaimOrder<MaxActors>,15> reclaimOrder_{};
    int reclaimPressure_=0;
    unsigned reclaimPhase_=0;
    bool nativeReclaimPhase_=false;
    void forgetReclaimSlot(unsigned slot);
    void syncReclaimOrder();
    std::array<bool,MaxActors> freeAtStepStart_{};
    std::array<ItemHud,MaxRacers> hud_{};
    std::array<ItemKind,MaxRacers> rouletteChoice_{};
    std::array<ItemRoulette,MaxRacers> roulettes_{};
    std::array<uint8_t,MaxRacers> rouletteRank_{};
    uint32_t rouletteRng_=1;
    std::array<uint16_t,4> itemSpawnTimers_{}; // Lightning, Blue, Blooper, POW
    std::array<ItemRacer,MaxRacers> racers_{};
    // Racers whose item point is the nearest route point to itemPointFrom_,
    // resolved on first use: only shell launches and guidance read it.
    std::array<Vec3,MaxRacers> itemPointFrom_{};
    uint16_t itemPointPending_=0;
    std::array<ItemInput,MaxRacers> inputs_{};
    std::array<bool,MaxRacers> held_{};
    // Thunder Cloud object state, kept with its carrier: transfer cooldown (+0x1d4) and age
    // (+0x160); cloudSlot_ is the user's 60 frames of the cloud icon in the item slot.
    std::array<uint16_t,MaxRacers> cloudTransfer_{},cloudAge_{},cloudSlot_{};
    std::array<bool,MaxRacers> itemLock_{};
    std::array<uint8_t,MaxRacers> heldDropTicks_{};
    struct InventoryDrop { ItemKind kind=ItemKind::None;uint8_t count=0,ticks=0;std::array<Vec3,3> velocity{}; };
    std::array<InventoryDrop,MaxRacers> inventoryDrops_{};
    std::array<uint32_t,MaxRacers> inkReadyFrame_{};
    std::array<uint32_t,MaxRacers> blooperAttack_{},blooperEnd_{};
    std::array<unsigned,MaxRacers> blooperEndPose_{};
    unsigned blooperCaster_=MaxRacers;
    uint32_t blooperSelectionFrame_=0,blooperStartFrame_=0,blooperBusyUntil_=0;
    // RMCU PowMgr (0x807d9808 deploy, 0x807d9d4c update): one POW at a time.
    int16_t powTimer_=0;uint8_t powUser_=MaxRacers;std::array<bool,MaxRacers> powTargets_{};
    struct BulletState { uint16_t elapsed=0,ending=0;uint8_t targetRank=1,wallTicks=0,wallMemory=0; };
    std::array<BulletState,MaxRacers> bullets_{};
    uint16_t bulletNoEnd_=0;
    struct HeldState {
        uint16_t angle=0,angularSpeed=600,spin=0,age=0;
        float radiusX=0,radiusZ=0,hop=0;
        std::array<float,3> scales{HeldInitialScale,HeldInitialScale,HeldInitialScale};
        std::array<Vec3,3> positions{},previous{},ups{},forwards{};
        TrailingItems trailing{};
    };
    std::array<HeldState,MaxRacers> deployments_{};
    std::array<uint16_t,MaxRacers> mushroomUses_{};
    std::vector<ItemEvent> events_;
    // This step's sounds, then any emitted between steps (give() from QA or tests), which
    // the next step keeps: stepSounds_ counts the last finished step's.
    std::vector<ItemSound> sounds_;
    size_t stepSounds_=0;
    std::array<unsigned,MaxRacers> successfulHits_{};
    void dropStepSounds(){sounds_.erase(sounds_.begin(),sounds_.begin()+std::min(stepSounds_,sounds_.size()));stepSounds_=0;}
    void sound(ItemSoundCue cue,const ItemActor& a,bool flag=false){
        sounds_.push_back({cue,uint8_t(a.owner<0?0xff:a.owner),uint8_t(&a-actors_.data()),a.kind,a.position,flag});
    }
    void sound(ItemSoundCue cue,unsigned player,ItemKind kind,Vec3 position,bool flag=false,uint8_t value=0){
        sounds_.push_back({cue,uint8_t(player),0xff,kind,position,flag,value});
    }
    void sound(ItemSoundCue cue,unsigned player,uint8_t value=0){
        sounds_.push_back({cue,uint8_t(player),0xff,ItemKind::None,player<MaxRacers?racers_[player].position:Vec3{},false,value});
    }
    bool battleOdds_=false,coinOdds_=false;
    std::array<uint16_t,12> battleScores_{};
    unsigned count_=0;
    uint32_t rng_=1,frame_=0;
    bool active_=false,contactsDone_=false,stepOpen_=false;
    uint32_t random();
    ItemKind choose(unsigned player);
    ItemKind nextRouletteIcon(unsigned player,ItemKind previous);
    void updateHolder(unsigned player);
    void use(unsigned player,bool backwards);
    void useLightning(unsigned player);
    void updatePow();
    void updateCloud(unsigned carrier);
    void powStomp(unsigned stomp);
    bool strike(unsigned player,int action,unsigned owner,Vec3 direction,bool force=false,ItemKind item=ItemKind::None);
    bool shrink(unsigned player,uint16_t frames,bool spinShrink,bool cloud);
    void applyLoss(unsigned player,const ItemKartLoss& loss);
    void takeKartLosses();
    void syncKart(unsigned player);
    void scatterHeld(unsigned player,unsigned slot);
    void destroyHeld(unsigned player,unsigned slot);
    void updateScattered(ItemActor& actor,ItemWorldQuery query);
    void updateActors(ItemWorldQuery query);
    void updateActor(ItemActor& actor,ItemWorldQuery query);
    void explode(ItemActor& actor);
    void launchShell(ItemActor& actor,unsigned player);
    void updateShell(ItemActor& actor,ItemWorldQuery query);
    void shatter(ItemActor& actor);
    void moveShellRoute(ItemActor& actor,ItemWorldQuery query,int target,bool blue);
    void shellTerrain(ItemActor& actor,ItemWorldQuery query,bool blue,bool route,bool approach=false);
    int redRouteTarget(unsigned player)const;
    int redTarget(const ItemActor& actor,bool ranked)const;
    int blueTarget(const ItemActor& actor)const;
    void trackShellRacers();
    uint16_t itemPoint(unsigned racer);
    ItemActor& spawn(unsigned player,ItemKind kind,bool backwards,const Vec3* origin=nullptr);
    void releaseHeld(unsigned player,bool backwards);
    void removeHeld(unsigned player,unsigned slot,bool breakChain);
    int targetAhead(unsigned player,bool leader=false) const;
    bool itemTouchesKart(const ItemActor& actor,unsigned player) const;
    void contactSounds(ItemKind kind,unsigned player);
    void actorKartContact(ItemActor& actor,unsigned player);
    void heldKartContacts(unsigned player);
    void boxContacts(unsigned player);
    void resolveActorPair(ItemActor& a,ItemActor& b);
    void resolveActorHeld(ItemActor& actor,unsigned owner,unsigned slot);
    void updateHeldItems(unsigned player,ItemWorldQuery query);
    void snapshotNative(unsigned count,std::array<ItemRacer,MaxRacers>& out) const;
    void unhookNative();
    static void nativeKartPass(void* context,size_t player);
    static void nativeCoursePickup(void* context,Vec3 position,Vec3 velocity,uint8_t owner);
    static void nativeKartBump(void* context,size_t attacker,size_t victim);
};
}
