#pragma once
#include <mkw/math.hpp>
#include <mkw/cpu_speed.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mkw {
class EnemyRoute;
// Retail Enemy::AI race behaviour for CPU racers: AIPathHandler/AIPathPoint
// route following, AIControlRace drive info and kart controller (body
// rotation steering, drift and mini-turbo control), AIProbabilityRace,
// AISpeedRace and AIRankManager. Ported from the USA StaticR decompilation
// with doldecomp/mkw class layouts; addresses and constants in docs/CPU_AI.md.
// trick: KPad trick input (0 none, 1 up, 2 down, 3 left, 4 right).
// stick: the KPad stick (AIInfo +0x10), neutral in normal driving: the AI turns the body.
// animationStick: Enemy::AI +0x14, which AI::getStick (0x80726204) hands the driver model
// (fn_1_2BBDC0 / KartModel fn_1_2BCF78) for a CPU instead of its pad. lean: KartMove +0x28C,
// the bike lean input KartMoveBike reads for a CPU (0x80587E58, 0x805871C0).
struct CpuCommand { uint16_t buttons=0; float stick=0; uint8_t trick=0; float animationStick=0,lean=0; };
// Item holder view for AIItemRace (ItemKind values, 20 = none) and its output:
// the item button for this frame plus the stick-up/down throw direction.
struct CpuItemView { uint8_t item=20,heldCount=0; bool roulette=false,held=false,heldRed=false,inked=false; };
struct CpuItemCommand { bool use=false,forwards=false,backwards=false; };
// kartAIActionParam.bin: per race position, percentages.
struct CpuActionParam { uint8_t startBoost=0,startFail=0,drift=0,miniturbo=0,trick=0,wheelie=0,unused=0,blockLine=0; };
bool readCpuActionParam(const void* data,size_t size,unsigned cpuMode,std::array<CpuActionParam,12>& out);

