#include <mkw/race_grid.hpp>
#include <mkw/battle_route.hpp>
#include <mkw/bullet_bill.hpp>
#include <mkw/mii_garage.hpp>
#include <mkw/garage.hpp>
#include <mkw/race_teams.hpp>
#include <game/system/RaceConfig.hh>
#include <game/system/RaceManager.hh>
#include <game/system/KPadDirector.hh>
#include <game/system/ResourceManager.hh>
#include <game/system/CourseMap.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartCollide.hh>
#include <game/kart/KartPhysics.hh>
#include <game/field/CollisionDirector.hh>
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
// Killer route-plane normal (USA805b2964). Non-pushing query keeps the
// kart's collision-entry array; detach/restore its optional no-bounce output.
Vec3 bulletRouteNormal(Vec3 start,Vec3 end){
    using namespace Kinoko;
    Field::CollisionInfo info{};
    auto* course=Field::CourseColMgr::Instance();
    auto* saved=course->noBounceWallInfo();course->clearNoBounceWallInfo();
    const bool hit=Field::CollisionDirector::Instance()->checkSphereFull(100.f,
        EGG::Vector3f(start.x,start.y,start.z),EGG::Vector3f::inf,
        Field::KCLTypeMask(0x20e80fff),&info,nullptr,0);
    course->setNoBounceWallInfo(saved);
    if(hit&&info.floorDist>-std::numeric_limits<float>::min())return native(info.floorNrm);
    const Vec3 route=start-end;
    return normalized(cross(cross(route,{0,1,0}),route),{});
}
// Retail keeps vehicle/BSP ground support beneath Killer's visual offset.
// The 3DS controller runs before the ordinary kart pass: resolve that support
// explicitly at both integration boundaries, while an actual gap keeps the
// authored ITPT flight controller. Never fabricate a floor or use the ITPT Y as terrain.
bool supportBullet(Kinoko::Kart::KartObject* kart){
    using namespace Kinoko;
    if(kart->status().onBit(Kart::eStatus::BeforeRespawn,Kart::eStatus::InRespawn,Kart::eStatus::InCannon))return false;
    const float height=kart->bsp().initialYPos*kart->scale().y;
    const float radius=std::clamp(height+75.f,100.f,200.f);
    Field::CollisionInfo info{};auto* course=Field::CourseColMgr::Instance();auto* saved=course->noBounceWallInfo();course->clearNoBounceWallInfo();
    const bool hit=Field::CollisionDirector::Instance()->checkSphereFull(radius,kart->pos(),EGG::Vector3f::inf,Field::KCLTypeMask(KCL_TYPE_DRIVER_FLOOR),&info,nullptr,0);
    course->setNoBounceWallInfo(saved);
    if(!hit||info.floorDist<0||info.floorNrm.y<.2f){kart->move()->setBulletGroundContact(false);return false;}
    const auto position=kart->pos()+info.floorNrm*(height-(radius-info.floorDist));
    kart->setPos(position);kart->physics()->updatePose();
    kart->move()->setBulletGroundContact(true,info.floorNrm);
    auto vel=kart->dynamics()->extVel();vel-=info.floorNrm*vel.dot(info.floorNrm);kart->dynamics()->setExtVel(vel);
    kart->dynamics()->setNoGravity(false);
    return true;
}
float horizontalProjection(Vec3 p,Vec3 a,Vec3 b){p.y=a.y=b.y=0;const Vec3 d=b-a;const float n=dot(d,d);return n>0.001f?dot(p-a,d)/n:1.f;}
float projection(Vec3 p,Vec3 a,Vec3 b){const Vec3 d=b-a;const float n=dot(d,d);return n>0.001f?dot(p-a,d)/n:1.f;}
}

