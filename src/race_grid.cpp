#include <mkw/race_grid.hpp>
#include <mkw/bullet_bill.hpp>
#include <mkw/garage.hpp>
#include <game/system/RaceConfig.hh>
#include <game/system/RaceManager.hh>
#include <game/system/KPadDirector.hh>
#include <game/system/ResourceManager.hh>
#include <game/system/CourseMap.hh>
#include <game/system/map/MapdataStageInfo.hh>
#include <egg/math/Vector.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartCollide.hh>
#include <game/kart/KartPhysics.hh>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace mkw {
namespace {
unsigned be16(const uint8_t* b){return unsigned(b[0])<<8|b[1];}
uint32_t be32(const uint8_t* b){return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];}
float beFloat(const uint8_t* b){uint32_t v=be32(b);float f;std::memcpy(&f,&v,4);return f;}
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Vec3 native(const Kinoko::EGG::Vector3f& p){return {p.x,p.y,p.z};}
float horizontalProjection(Vec3 p,Vec3 a,Vec3 b){p.y=a.y=b.y=0;const Vec3 d=b-a;const float n=dot(d,d);return n>0.001f?dot(p-a,d)/n:1.f;}
float projection(Vec3 p,Vec3 a,Vec3 b){const Vec3 d=b-a;const float n=dot(d,d);return n>0.001f?dot(p-a,d)/n:1.f;}
// CheckpointHolder_computeMeanTotalDistance (0x80512370; recursion fn_1_1CB0, 0x80512064):
// from the finish-line checkpoint, the mean over its next links of the link length (midpoint
// to midpoint, MapdataCheckPoint::initCheckpointLinks) plus, unless the link returns to the
// finish line, the same mean from the linked checkpoint. Memoised (each checkpoint's mean is
// fixed), iterative; 0 without checkpoints.
float meanCheckpointDistance(const void* bytes,size_t size){
    using Kinoko::EGG::Vector2f;
    if(!bytes||size<16)return 0;
    const auto* b=static_cast<const uint8_t*>(bytes);
    if(std::memcmp(b,"RKMD",4)||be32(b+4)>size||be32(b+4)<16)return 0;
    size=be32(b+4);
    const unsigned sections=be16(b+8),header=be16(b+10);
    if(sections>64||header<16+sections*4||header>size)return 0;
    size_t ckpt=0,ckph=0;
    for(unsigned i=0;i<sections;++i){const size_t off=header+size_t(be32(b+16+4*i));
        if(off>size||size-off<8)return 0;
        if(!std::memcmp(b+off,"CKPT",4))ckpt=off;
        if(!std::memcmp(b+off,"CKPH",4))ckph=off;
    }
    if(!ckpt||!ckph)return 0;
    const unsigned count=be16(b+ckpt+4),paths=be16(b+ckph+4);
    if(!count||count>255||ckpt+8+size_t(count)*20>size||ckph+8+size_t(paths)*16>size)return 0;
    std::vector<Vector2f> mid(count);std::vector<std::array<uint8_t,6>> links(count);std::vector<uint8_t> linkCount(count,0);
    int finish=-1;
    for(unsigned i=0;i<count;++i){const auto* p=b+ckpt+8+i*20;
        mid[i]=0.5f*(Vector2f(beFloat(p),beFloat(p+4))+Vector2f(beFloat(p+8),beFloat(p+12)));
        if(int8_t(p[17])==0)finish=int(i); // check area 0: the finish line (the last one)
    }
    if(finish<0)return 0;
    for(unsigned i=0;i<count;++i){const unsigned next=b[ckpt+8+i*20+19];
        if(next!=0xff){if(next>=count)return 0;links[i][linkCount[i]++]=uint8_t(next);continue;}
        // The last checkpoint of its CKPH group links to the first of each next group.
        for(unsigned g=0;g<paths;++g){const auto* h=b+ckph+8+g*16;if(i<h[0]||i>=unsigned(h[0])+h[1])continue;
            for(unsigned k=0;k<6;++k){const unsigned to=h[8+k];if(to==0xff)continue;if(to>=paths||b[ckph+8+to*16]>=count)return 0;links[i][linkCount[i]++]=b[ckph+8+to*16];}
            break;
        }
    }
    std::vector<float> value(count,0.f);std::vector<uint8_t> state(count,0); // 0 new, 1 open, 2 done
    std::vector<std::pair<unsigned,unsigned>> stack;stack.reserve(count);
    stack.push_back({unsigned(finish),0u});state[unsigned(finish)]=1;
    while(!stack.empty()){
        const unsigned id=stack.back().first,k=stack.back().second;
        if(k<linkCount[id]){++stack.back().second;const unsigned n=links[id][k];
            if(n!=unsigned(finish)&&state[n]==0){state[n]=1;stack.push_back({n,0u});}
            continue;
        }
        float sum=0;
        for(unsigned j=0;j<linkCount[id];++j){const unsigned n=links[id][j];
            sum=sum+(mid[n]-mid[id]).length();
            if(n!=unsigned(finish))sum=sum+value[n];
        }
        value[id]=linkCount[id]?sum/float(linkCount[id]):0.f;state[id]=2;stack.pop_back();
    }
    return value[unsigned(finish)];
}
}

