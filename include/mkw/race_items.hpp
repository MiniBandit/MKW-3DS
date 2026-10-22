#pragma once
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

namespace Kinoko::Field {class RailInterpolator;class ObjectItemboxPress;class ObjectBase;}
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
    // The Bullet's end-phase timer (KartKiller +0x12, PAL 0x8059CC60): 1 on the first step
    // after the end starts, 0 otherwise. Its fly sound pitch follows it (0x8070C758).
    uint16_t bulletEnd=0;
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
// An s_itembox that landed on a drivable object it rides (retail RMCP calc 8082945c): its
// position is the object's position plus this offset.
struct ItemBoxRide { const Kinoko::Field::ObjectBase* object=nullptr; uint16_t box=0; Vec3 offset{}; };
struct ItemActor {
    ItemKind kind=ItemKind::None;
    Vec3 position{}, previous{}, velocity{},flightDirection{0,0,1};
    // The object's model up axis (retail matrix column 1): the thrower's up, then the floor's.
    Vec3 up{0,1,0};
    float yaw=0, scale=1, radius=45,terrainRadius=40,travelSpeed=0;
    uint16_t age=0, lifetime=0;
    int8_t owner=-1, target=-1;
    uint8_t bounces=0;
    uint8_t bounceLimit=10;
    bool active=false, explosion=false,backwards=false;
    bool settled=false, scattered=false;
    // Sound only: fn_1_2A655C (PAL 0x807B6910) ran, the object came to rest and played its land
    // sound (ObjProperties +0x68). Retail's rest state (fn_1_2A6848 0x807B6BFC) plays nothing, so
    // the sound does not repeat when `settled` lapses for a frame without floor contact.
    bool rested=false;
    // ItemObj +0x78 bit 0x2000: a course object bounced it (fn_1_291548 response 1); its next
    // scatter step (fn_1_296F9C) cannot land it (fn_1_291F98) and clears the bit.
    bool objectBounce=false;
    // Frames left in the dying state (fn_1_29352C 0x807A38E0) after fn_1_296644 (0x807A69F8)
    // destroyed it, ItemHolderItem_despawn at 0: hidden (scale 0) for 6, or 30 when it was
    // below y 0, moving on and shrinking. A dying object touches nothing (its BoxColManager
    // entry is removed, fn_1_28EB18 0x8079EECC).
    uint8_t dying=0;
    int16_t fuse=300;
    uint16_t hitMask=0;
    // RMCU ObjItem +0x78 bit 0x20000: the thrower is immune until the item is older than
    // ownerImmunity frames (30 thrown, 15 trap throw, 7 ejected by damage).
    uint8_t ownerImmunity=30;
    float blastInitialScale=1,bounceImpulse=0;
    uint8_t floorBounces=0;
    // Green / Red shell movement (fn_1_2A5140): the velocity direction +0x1b0 (flightDirection
    // is +0x1a4), and ItemObj_update's air frames +0x15c (counted to 6, cleared on a floor) with
    // the flag it leaves for the next update, +0x78 bit 0x20 (5 or more).
    Vec3 shellDirection{0,0,1};
    uint8_t airFrames=0;bool airborne=false;
    // +0x15e: frames left after a solid-OOB floor (ItemObj_update 0x8079EFEC), 0 when off.
    uint16_t solidFallTimer=0;
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
enum class ItemEffect : uint8_t { Mushroom, Spin, Launch, Explosion, Shrink, Crush, Cure, Knockback, Pow, Ink, Break, PowStomp, LightningFlash, Bolt };
// Object events (Break, and Explosion with action -1) carry the item kind in `frames`, its
// position in `direction` and its model scale. PowStomp carries the stomp in `frames` and
// action 1 for a target, 0 for the user. Bolt (KartEffect +0x4c / +0x50 reset) has frames 0
// for a Lightning bolt and 1 for the Thunder Cloud's strike.
struct ItemEvent { ItemEffect effect; uint8_t player; uint16_t frames=0; Vec3 direction{}; int8_t action=-1; float scale=1; uint8_t source=0xff; };
// types: the KCL type bits the sphere touched (ColInfo +0xd4 in the retail scatter state).
struct ItemSurface { Vec3 correction{}, floorNormal{}, wallNormal{}; bool kill=false; bool cannon=false; uint32_t types=0;
    // The rest of the ColInfo (Kinoko CollisionInfo; retail item +0xd8): the push box, the
    // floor and wall distances (-FLT_MIN when none) and the wall perpendicularity.
    Vec3 low{},high{}; float floorDist=-1.17549435e-38f,wallDist=-1.17549435e-38f,perpendicularity=0; };
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
    HitSuccess, // the player's item put another racer into an action (PlayerSub18_checkItemCollision
                // 0x80572AD0 -> CharacterActor fn_1_355164 0x80865518: voice 3, ITM_SCES)
};
struct ItemSound { ItemSoundCue cue; uint8_t player=0xff, actor=0xff; ItemKind kind=ItemKind::None; Vec3 position{}; bool flag=false; uint8_t value=0; };
// The KCL types an item object's course query accepts. 0x6CEBDFFF for every object
// (CourseModel::checkSphereFullPush 0x8078F784 from the scatter state, 0x807A73B8);
// 0x6CEBDBFF, without the solid OOB, for the Blue Shell: its own states pass it (dive
// 0x807AE0D4, checkSphereFull 0x807B4348) and the shared shell movers pick it by +0x1CC
// (0x807B4ED4, 0x807B555C), which only the Blue's 0x807AC634 / 0x807AE6EC set.
inline constexpr uint32_t ItemKclMask=0x6CEBDFFF,BlueShellKclMask=0x6CEBDBFF;
// Held (ring and trail) items test floors only and are pushed, never destroyed, by them
// (calcRotatingItemObjs 0x807941F0, fn_1_282264 0x80792904, fn_1_2831B8 0x80793BF8).
inline constexpr uint32_t HeldItemKclMask=0x20E80FFF;
using ItemWorldQuery=ItemSurface(*)(Vec3 previous,Vec3 position,float radius,uint32_t mask);