bool EnemyRoute::load(const void* bytes,size_t size,bool itemPath,bool battle){
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
            v.requiresBoost=!itemPath&&!battle&&points[start].setting1==1;
            if(battle&&!itemPath){
                if(!battleRouteSuccessors(b+enph+8,groups,count,j,v.next,v.nextCount))return false;
                continue;
            }
            if(j+1<start+length){v.next[v.nextCount++]=j+1;continue;}
            for(unsigned k=0;k<6;++k){const unsigned next=p[8+k];if(next==255)continue;
                if(next>=groups)return false;const unsigned point=b[enph+8+next*16];if(point>=count)return false;
                if(std::find(v.next.begin(),v.next.begin()+v.nextCount,point)==v.next.begin()+v.nextCount)v.next[v.nextCount++]=point;
            }
            if(v.nextCount==0)return false;
        }
    }
    if(std::find(covered.begin(),covered.end(),false)!=covered.end())return false;
    points_=std::move(points);itemPath_=itemPath;return true;
}
unsigned EnemyRoute::successor(unsigned index,unsigned racer) const{
    if(index>=size()||!points_[index].nextCount)return Invalid;
    const auto& p=points_[index];
    // Killer passes branch 0 to KMP::GetNextITPT (USA805b13f4).
    if(itemPath_)return p.next[0];
    std::array<unsigned,12> allowed{};unsigned count=0;
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

void RaceGrid::configure(Kinoko::System::RaceConfig& config,unsigned localChoice,unsigned count,const std::array<unsigned,Capacity>* roster){
    constexpr unsigned driverCount=GarageChoices.size()/VehiclesPerDriver;
    static_assert(GarageChoices.size()%VehiclesPerDriver==0&&driverCount>=Capacity);
    count_=std::clamp(count,1u,Capacity);localChoice%=GarageChoices.size()+MiiGarageChoices.size();
    auto& scenario=config.raceScenario();scenario.playerCount=count_;
    const unsigned course=unsigned(scenario.course);
    battleArena_=course>=32&&course<42;
    const unsigned localDriver=localChoice/VehiclesPerDriver;
    const auto teams=offlineVsTeams(scenario.teams,RaceTeam(scenario.humanTeam));
    for(unsigned i=0;i<Capacity;++i){auto& p=scenario.players[i];p.team=uint8_t(teams[i]);
        if(i>=count_){p.type=Kinoko::System::RaceConfig::Player::Type::None;continue;}
        choices_[i]=i?offlineRaceDriver(localDriver,i,driverCount)*VehiclesPerDriver:localChoice;
        if(i&&scenario.gameMode==Kinoko::System::RaceConfig::Scenario::GameMode::Versus){
            constexpr unsigned karts[]={0,1,2,6,7,8},bikes[]={3,4,5,9,10,11};
            const unsigned kind=scenario.cpuVehicles==2?(i&1):scenario.cpuVehicles;
            // Keep each opponent's machine across the VS set and restarts.
            choices_[i]+=(kind?bikes:karts)[i%6];
        }
        if(i&&roster)choices_[i]=(*roster)[i];
        const auto& choice=raceGarageChoice(choices_[i]);
        p.character=static_cast<Kinoko::Character>(choice.character);p.vehicle=static_cast<Kinoko::Vehicle>(choice.vehicle);
        p.type=i?Kinoko::System::RaceConfig::Player::Type::CPU:Kinoko::System::RaceConfig::Player::Type::Local;
        p.driftIsAuto=i!=0;
    }
}
bool RaceGrid::reset(){
    using namespace Kinoko;
    drivers_={};finished_={};laps_={};route_.clear();bulletRoute_.clear();
    auto* config=System::RaceConfig::Instance();if(!config)return false;
    count_=config->raceScenario().playerCount;
    auto* kartObjects=Kart::KartObjectManager::Instance();
    for(unsigned i=0;i<count_;++i){
        auto* kart=kartObjects->object(i);
        ranks_[i]=i+1;
        drivers_[i].nativeSpeed=kart->param()->stats().speed;
        drivers_[i].last=native(kart->pos());
    }
    size_t size=0;const void* kmp=System::ResourceManager::Instance()->getFile("course.kmp",&size,System::ArchiveId::Course);
    bool ok=route_.load(kmp,size,false,battleArena_);bulletRoute_.load(kmp,size,true);
    size_t paramSize=0;const void* params=System::ResourceManager::Instance()->getFile("kartAISpdParam.bin",&paramSize,System::ArchiveId::Core);
    const auto& scenario=config->raceScenario();
    const unsigned engine=std::min(unsigned(scenario.engineClass),2u);
    engineClass_=engine;difficulty_=cpuModeForRace(scenario.gameMode==System::RaceConfig::Scenario::GameMode::Grand_Prix,engine,scenario.cpuDifficulty);
    const bool grandPrix=scenario.gameMode==System::RaceConfig::Scenario::GameMode::Grand_Prix;
    speedParamReady_=readCpuSpeedParam(params,paramSize,unsigned(scenario.course),engine,difficulty_,speedParam_,grandPrix,scenario.mirror);
    if(battleArena_&&!ok){
        speedParamReady_=false;
        CpuAi::Setup setup;setup.route=nullptr;setup.count=count_;ai_.reset(setup);
        return true;
    }
    // Keep the old fallback driver's ENPT scale separate from CpuAi below.
    // CpuAi receives the original mean CKPT circuit distance.
    lapDistance_=0;std::vector<bool> visited(route_.size(),false);unsigned point=0;
    while(point<route_.size()&&!visited[point]){visited[point]=true;unsigned next=route_.successor(point,0);lapDistance_+=length(route_.point(next).position-route_.point(point).position);point=next;}
    // AISpeedRace compares against the retail mean-checkpoint distance scale.
    // ENPT paths include long authored branch chords, so using their raw loop
    // length suppresses the catch-up response on several courses.
    lapDistance_=std::clamp(lapDistance_,4500.f,9000.f);
    afterStep();
    // Retail Enemy::AI uses the selected class's opening/catch-up tables.
    CpuAi::Setup setup;setup.route=&route_;setup.count=count_;setup.engineClass=engine;setup.cpuMode=difficulty_;setup.mirror=scenario.mirror;
    setup.battle=battleArena_;setup.speed=speedParam_;setup.speedReady=speedParamReady_;
    size_t actionSize=0;const void* actions=System::ResourceManager::Instance()->getFile("kartAIActionParam.bin",&actionSize,System::ArchiveId::Core);
    setup.actionsReady=readCpuActionParam(actions,actionSize,setup.cpuMode,setup.actions);
    if(!setup.actionsReady)for(auto& a:setup.actions)a={50,10,70,70,80,50,0,0};
    const auto* checkpoints=System::CourseMap::Instance()->checkPoint();
    setup.courseLength=checkpoints?checkpoints->calcTotalDistance():0.f;setup.humanMask=1;
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
    if(battleArena_&&manager->isStageReached(System::RaceManager::Stage::FinishGlobal)){
        for(unsigned i=0;i<count_;++i)System::KPadDirector::Instance()->hostController(i)->setInputsRawStick(0,7,7,System::Trick::None);
        return;
    }
    const bool racing=manager->isStageReached(System::RaceManager::Stage::Race);
    if (battleArena_ && !route_.size()) {
        if(!racing)return;
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
    for(unsigned i=0;i<count_;++i)if(!(bulletPlayers&(1u<<i))&&(i||driveLocal))aiMask|=1u<<i;
    if(ai_.ready()){
        // CPUs run the retail stuck handler inside CpuAi; the QA autopilot keeps
        // the adapted recovery below.
        ai_.setRecovering(0,drivers_[0].reverse>0);
        ai_.step(aiMask,ranks_,laps_,finished_,3);
    }
    for(unsigned i=0;i<count_;++i){
        const bool bullet=(bulletPlayers&(1u<<i))!=0;
        auto& d=drivers_[i];
        auto* kart=objects->object(i);
        auto releaseBulletGravity=[&]{
            // Respawn and cannon own this flag once their controllers take over.
            if(d.bulletGravity.noGravity&&kart->status().offBit(Kart::eStatus::InRespawn,Kart::eStatus::InCannon))
                kart->dynamics()->setNoGravity(false);
            d.bulletGravity.reset();kart->move()->setBulletRouteFlight(false,EGG::Vector3f::ey);kart->move()->setBulletGroundContact(false);
        };
        if(d.bullet&&!bullet)releaseBulletGravity();
        if(i==0&&!bullet&&!driveLocal){if(d.bullet)kart->move()->setKartSpeedLimit();d.bullet=false;continue;}
        auto* pad=System::KPadDirector::Instance()->hostController(i);
        const auto& route=bullet?bulletRoute_:route_;
        if(!route.size()){releaseBulletGravity();pad->setInputsRawStick(0,7,7,System::Trick::None);continue;}
        unsigned& segment=bullet?d.bulletSegment:d.segment;
        const bool supported=bullet&&supportBullet(kart);
        const Vec3 pos=native(kart->pos()),forward=native(kart->bodyFront());
        bool respawning=kart->status().onBit(Kart::eStatus::BeforeRespawn,Kart::eStatus::InRespawn);
        // A human may drive far beyond the last segment between Bullet uses.
        // Bullet follows retail ITPT/ITPH; ordinary CPUs follow ENPT/ENPH.
        // Reacquire with full 3D distance so stacked mall floors stay distinct.
        if(segment==EnemyRoute::Invalid||d.respawning!=respawning||length(pos-d.last)>2000.f||bullet!=d.bullet)segment=route.nearest(pos,forward,i);
        if(bullet!=d.bullet){
            d.stuck=d.reverse=d.recoveries=0;
            if(bullet){d.bulletTurn=.03f;d.bulletGravity.reset();kart->move()->clearDrift();}
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
            if(racing&&!respawning&&i==0&&!finished_[0]){
                // QA autopilot only: adapted reverse-out recovery.
                if(speed<3.f){if(++d.stuck>150){d.reverse=70;d.stuck=0;++d.recoveries;}}else d.stuck=0;
                if(d.reverse){--d.reverse;buttons=2;aiStick=0;}
                if(d.recoveries>=3){kart->collide()->activateOob(false,nullptr,true,false);d.recoveries=0;d.reverse=0;}
                if(speed>20.f&&!d.reverse)d.recoveries=0;
            }else if(!respawning&&manager->stage()==System::RaceManager::Stage::Countdown&&manager->getCountdownTimer()<24)buttons=1;
            // Trick input from the retail AITrickHandler (CPU) / AIEngine command.
            const auto trick=racing&&!d.reverse?System::Trick(std::min<uint8_t>(command.trick,4)):System::Trick::None;
            const int raw=std::clamp(7+int(std::lround(aiStick*7.f)),0,14);
            pad->setInputsRawStick(buttons,unsigned(raw),7,trick);
            continue;
        }
        if(i&&racing&&!bullet&&speedParamReady_){
            const float gap=std::max(0.f,(leaderCompletion-manager->player(i).raceCompletion())*lapDistance_);
            // Stable target ranks adapt the source rank-manager interface. Its
            // randomized top/middle/bottom group state machine is not yet ported.
            const float policySpeed=d.speedPolicy.step(speedParam_,ranks_[i],ranks_[i],gap,laps_[i],engineClass_,difficulty_);
            d.desiredSpeed=cpuBaseSpeed(d.nativeSpeed,policySpeed);
            kart->move()->setCpuBaseSpeed(d.desiredSpeed);
        }
        const float lookahead=std::clamp(350.f+speed*7.f,400.f,1400.f);
        const Vec3 routeA=route.point(segment).position,routeB=route.point(route.successor(segment,i)).position;
        const bool descentBehind=routeB.y<routeA.y&&
            (horizontalProjection(pos,routeA,routeB)-projection(pos,routeA,routeB))*length(routeB-routeA)>lookahead;
        const bool horizontalRecovery=!bullet&&(d.routeTicks>360||descentBehind);
        if(bullet){
            // The current retail ITPT is the next target of our incoming segment.
            // UpdateGravity consumes it before this tick advances the route.
            const unsigned current=route.successor(segment,i);
            // USA805b2b10..58: tube spans previous/current ITPT, not current/next.
            const auto& a=route.point(current);const auto& b=route.point(segment);
            bool inside=false;
            if(a.setting1!=1&&a.setting1!=2&&!d.bulletGravity.latched){
                const Vec3 normal=bulletRouteNormal(a.position,b.position);
                inside=bulletInsideRoute(pos,a.position,b.position,a.width,b.width,normal,a.setting1!=3,0.f);
            }
            bool noGravity=d.bulletGravity.step(a.setting1,inside,respawning);
            // Retail flight latch survives ordinary road contact (805b2434).
            // Ground support only owns this integration step; clearing the
            // route latch here loses flight across consecutive short gaps.
            if(supported)noGravity=false;
            // Retail Update skips physics writes during the cannon-owned phase.
            if(kart->status().offBit(Kart::eStatus::InCannon,Kart::eStatus::InRespawn)){
                kart->dynamics()->setNoGravity(noGravity);
                if(noGravity)kart->dynamics()->setExtVel(EGG::Vector3f::zero);
            }
            d.bulletGravity.observeWall(kart->status().onBit(Kart::eStatus::WallCollision,Kart::eStatus::Wall3Collision),
                kart->collisionData().wallNrm.y);
        }
        for(unsigned guard=0;guard<8;++guard){unsigned next=route.successor(segment,i);const Vec3 a=route.point(segment).position,b=route.point(next).position;
            if(bullet){
                // USA805b1284: crossing the incoming horizontal plane OR
                // entering clamp(ITPT.width * 50, 700, 1000) advances one point.
                // Omitting the radius made sharp corners chase a point behind
                // the kart and oscillate instead of anticipating the next leg.
                if(!bulletAdvancePoint(pos,a,b,route.point(next).width))break;
            }else if((horizontalRecovery?horizontalProjection(pos,a,b):projection(pos,a,b))<1.f&&length(pos-b)>180.f)break;
            segment=next;d.routeTicks=0;if(bullet)break;
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
            // Existing steering is retained; the route-gated gravity/recovery
            // controller above owns only gravity and external collision velocity.
            buttons=1;d.reverse=d.recoveries=0;
            if(kart->status().offBit(Kart::eStatus::InCannon,Kart::eStatus::InAction)){
                // USA805b1434..1758: aim the complete quaternion at the ITPT,
                // not only yaw in the last road plane. Flight targets add100Y
                // (805b1eec); only an actual wall contact suppresses targetY.
                auto rotation=kart->dynamics()->mainRot();
                rotation.normalise();
                const auto heading=rotation.rotateVector(EGG::Vector3f::ez);
                Vec3 offset=target-pos;
                if(d.bulletGravity.noGravity)offset.y+=100.f;
                if(kart->status().onBit(Kart::eStatus::WallCollision,Kart::eStatus::Wall3Collision))offset.y=0;
                const Vec3 desired=normalized(offset,native(heading));
                Vec3 h=native(heading),aim=desired;h.y=aim.y=0;
                h=normalized(h,{0,0,1});aim=normalized(aim,h);
                const float angle=std::atan2(dot(cross(h,aim),{0,1,0}),dot(h,aim));
                d.bulletTurn=bulletTurnRate(d.bulletTurn,angle);
                EGG::Quatf correction;correction.makeVectorRotation(heading,{desired.x,desired.y,desired.z});
                rotation=rotation.slerpTo(correction*rotation,d.bulletTurn);
                rotation.normalise();
                const auto direction=rotation.rotateVector(EGG::Vector3f::ez);
                // Retail UpdateUps derives up from the route target and the
                // current right axis, rather than adopting a landing lip normal.
                auto right=rotation.rotateVector(EGG::Vector3f::ex);right.y=0;
                EGG::Vector3f routeUp=EGG::Vector3f(offset.x,offset.y,offset.z).cross(right);
                if(routeUp.y<0)routeUp=-routeUp;
                if(routeUp.squaredLength()>0.0000001f)routeUp.normalise();else routeUp=EGG::Vector3f::ey;
                kart->move()->setBulletRouteFlight(d.bulletGravity.noGravity,routeUp);
                kart->setRot(rotation);
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
    auto* objects=Kinoko::Kart::KartObjectManager::Instance();
    for(unsigned i=0;i<count_;++i)if(drivers_[i].bullet)supportBullet(objects->object(i));
    forwardBumps();
    if(!battleArena_){
    std::array<unsigned,Capacity> order{};for(unsigned i=0;i<count_;++i)order[i]=i;
    std::sort(order.begin(),order.begin()+count_,[&](unsigned a,unsigned b){
        const auto& x=manager->player(a);const auto& y=manager->player(b);
        if(x.finished()!=y.finished())return x.finished();
        if(x.finished())return x.finishOrder()<y.finishOrder();
        if(x.raceCompletion()!=y.raceCompletion())return x.raceCompletion()>y.raceCompletion();
        return a<b;
    });
    for(unsigned rank=0;rank<count_;++rank)ranks_[order[rank]]=rank+1;
    } // Battle receives the mode's prior-position/score ranks after scoring.
    for(unsigned i=0;i<count_;++i){const auto& p=manager->player(i);finished_[i]=p.finished();laps_[i]=unsigned(std::clamp<int>(p.currentLap(),1,3));}
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