class CpuAi {
public:
    static constexpr unsigned Capacity=12;
    struct Setup {
        const EnemyRoute* route=nullptr;
        unsigned count=1,engineClass=2,cpuMode=1;
        // Time Trials: a human racer the QA autopilot drives steers by input
        // only, so the run replays as a ghost (the body yaw below is not input).
        bool inputOnlyHumans=false;
        CpuSpeedParam speed{};bool speedReady=false;
        std::array<CpuActionParam,12> actions{};bool actionsReady=false;
        // Rank distances: raceCompletion times the mean checkpoint lap length
        // (CheckpointHolder_computeMeanTotalDistance 0x80512370, fn_1_22F088).
        float courseLength=6000;
        uint32_t humanMask=1;
        uint32_t seed=0x2a;
        // fn_1_22015C: STGI pole position (1 flips the start lanes).
        unsigned polePosition=0;
        // fn_1_22C244 / fn_1_26474: races start the route at ENPT point 0.
        bool startAtPointZero=false;
        // AIRankManager::init (fn_1_2311B4): the race number in the Grand Prix / VS set
        // (RaceConfig +0xB8C, 0 = first) and every racer's points before this race (+0xD8).
        unsigned raceNumber=0;
        std::array<uint16_t,Capacity> points{};
    };
    // After the race scene is built (karts at their grid slots).
    void reset(const Setup& setup);
    // Once per 60 Hz tick before physics. driveMask: racers this AI controls
    // (CPUs, plus player zero for QA route driving). ranks: 1-based positions.
    void step(uint32_t driveMask,const std::array<unsigned,Capacity>& ranks,const std::array<unsigned,Capacity>& laps,
              const std::array<bool,Capacity>& finished,unsigned finalLap);
    const CpuCommand& command(unsigned i) const { return racers_[i<Capacity?i:0].command; }
    // AI::handleBump (retail 80721f9c): kart contact shifts the lateral offset.
    void bump(unsigned racer,Vec3 direction);
    // AIItemRace inputs (call before step) and output (consumed next frame).
    void setItemView(unsigned racer,const CpuItemView& view){ if(racer<Capacity)racers_[racer].itemView=view; }
    void setRecovering(unsigned racer,bool recovering){ if(racer<Capacity)racers_[racer].recovering=recovering; }
    const CpuItemCommand& itemCommand(unsigned racer) const { return racers_[racer<Capacity?racer:0].itemCommand; }
    bool mushroomRoute(unsigned racer) const { return racer<Capacity&&racers_[racer].mushroomRoute; }
    // Landed item hazards (bananas, fake boxes, Bob-ombs) for the retail
    // obstacle avoider; set once per tick before step.
    static constexpr unsigned MaxHazards=32;
    void setHazards(const Vec3* positions,const float* radii,unsigned count){
        hazardCount_=std::min(count,MaxHazards);
        for(unsigned k=0;k<hazardCount_;++k){hazards_[k]=positions[k];hazardRadii_[k]=radii[k];}
    }
    // AIPowAvoider (0x8072A7A0) reads the POW manager's timer (0: no POW); set once per tick before step.
    void setPowTimer(uint16_t timer){ powTimer_=timer; }
    unsigned routePoint(unsigned racer) const { return racer<Capacity?racers_[racer].path.cur:0; }
    Vec3 target(unsigned racer) const { return racer<Capacity?racers_[racer].path.target:Vec3{}; }
    float baseSpeed(unsigned racer) const { return racer<Capacity?racers_[racer].speed.result:0; }
    // QA: stuck handler / avoider / item AI states.
    int stuckState(unsigned racer) const { return racer<Capacity?racers_[racer].stuck.state:0; }
    int avoidState(unsigned racer) const { return racer<Capacity?racers_[racer].avoid.state:0; }
    int itemState(unsigned racer) const { return racer<Capacity?int(racers_[racer].item.state):0; }
    int targetRank(unsigned racer) const { return racer<Capacity?racers_[racer].rank.target:0; }
    bool ready() const { return route_!=nullptr; }
    // Retail AIProbabilityRace::getStartBoost (fn_1_22EAD8): 0 none, -1 burnout, 1..5 levels.
    int startBoostLevel(unsigned racer) const { return racer<Capacity?racers_[racer].startBoost:0; }
private:
    struct PointParam {
        int setting1=0,setting2=0;uint8_t flags=0;float curvature=0;
        void clear(){setting1=setting2=0;flags=0;curvature=0;}
        bool endDrift() const{return setting2==1;}
        bool forbidDrift() const{return setting2==2;}
        bool forceDrift() const{return setting2==3;}
    };
    struct Path {
        uint16_t start=0,next=0,cur=0,prev=0,log2=0,log3=0;
        int branch=0;
        float maxOffset=0,offset=0,curvature=0;
        Vec3 target{};
        bool active=false,switching=false,mushroomRoute=false;
        int passMode=2,passModeReset=2;
        float goNext=1000,goNextDefault=1000,inside=0,distance=0;
        PointParam currParam,nextParam;
    };
    struct Drive {
        int state=0;            // 0 normal, 1 drifting
        float driftDir=0,timer=0,ramp=0,failFrames=0;
        bool switched=false;
        float feedForward=0;    // DriveInfo+0x1c, persists across ticks
        float lastAngle=0;      // controller +0xc
        float lean=0;           // KartMove +0x28C: written in the normal state only
    };
    // AIControlBase +0x48 obstacle avoider (ctor fn_1_2161C0, vtable 808c688c).
    struct Avoid {
        int state=0,next=-1;    // 0 normal, 1 avoid, 2 late avoid, 4 ink wobble
        int timer=0,phase=0;    // +0x14c, +0x150
        bool active=false;      // +0x158 -> DriveInfo +0x28
        float steer=0;          // +0x15c -> DriveInfo +0x20
        Vec3 start{},end{},obstacle{};
        float steerWeight=1,chanceWeight=1;int result=0; // probe +0x24, +0x28, +0x2c
        unsigned seek=0;        // +0x154 round-robin racer for Mega/cloud seeking
    };
    // AIControlRace +0x94 block line (ctor fn_1_218F9C, vtable 808c5200), karts only.
    struct Block { int state=0,next=0,timer=0,target=-1; float gainScale=1; };
    // AIControlBase +0x40 stuck handler (ctor fn_1_23437C, vtable 808c6170).
    struct Stuck {
        int state=0,next=-1;    // 0 detect, 1 recover, 2 respawn
        Vec3 last{},sample{};   // +0xc8, +0xd4
        int counter=0,wallSamples=0,noProgress=0,sampleTimer=0; // +0xe4, +0xe8, +0xec, +0xf0
        int phase=0,timer=0;    // +0xf4, +0xf8
        float rate=0;           // AIPathPoint +0x2c: recovery target slides back along the route
        float gainScale=1;      // +0xe0: scales the normal steering gain
        int wallTimer=0;        // helper +0x8: frames since a wall contact (10-frame hold)
        bool primed=false;
    };
    struct Speed {
        float slowdown=0,opening=1,openingBonus=0,catchup=10;
        float frameBonus=0,advantage=0,result=70;
    };
    struct Rank {
        int position=0,compact=0,target=-1,group=-1,groupIndex=0;
        float offset=0,distance=0,gap=0;
    };
    enum class ItemState : uint8_t { Idle, Stock, Throw, Mushroom, ThrowFront, ThrowBack, Hold, Goal, None };
    struct ItemAi {
        ItemState state=ItemState::Idle,next=ItemState::None;
        int counter=0,dir=0,delay=0; // +0x19c press sequence, +0x1a4 direction, +0x1a8 delay
        bool useItem=false,goal=false;
    };
    struct Racer {
        Path path;Drive drive;Speed speed;Rank rank;CpuCommand command;
        ItemAi item;CpuItemView itemView;CpuItemCommand itemCommand;Avoid avoid;Stuck stuck;Block block;
        bool active=false,human=false,mushroomRoute=false,respawning=false,recovering=false,wheelie=false;
        int startBoost=0;bool startDecided=false;
        bool driftRolled=false,canMiniturbo=true;
    };
    struct Group { int start=1;std::array<int,6> members{};int count=0;int phase=0,timer=0; };