bool EnemyRoute::load(const void* bytes,size_t size,bool itemPath){
    clear();if(!bytes||size<16)return false;
    const auto* b=static_cast<const uint8_t*>(bytes);
    if(std::memcmp(b,"RKMD",4)||be32(b+4)>size||be32(b+4)<16)return false;
    size=be32(b+4);
    const unsigned sections=be16(b+8),header=be16(b+10);
    if(sections>64||header<16+sections*4||header>size)return false;
    size_t enpt=0,enph=0,ptEnd=0,phEnd=0;
    for(unsigned i=0;i<sections;++i){
        const size_t off=header+size_t(be32(b+16+4*i));
        if(off>size||size-off<8)return false;
        size_t end=size;
        for(unsigned j=0;j<sections;++j){const size_t other=header+size_t(be32(b+16+4*j));if(other>off)end=std::min(end,other);}
        if(!std::memcmp(b+off,itemPath?"ITPT":"ENPT",4)){enpt=off;ptEnd=end;}
        if(!std::memcmp(b+off,itemPath?"ITPH":"ENPH",4)){enph=off;phEnd=end;}
    }
    if(!enpt||!enph)return false;
    const unsigned count=be16(b+enpt+4),groups=be16(b+enph+4);
    if(count<2||count>4096||groups==0||groups>255||enpt+8+count*20>ptEnd||enph+8+groups*16>phEnd)return false;
    std::vector<Point> points(count);std::vector<bool> covered(count,false);
    for(unsigned i=0;i<count;++i){const auto* p=b+enpt+8+i*20;auto& v=points[i];
        v.position={beFloat(p),beFloat(p+4),beFloat(p+8)};v.width=beFloat(p+12);v.setting1=be16(p+16);v.setting2=be16(p+18);v.driftSetting=p[18];v.flags=p[19];
        if(!finite(v.position)||!std::isfinite(v.width)||v.width<0)return false;
    }
    for(unsigned g=0;g<groups;++g){const auto* p=b+enph+8+g*16;unsigned start=p[0],length=p[1];
        if(length==0||start+length>count)return false;
        for(unsigned j=start;j<start+length;++j){
            if(covered[j])return false;covered[j]=true;auto& v=points[j];
            // ENPT group-entry setting 1 requires a cutting item. Our CPU
            // driver cannot guarantee that item is held/used at entry, so use
            // ordinary branches. ITPT setting 1 has different semantics.
            v.requiresBoost=!itemPath&&points[start].setting1==1;
            if(j+1<start+length){v.next[v.nextCount++]=j+1;continue;}
            for(unsigned k=0;k<6;++k){const unsigned next=p[8+k];if(next==255)continue;
                if(next>=groups)return false;const unsigned point=b[enph+8+next*16];if(point>=count)return false;
                if(std::find(v.next.begin(),v.next.begin()+v.nextCount,point)==v.next.begin()+v.nextCount)v.next[v.nextCount++]=point;
            }
            if(v.nextCount==0)return false;
        }
    }
    if(std::find(covered.begin(),covered.end(),false)!=covered.end())return false;
    points_=std::move(points);return true;
}
unsigned EnemyRoute::successor(unsigned index,unsigned racer) const{
    if(index>=size()||!points_[index].nextCount)return Invalid;
    const auto& p=points_[index];
    std::array<unsigned,6> allowed{};unsigned count=0;
    for(unsigned n=0;n<p.nextCount;++n)if(!points_[p.next[n]].requiresBoost)allowed[count++]=p.next[n];
    return count?allowed[racer%count]:p.next[racer%p.nextCount];
}
unsigned EnemyRoute::nearest(Vec3 pos,Vec3 forward,unsigned racer) const{
    unsigned result=Invalid;float best=std::numeric_limits<float>::max();
    for(unsigned i=0;i<size();++i){if(points_[i].requiresBoost)continue;unsigned n=successor(i,racer);if(n==Invalid)continue;
        const Vec3 a=points_[i].position,d=points_[n].position-a;
        const Vec3 closest=a+d*std::clamp(projection(pos,a,a+d),0.f,1.f);Vec3 error=pos-closest;
        // Height matters at Coconut Mall's overlapping floors. Prefer a route
        // facing the same direction when equally close, but allow wrong-way recovery.
        float cost=dot(error,error);if(d.x*forward.x+d.z*forward.z<0)cost+=250000.f;
        if(cost<best){best=cost;result=i;}
    }
    return result;
}
Vec3 EnemyRoute::target(unsigned segment,unsigned racer,Vec3 pos,float lookahead,bool horizontalRecovery) const{
    if(segment>=size())return pos;
    unsigned next=successor(segment,racer);Vec3 a=points_[segment].position,z=points_[next].position;
    a=a+(z-a)*std::clamp(horizontalRecovery?horizontalProjection(pos,a,z):projection(pos,a,z),0.f,1.f);
    for(unsigned step=0;step<size();++step){const Vec3 delta=z-a;const float distance=horizontalRecovery?std::hypot(delta.x,delta.z):length(delta);
        if(distance>=lookahead&&distance>0.01f)return a+delta*(lookahead/distance);
        lookahead-=distance;a=z;segment=next;next=successor(segment,racer);z=points_[next].position;
    }
    return z;
}