// Race item manager at 60 Hz. RMCU order: item boxes move with the course objects before the
// karts; each kart resolves its box and item contacts inside its own collision pass
// (kartContacts, RMCU 0x805905e4); the holders (roulette, inventory, use) and the item objects
// update after the karts. Every effect on a kart goes through ItemKarts, which applies the
// retail action gates. See docs/POWERUPS_SOURCE.md.
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
    /// ItemSlot.bin tables (PAL fn_1_2AAB94 after RaceGlobals_init): a Grand Prix reads
    /// tables 0 (players) and 1 (CPUs), unscaled; VS reads 2 and 3. Set before resetFromCourse.
    void setGrandPrix(bool grandPrix){grandPrix_=grandPrix;}
    /// The item manager's Time Trial flag (PAL 0x809C38BC): RaceGlobals_init 0x807BD1D0 sets
    /// it from RaceConfig +0xB70 for Time Trial and Ghost Race. Set before resetFromCourse.
    void setTimeTrial(bool timeTrial){timeTrial_=timeTrial;}
    bool loadOdds(const void* data,size_t size){return grandPrix_?odds_.load(data,size,0,1):odds_.load(data,size,2,3);}
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
    // Every Mushroom / Golden Mushroom use, for per-press boost presentation.
    uint16_t mushroomUses(unsigned player) const { return mushroomUses_[player<MaxRacers?player:0]; }
    unsigned rank(unsigned player) const;
    uint32_t frame() const { return frame_; }
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
    bool grandPrix_=false,timeTrial_=false;
    ShellRoute shellRoute_;
    unsigned shellCourse_=0xffff;
    std::unique_ptr<ItemKarts> ownKarts_;
    ItemKarts* karts_=nullptr;
    std::vector<ItemBox> boxes_;
    std::vector<ItemBoxMover> movers_;
    std::vector<ItemBoxLine> lines_;
    std::vector<ItemBoxPress> presses_;
    std::vector<ItemBoxRide> rides_;
    void stepMovers();
    std::array<ItemActor,MaxActors> actors_{};
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
        Vec3 up{0,1,0};float lift=0; // heldFrame(): holder +0x64, +0xcc
    };
    std::array<HeldState,MaxRacers> deployments_{};
    // Item::PlayerObj as its frame rebuild fn_1_281AA8 (0x80791E5C) leaves it, kept across
    // deployments: the anchor +0x7c, its change since the last rebuild +0x9c, the axes Y +0x64
    // and Z +0x70, and +0xac (moving along Z, or in an action or trick); X +0x58 and the speed
    // ratio +0x98 for the forward throw's sideways part.
    struct HeldMotion { Vec3 anchor{},delta{},right{1,0,0},up{0,1,0},forward{0,0,1};float ratio=0;bool ahead=false; };
    std::array<HeldMotion,MaxRacers> heldMotion_{};
    // ItemHolderPlayer +0x40 (ItemHolderPlayer_update 0x80797928): the eased slope a throw's
    // direction takes as its vertical part, with the holder's last kart position +0x24, and
    // +0x23c, the stick X it follows at 1/15 per step (a forward throw's sideways aim).
    struct ThrowPitch { Vec3 last{};float pitch=0,slope=0,aim=0;bool placed=false; };
    std::array<ThrowPitch,MaxRacers> throwPitch_{};
    // Holder +0xc0 / +0xc4 / +0xc8 (fn_1_285210 0x807955C4): the last wheel's kart-frame y and
    // z and its height above the kart on race frame 60, for heldFrame()'s wheelie lift.
    std::array<Vec3,MaxRacers> wheelieReference_{};
    std::array<uint16_t,MaxRacers> mushroomUses_{};
    // ItemHolderPlayer +0x238 (PAL): set by activateMushroom (0x8079864C), cleared at the
    // top of every holder update (0x807979D8 / 0x807979E8); a Blue Shell dodges on it.
    std::array<bool,MaxRacers> mushroomPulse_{};
    // RaceConfig +0xB6C engine class (0 50cc, 1 100cc, 2 150cc): the Blue's dodge floor.
    uint8_t engineClass_=2;
    std::vector<ItemEvent> events_;
    // This step's sounds, then any emitted between steps (give() from QA or tests), which
    // the next step keeps: stepSounds_ counts the last finished step's.
    std::vector<ItemSound> sounds_;
    size_t stepSounds_=0;
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
    bool strike(unsigned player,int action,unsigned owner,Vec3 direction,bool force=false);
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
    void moveShell(ItemActor& actor,ItemWorldQuery query,Vec3& up,float target);
    Vec3 backwardThrow(unsigned player,float& speed)const;
    Vec3 forwardThrow(unsigned player,float base,float& speed)const;
    Vec3 finishThrow(unsigned player,Vec3 dir)const;
    void noteHeldFrame(unsigned player,Vec3 anchor,Vec3 right,Vec3 up,Vec3 forward);
    void updateThrowPitch(unsigned player);
    int redRouteTarget(unsigned player)const;
    int redTarget(const ItemActor& actor,bool ranked)const;
    int blueTarget(const ItemActor& actor)const;
    void trackShellRacers();
    uint16_t itemPoint(unsigned racer);
    ItemActor& spawn(unsigned player,ItemKind kind,bool backwards,const Vec3* origin=nullptr);
public:
    // A course object's loose Mushroom (a squashed Goomba, Kinoko ObjectKuribo):
    // launched from the object; owner 12 is nobody.
    void courseMushroom(Vec3 position,Vec3 velocity,unsigned owner);
private:
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
    // Item vs course object (ItemObj_update 0x8079EFEC -> fn_1_291B24 0x807A1ED8).
    void objectContacts(ItemActor& actor);
    void heldObjectContacts(unsigned type);
    void updateHeldItems(unsigned player,ItemWorldQuery query);
    void snapshotNative(unsigned count,std::array<ItemRacer,MaxRacers>& out) const;
    void unhookNative();
    static void nativeKartPass(void* context,size_t player);
    static void nativeKartBump(void* context,size_t attacker,size_t victim);
};
}