    uint32_t rand(uint32_t limit);
    float randF(float limit);
    const CpuActionParam& action(const Racer& r) const;
    // AIPathPoint / AIPathHandler.
    Vec3 pointPos(unsigned index) const;
    float widthOffset(unsigned index) const;
    unsigned chooseBranch(unsigned racer,unsigned from,bool allowMushroom=true);
    void computeCurvature(Path& p) const;
    void recalcTarget(Path& p,float inside) const;
    float slotOffset(unsigned racer) const;
    void jitterOffset(unsigned racer,bool spacing);
    void setPoint(unsigned racer,float offset);
    void advance(unsigned racer,float inside);
    bool shouldGoNext(Path& p,Vec3 kartPos) const;
    void goNext(unsigned racer,Vec3 kartPos);
    void updatePath(unsigned racer,Vec3 kartPos);
    // Respawn: the path restarts at the respawn point's ENPT link (fn_1_22B79C).
    unsigned respawnLink(unsigned racer,Vec3 kartPos) const;
    void onOutOfBounds(unsigned racer,unsigned point);
    bool driftHere(const Path& p) const;
    bool endDriftHere(const Path& p) const;
    // AIControlRace / kart controller.
    void drive(unsigned racer);
    // AISpeedRace / AIRankManager.
    void updateRanks(const std::array<unsigned,Capacity>& ranks,const std::array<unsigned,Capacity>& laps,
                     const std::array<bool,Capacity>& finished,unsigned finalLap);
    void updateSpeed(unsigned racer,unsigned lap);
    void assignGroupTargets();
    void redistribute();
    void swapTargets(Group& g);
    void swapWithOther(Group& g,int member);
    void updateTopGroup(unsigned finalLap,const std::array<unsigned,Capacity>& laps);
    void chase(float t);
    // Block line.
    void blockUpdate(unsigned racer);
    int findBlockTarget(unsigned racer) const;
    bool behindWithin(unsigned racer,unsigned other,float range,float cosine) const;
    // Stuck handler.
    void stuckUpdate(unsigned racer);
    bool stuckWallCheck(unsigned racer);
    Vec3 recoveryTarget(const Racer& r) const;
    // Obstacle avoider.
    void avoid(unsigned racer);
    void enterAvoidState(unsigned racer);
    bool findObstacle(unsigned racer);
    // AIItemRace.
    void stepItems(unsigned racer);
    void enterItemState(unsigned racer);
    void calcItemState(unsigned racer);
    bool shouldHoldItem(unsigned racer);
    int throwDirection(unsigned racer,bool held);
    int humanAdjacent(unsigned racer);

    const EnemyRoute* route_=nullptr;
    // Per JGPT: its ENPT link (MapdataJugemPoint +0x28, fn_1_8148 0x805184FC).
    std::vector<uint8_t> respawnLinks_;
    Setup setup_{};
    bool driveFinished_=false; // the racer drive() steers has finished
    std::array<Racer,Capacity> racers_{};
    std::array<Group,3> groups_{}; // top, middle, bottom
    int topType=2;
    int rmState=0,rmFrame=0,rmDuration=0,rmPhase=0,rmInterval=-1;
    bool advantageEnabled=false;
    int advantageState=0;float advantageValue=0;
    std::array<int,Capacity> sortedCpus{};int cpuCount=0;
    std::array<int,Capacity> sortedHumans{};int humanCount=0;
    uint32_t rng_=1;
    uint32_t frame_=0;
    std::array<Vec3,MaxHazards> hazards_{};std::array<float,MaxHazards> hazardRadii_{};unsigned hazardCount_=0;
    uint16_t powTimer_=0;
    bool raceStarted_=false;
    // Course objects' positions and probe reach for this step's findObstacle
    // calls (objects only move in the Kinoko step that follows).
    struct ObstacleProbe {const void* object;Vec3 position;float reach;};
    std::vector<ObstacleProbe> obstacles_;bool obstaclesValid_=false;
};
}