void RaceGrid::drawLineup(unsigned localDriver){
    // The menus choose the CPU racers once per Grand Prix / VS set (RaceConfig menu
    // scenario), with a time-seeded Util::Random. Every character and vehicle is
    // unlocked in this port (characterIsUnlocked, fn_1_3FD18 always pass).
    constexpr unsigned driverCount=GarageChoices.size()/VehiclesPerDriver;
    std::array<unsigned,driverCount> driverOf{}; // retail character id -> garage driver
    for(unsigned d=0;d<driverCount;++d)driverOf[GarageChoices[d*VehiclesPerDriver].character%driverCount]=d;
    // getCharacterWeightClass (0x8081CD3C) is the class of the driver's twelve vehicles.
    auto weight=[&](unsigned id){return GarageChoices[driverOf[id]*VehiclesPerDriver].vehicle%3;};
    uint32_t rng=session_.seed?session_.seed:1;
    auto next=[&](uint32_t limit){rng=rng*1664525u+1013904223u;return (rng>>8)%limit;};
    // fn_1_32E874 (0x8083EC28): ids 0..23 (never a Mii) re-drawn until no earlier racer
    // has it and its weight class holds fewer than (n + n%3)/3 racers, humans counted first.
    const unsigned n=count_,limit=(n+n%3)/3;
    std::array<unsigned,3> classes{};std::array<unsigned,Capacity> characters{};
    characters[0]=GarageChoices[localDriver*VehiclesPerDriver].character%driverCount;++classes[weight(characters[0])];
    for(unsigned i=1;i<n;++i){
        unsigned id=next(driverCount);
        while(classes[weight(id)]>=limit||std::find(characters.begin(),characters.begin()+i,id)!=characters.begin()+i)id=next(driverCount);
        characters[i]=id;++classes[weight(id)];
    }
    // Vehicles. Battle (fn_1_32A9A0, 0x8083AD54): the class's standard kart (0/1/2) or bike
    // (18/19/20), 50/50 when both are allowed. Races (fn_1_3370A8, 0x8084745C): a uniform
    // pick of the class's twelve (class + 3k), re-drawn until the rule allows it
    // (fn_1_30CC00: 0 karts < 18, 1 bikes 18..35, 2 both).
    const unsigned rule=session_.vehicles;
    for(unsigned i=1;i<n;++i){
        const unsigned w=weight(characters[i]),d=driverOf[characters[i]];
        unsigned vehicle;
        if(session_.mode==Session::Mode::Battle){
            const unsigned coin=next(2);
            vehicle=rule==0?w:rule==1?18+w:coin==0?w:18+w;
        }else do vehicle=w+3*next(12);while((rule==0&&vehicle>=18)||(rule==1&&vehicle<18));
        unsigned slot=0;
        for(unsigned s=0;s<VehiclesPerDriver;++s)if(GarageChoices[d*VehiclesPerDriver+s].vehicle==vehicle){slot=s;break;}
        lineup_[i]=d*VehiclesPerDriver+slot;
    }
}
void RaceGrid::configure(Kinoko::System::RaceConfig& config,unsigned localChoice,unsigned count){
    constexpr unsigned driverCount=GarageChoices.size()/VehiclesPerDriver;
    static_assert(GarageChoices.size()%VehiclesPerDriver==0&&driverCount>=Capacity);
    count_=std::clamp(count,1u,Capacity);localChoice%=GarageChoices.size();
    auto& scenario=config.raceScenario();scenario.playerCount=count_;
    const unsigned course=unsigned(scenario.course);
    battleArena_=course>=32&&course<42;
    const unsigned localDriver=localChoice/VehiclesPerDriver;
    // A new set (or another human driver) draws a new CPU line-up; restarts and the
    // later races of a Grand Prix / VS set keep it.
    if(count_>1&&(lineupPending_||lineupDriver_!=localDriver)){drawLineup(localDriver);lineupPending_=false;lineupDriver_=localDriver;}
    for(unsigned i=0;i<Capacity;++i){auto& p=scenario.players[i];
        if(i>=count_){p.type=Kinoko::System::RaceConfig::Player::Type::None;continue;}
        choices_[i]=i?lineup_[i]:localChoice;
        const auto& choice=GarageChoices[choices_[i]];
        p.character=static_cast<Kinoko::Character>(choice.character);p.vehicle=static_cast<Kinoko::Vehicle>(choice.vehicle);
        p.type=i?Kinoko::System::RaceConfig::Player::Type::CPU:Kinoko::System::RaceConfig::Player::Type::Local;
        // Retail CPUs drift manually (KPadAI: driftIsAuto 0, so AIControlBase::init picks
        // the manual kart controller). The caller sets the human's transmission.
        p.driftIsAuto=false;
    }
}
bool RaceGrid::reset(){
    using namespace Kinoko;
    drivers_={};finished_={};laps_={};route_.clear();bulletRoute_.clear();
    auto* config=System::RaceConfig::Instance();if(!config)return false;
    count_=config->raceScenario().playerCount;
    ghostMask_=0;
    for(unsigned i=0;i<count_;++i){
        const auto& p=config->raceScenario().players[i];
        if(p.type!=System::RaceConfig::Player::Type::Ghost)continue;
        ghostMask_|=1u<<i;
        for(unsigned c=0;c<GarageChoices.size();++c)if(GarageChoices[c].character==unsigned(p.character)&&GarageChoices[c].vehicle==unsigned(p.vehicle)){choices_[i]=c;break;}
    }
    auto* kartObjects=Kart::KartObjectManager::Instance();
    for(unsigned i=0;i<count_;++i){
        auto* kart=kartObjects->object(i);
        ranks_[i]=i+1;
        // Stat speed times the engine-class rate (retail KartMove +0x14).
        drivers_[i].nativeSpeed=kart->move()->statBaseSpeed();
        drivers_[i].last=native(kart->pos());
    }
    size_t size=0;const void* kmp=System::ResourceManager::Instance()->getFile("course.kmp",&size,System::ArchiveId::Course);
    bool ok=route_.load(kmp,size);bulletRoute_.load(kmp,size,true);
    size_t paramSize=0;const void* params=System::ResourceManager::Instance()->getFile("kartAISpdParam.bin",&paramSize,System::ArchiveId::Core);
    const unsigned engineClass=std::min<unsigned>(config->raceScenario().engineClass,2);
    const bool grandPrix=session_.mode==Session::Mode::GrandPrix,set=grandPrix||session_.mode==Session::Mode::Versus;
    // AiHolder_initEngineClassAndCpuMode (0x8073999C): a Grand Prix's CPU mode is its engine
    // class (50cc Easy, 100cc Normal, 150cc and Mirror Hard). VS takes the VS rules' CPU
    // setting, which this port's menus do not offer: Normal.
    const unsigned cpuMode=grandPrix?engineClass:1;
    speedParamReady_=readCpuSpeedParam(params,paramSize,unsigned(config->raceScenario().course),engineClass,cpuMode,speedParam_,grandPrix,config->raceScenario().mirror);
    if (battleArena_ && !ok) {
        speedParamReady_=false;
        for(unsigned i=0;i<count_;++i)ranks_[i]=i+1;
        CpuAi::Setup setup;
        setup.route=nullptr;
        setup.count=count_;
        ai_.reset(setup);
        return true;
    }
    // Distance adapter: a permitted native ENPT circuit scales checkpoint lap
    // completion. Retail uses mean CKPT circuit length; branched-path parity remains pending.
    lapDistance_=0;std::vector<bool> visited(route_.size(),false);unsigned point=0;
    while(point<route_.size()&&!visited[point]){visited[point]=true;unsigned next=route_.successor(point,0);lapDistance_+=length(route_.point(next).position-route_.point(point).position);point=next;}
    // AISpeedRace compares against the retail mean-checkpoint distance scale.
    // ENPT paths include long authored branch chords, so using their raw loop
    // length suppresses the catch-up response on several courses.
    const float routeLength=lapDistance_;
    lapDistance_=std::clamp(lapDistance_,4500.f,9000.f);
    afterStep();
    // Retail Enemy::AI (cpu_ai.cpp): the chosen engine class and CPU mode.
    CpuAi::Setup setup;setup.route=&route_;setup.count=count_;setup.engineClass=engineClass;setup.cpuMode=cpuMode;
    setup.speed=speedParam_;setup.speedReady=speedParamReady_;
    size_t actionSize=0;const void* actions=System::ResourceManager::Instance()->getFile("kartAIActionParam.bin",&actionSize,System::ArchiveId::Core);
    setup.actionsReady=readCpuActionParam(actions,actionSize,setup.cpuMode,setup.actions);
    if(!setup.actionsReady)for(auto& a:setup.actions)a={50,10,70,70,80,50,0,0};
    // Rank distances use the mean checkpoint lap length (fn_1_22F088); the ENPT loop
    // stays the fallback for a course without checkpoints.
    const float checkpointLength=meanCheckpointDistance(kmp,size);
    setup.courseLength=checkpointLength>0?checkpointLength:std::max(routeLength,1000.f);
    setup.humanMask=1|ghostMask_;setup.inputOnlyHumans=config->raceScenario().isTimeTrial();
    if(const auto* stage=System::CourseMap::Instance()->getStageInfo())setup.polePosition=stage->polePosition();
    setup.startAtPointZero=!battleArena_;
    if(set){setup.raceNumber=session_.raceNumber;setup.points=session_.points;}
    setup.seed=0x9e3779b9u^(unsigned(config->raceScenario().course)*2654435761u);
    ai_.reset(setup);
    return ok;
}
float RaceGrid::steeringTo(Vec3 forward,Vec3 offset){
    // Kinoko's positive stick produces negative yaw (KartMove::calcTurn).
    const float error=std::atan2(forward.z*offset.x-forward.x*offset.z,forward.x*offset.x+forward.z*offset.z);
    return std::clamp(-error*2.3f,-1.f,1.f);
}
void RaceGrid::beforeStep(uint16_t bulletPlayers){
    using namespace Kinoko;
    auto* manager=System::RaceManager::Instance();auto* objects=Kart::KartObjectManager::Instance();
    if(!manager||!objects)return;
    const bool racing=manager->isStageReached(System::RaceManager::Stage::Race);
    if (battleArena_ && !route_.size()) {
        auto* local=objects->object(0);
        const Vec3 target=native(local->pos());
        for(unsigned i=1;i<count_;++i){
            auto* kart=objects->object(i);
            auto* pad=System::KPadDirector::Instance()->hostController(i);
            const Vec3 pos=native(kart->pos());
            const Vec3 forward=native(kart->bodyFront());
            const Vec3 offset{target.x-pos.x,0.f,target.z-pos.z};
            const float steer=steeringTo(forward,offset);
            const unsigned buttons=1u;
            const int stick=std::clamp(int(std::lround(steer*7.f)), -7, 7);
            pad->setInputsRawStick(buttons,unsigned(stick+7),7,System::Trick::None);
        }
        return;
    }
    float leaderCompletion=manager->player(0).raceCompletion();
    for(unsigned j=1;j<count_;++j)leaderCompletion=std::max(leaderCompletion,manager->player(j).raceCompletion());
    // Retail Enemy::AI drives every CPU outside a Bullet (and player zero for QA).
    uint32_t aiMask=0;
    for(unsigned i=0;i<count_;++i)if(!(bulletPlayers&(1u<<i))&&(i||driveLocal)&&!ghost(i))aiMask|=1u<<i;
    if(ai_.ready()){
        // CPUs run the retail stuck handler inside CpuAi; the QA autopilot keeps
        // the adapted recovery below.
        ai_.setRecovering(0,drivers_[0].reverse>0);
        ai_.step(aiMask,ranks_,laps_,finished_,3);
    }
    for(unsigned i=0;i<count_;++i){
        if(ghost(i))continue;
        const bool bullet=(bulletPlayers&(1u<<i))!=0;
        auto& d=drivers_[i];
        if(i==0&&!bullet&&!driveLocal){if(d.bullet)objects->object(i)->move()->setKartSpeedLimit();d.bullet=false;continue;}
        auto* pad=System::KPadDirector::Instance()->hostController(i);
        const auto& route=bullet?bulletRoute_:route_;
        if(!route.size()){pad->setInputsRawStick(0,7,7,System::Trick::None);continue;}
        unsigned& segment=bullet?d.bulletSegment:d.segment;
        auto* kart=objects->object(i);const Vec3 pos=native(kart->pos()),forward=native(kart->bodyFront());
        bool respawning=kart->status().onBit(Kart::eStatus::BeforeRespawn,Kart::eStatus::InRespawn);
        // A human may drive far beyond the last segment between Bullet uses.
        // Bullet follows retail ITPT/ITPH; ordinary CPUs follow ENPT/ENPH.
        // Reacquire with full 3D distance so stacked mall floors stay distinct.
        if(segment==EnemyRoute::Invalid||d.respawning!=respawning||length(pos-d.last)>2000.f||bullet!=d.bullet)segment=route.nearest(pos,forward,i);
        if(bullet!=d.bullet){
            d.stuck=d.reverse=d.recoveries=0;
            if(bullet){d.bulletTurn=.03f;kart->move()->clearDrift();}
            else kart->move()->setKartSpeedLimit();
        }
        if(bullet)kart->move()->setBulletSpeedLimit();
        d.bullet=bullet;
        d.last=pos;d.respawning=respawning;
        // Adapted navigation recovery, not recovered Nintendo AI: keep normal
        // 3D progress (ramps/half-pipes), but recover a segment held for six
        // seconds, or a descending chord whose pursuit target lies behind us.
        // No position, velocity, checkpoint or finish state is changed here.
        if(racing&&!respawning&&!bullet)++d.routeTicks;else d.routeTicks=0;
        float speed=std::fabs(kart->move()->speed());
        if(!bullet&&ai_.ready()){
            // Retail drive: inputs from the AI controller; it also rotates the
            // body toward the route target and sets the CPU base speed.
            const auto& command=ai_.command(i);
            uint16_t buttons=command.buttons;float aiStick=command.stick;
            float speed=std::fabs(kart->move()->speed());
            if(racing&&!respawning&&i==0){
                // QA autopilot only: adapted reverse-out recovery.
                if(speed<3.f){if(++d.stuck>150){d.reverse=70;d.stuck=0;++d.recoveries;}}else d.stuck=0;
                if(d.reverse){--d.reverse;buttons=2;aiStick=0;}
                if(d.recoveries>=3){kart->collide()->activateOob(false,nullptr,true,false);d.recoveries=0;d.reverse=0;}
                if(speed>20.f&&!d.reverse)d.recoveries=0;
            }else if(!respawning&&manager->stage()==System::RaceManager::Stage::Countdown){
                // AIEngine countdown (fn_1_22252C): a CPU holds accelerate once the race
                // frame passes its start boost's frame (0x808CA690), which charges exactly
                // that boost. The QA autopilot keeps the last 24 frames.
                constexpr uint16_t StartAccelFrame[7]={130,999,156,150,144,138,134}; // burnout, none, levels 1-5
                if(i==0?manager->getCountdownTimer()<24:manager->timer()>StartAccelFrame[std::clamp(ai_.startBoostLevel(i)+1,0,6)])buttons=1;
            }
            // Trick input from the retail AITrickHandler (CPU) / AIEngine command.
            const auto trick=racing&&!d.reverse?System::Trick(std::min<uint8_t>(command.trick,4)):System::Trick::None;
            // setStickXUnmirrored(7 + (int)(7 * stick)): truncation, as fn_1_2226BC.
            const int raw=std::clamp(7+int(aiStick*7.f),0,14);
            pad->setInputsRawStick(buttons,unsigned(raw),7,trick);
            continue;
        }
        if(i&&racing&&!bullet&&speedParamReady_){
            const float gap=std::max(0.f,(leaderCompletion-manager->player(i).raceCompletion())*lapDistance_);
            // Stable target ranks adapt the source rank-manager interface. Its
            // randomized top/middle/bottom group state machine is not yet ported.
            const float policySpeed=d.speedPolicy.step(speedParam_,ranks_[i],ranks_[i],gap,laps_[i]);
            d.desiredSpeed=cpuBaseSpeed(d.nativeSpeed,policySpeed);
            kart->move()->setCpuBaseSpeed(d.desiredSpeed);
        }
        const float lookahead=std::clamp(350.f+speed*7.f,400.f,1400.f);
        const Vec3 routeA=route.point(segment).position,routeB=route.point(route.successor(segment,i)).position;
        const bool descentBehind=routeB.y<routeA.y&&
            (horizontalProjection(pos,routeA,routeB)-projection(pos,routeA,routeB))*length(routeB-routeA)>lookahead;
        const bool horizontalRecovery=!bullet&&(d.routeTicks>360||descentBehind);
        for(unsigned guard=0;guard<8;++guard){unsigned next=route.successor(segment,i);const Vec3 a=route.point(segment).position,b=route.point(next).position;
            if(bullet){
                // Retail a5b08 advances after crossing the horizontal plane
                // through the current ITPT point, along its incoming segment.
                Vec3 delta=b-a;delta.y=0;
                if(dot(pos-b,delta)<=0)break;
            }else if((horizontalRecovery?horizontalProjection(pos,a,b):projection(pos,a,b))<1.f&&length(pos-b)>180.f)break;
            segment=next;d.routeTicks=0;
        }
        Vec3 target=bullet?route.point(route.successor(segment,i)).position:route.target(segment,i,pos,lookahead,horizontalRecovery);
        float steer=steeringTo(forward,target-pos);uint16_t buttons=0;
        if(!bullet){
            // Adapted pursuit measures its heading error on the road plane.
            // Global XZ yaw is incorrect on steep/banked course surfaces.
            const Vec3 up=normalized(native(kart->move()->smoothedUp()));
            const Vec3 heading=normalized(tangent(forward,up),forward);
            const Vec3 desired=normalized(tangent(target-pos,up),heading);
            steer=std::clamp(-std::atan2(dot(cross(heading,desired),up),dot(heading,desired))*2.3f,-1.f,1.f);
        }
        if(racing&&!respawning&&bullet){
            // Retail target selection and adaptive rotation rate (a5400).
            // This ground-plane integration retains Kinoko floor/jump physics;
            // the separate retail free-flight/recovery controller is pending.
            buttons=1;d.reverse=d.recoveries=0;
            if(kart->status().offBit(Kart::eStatus::InCannon,Kart::eStatus::InAction)){
                const Vec3 up=normalized(native(kart->move()->smoothedUp()));
                const Vec3 heading=normalized(tangent(native(kart->move()->dir()),up),forward);
                const Vec3 desired=normalized(tangent(target-pos,up),heading);
                const float angle=std::atan2(dot(cross(heading,desired),up),dot(heading,desired));
                d.bulletTurn=bulletTurnRate(d.bulletTurn,angle);
                const float turn=angle*d.bulletTurn;
                EGG::Quatf correction(std::cos(turn*.5f),up.x*std::sin(turn*.5f),up.y*std::sin(turn*.5f),up.z*std::sin(turn*.5f));
                const EGG::Vector3f direction=correction.rotateVector(kart->move()->dir());
                kart->setRot(correction*kart->dynamics()->mainRot());
                kart->move()->setDir(direction);kart->move()->setVel1Dir(direction);
                // Steering is supplied above: no manual drift/hop state or
                // stale human stick is inherited by the transformed driver.
                steer=0;
            }

        }else if(racing&&!respawning){
            // Lift/brake before tight bends. All speed changes pass through
            // original KartMove acceleration/brake/road collision code.
            const float bend=std::fabs(steer);
            // Existing pursuit driver still needs corner braking. The recovered
            // base-speed controller supplies its straight-line pace and catch-up.
            const float pace=speedParamReady_?d.desiredSpeed:76.f;
            const float desired=std::clamp(pace-40.f*bend,32.f,pace);
            buttons=speed>desired+8.f?2:speed<desired?1:0;
            if(speed<3.f||d.routeTicks>360){if(++d.stuck>150){d.reverse=70;d.stuck=0;++d.recoveries;}}
            else d.stuck=0;
            if(d.reverse){
                --d.reverse;buttons=2;steer=-steer;
                // Rejoin the route at the backed-out position instead of
                // repeatedly aiming at the same inaccessible ledge.
                if(!d.reverse)segment=route.nearest(pos,forward,i);
            }
            if(d.recoveries>=3){kart->collide()->activateOob(false,nullptr,true,false);d.recoveries=0;d.reverse=0;}
            if(speed>20.f&&!d.reverse&&d.routeTicks<=360)d.recoveries=0;
        }else if(!respawning&&manager->stage()==System::RaceManager::Stage::Countdown&&manager->getCountdownTimer()<24){buttons=1;}
        const int stick=std::clamp(int(std::lround(steer*7.f))+7,0,14);
        // The recovered AITrickHandler permits tricks at AirStart, excluding
        // jump pads and item/object actions. Probability/direction choice here
        // is native policy; source vehicle trick physics remains unchanged.
        const bool trick=racing&&!bullet&&kart->status().onBit(Kart::eStatus::AirStart)&&kart->status().offBit(Kart::eStatus::JumpPad,Kart::eStatus::InAction);
        pad->setInputsRawStick(buttons,unsigned(stick),7,trick?System::Trick::Up:System::Trick::None);
    }
}
uint16_t RaceGrid::bulletNoEndPlayers() const{
    uint16_t mask=0;
    for(unsigned i=0;i<count_;++i){const auto& d=drivers_[i];if(!d.bullet||d.bulletSegment>=bulletRoute_.size())continue;
        const unsigned next=bulletRoute_.successor(d.bulletSegment,i);
        // Retail KartKiller::readPointSettings (.text a6288), decimal ones
        // digit of ITPT setting2: 1 means the Bullet must not end here.
        if(next<bulletRoute_.size()&&bulletRoute_.point(next).setting2<32768&&bulletRoute_.point(next).setting2%10==1)mask|=uint16_t(1u<<i);
    }
    return mask;
}
void RaceGrid::afterStep(){
    auto* manager=Kinoko::System::RaceManager::Instance();if(!manager)return;
    forwardBumps();
    std::array<unsigned,Capacity> order{};for(unsigned i=0;i<count_;++i)order[i]=i;
    std::sort(order.begin(),order.begin()+count_,[&](unsigned a,unsigned b){
        // A Time Trial ghost never takes a place from the player.
        if(ghost(a)!=ghost(b))return ghost(b);
        const auto& x=manager->player(a);const auto& y=manager->player(b);
        if(x.finished()!=y.finished())return x.finished();
        if(x.finished())return x.finishOrder()<y.finishOrder();
        if(x.raceCompletion()!=y.raceCompletion())return x.raceCompletion()>y.raceCompletion();
        return a<b;
    });
    for(unsigned rank=0;rank<count_;++rank)ranks_[order[rank]]=rank+1;
    // Raceinfo maxLap (RaceinfoPlayer+0x26), not currentLap (+0x24): CtrlRaceLap
    // (0x807EF810), JugemSwitchLap (0x80725BD8) and the AI all read it. endLap
    // (0x805349B8) raises it only past the old max, so backing over the line
    // (decrementLap) never lowers it. Running max of currentLap = maxLap.
    for(unsigned i=0;i<count_;++i){const auto& p=manager->player(i);finished_[i]=p.finished();laps_[i]=std::max(laps_[i],unsigned(std::clamp<int>(p.currentLap(),1,3)));}
}
void RaceGrid::forwardBumps(){
    using namespace Kinoko;
    // Retail racer-to-racer collision now runs inside the vendored kart pass
    // (KartCollide::checkKartCollision); fn_1_609F4 then calls AI::handleBump
    // on the bumped racer with the applied lateral force.
    auto* objects=Kart::KartObjectManager::Instance();
    for(unsigned i=0;i<count_;++i){
        const auto* collide=objects->object(i)->collide();
        if(collide->kartBumped())ai_.bump(i,native(collide->kartBumpForce()));
    }
}
}
