#include <mkw/cpu_ai.hpp>
#include <mkw/race_grid.hpp>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartState.hh>
#include <game/kart/KartDynamics.hh>
#include <game/system/RaceManager.hh>
#include <game/system/CourseMap.hh>
#include <game/system/map/MapdataJugemPoint.hh>
#include <game/field/ObjectDirector.hh>
#include <game/field/obj/ObjectBase.hh>
#include <egg/math/Quat.hh>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace mkw {
namespace {
using namespace Kinoko;
Vec3 native(const EGG::Vector3f& v){return {v.x,v.y,v.z};}
EGG::Vector3f egg(Vec3 v){return EGG::Vector3f(v.x,v.y,v.z);}
constexpr float Epsilon=1.19209e-07f;
// AIPathPoint / AIPathHandler.
constexpr float OffsetPerWidth=50.f;            // 808c736c
constexpr float OffsetJitter=100.f;             // 808c7370
constexpr float CurveThreshold=.3f;             // 808c7374, 808c7378
constexpr float GoNextDistance=1000.f;          // 808c57b8
constexpr float InsideFactor[3]={-.3f,0.f,.7f}; // 808c57bc, 805113d0, 808c57c0
constexpr int SlotSearch[20]={2,-2,3,-3,1,-1,4,-4,5,-5,6,-6,7,-7,8,-8,9,-9,10,-10}; // 8089df58
// fn_1_8148 (PAL 0x805184FC): the respawn point's ENPT link cone, 15 degrees (FIdx 0x8088FA7C),
// and the initial nearest distance (0x8088FA80).
constexpr float RespawnLinkCone=32.f/3.f,RespawnLinkFar=9999999.f;
// AIPathHandler +0x28: the pass mode the handler is initialised with (fn_1_22B228).
constexpr int InitialPassMode=2;
// AIControlBase::setBasicDriveInfo_ and the kart controller.
constexpr float DriveGain=.04f,DriftGain=.05f;  // 808c526c, 808c5270
constexpr float DriftStartDistance=5000.f;      // 808c5274
constexpr float FeedForward=1.5f,FeedForwardDecay=.01f; // 808c5278, 808c527c
constexpr float DriftFailFrames=10.f;           // 808c6e10
constexpr float RampStep=.05f;                  // 8089e5c4
// AISpeedRace.
constexpr float SpeedBase=70.f;                 // 808c6048
constexpr float PostRaceSpeed=.8f;              // 808c600c
constexpr float SlowdownStep=.0003f,OpeningDecay=.0005f; // 808c6004, 808c6008
constexpr float GapResponse=4800.f;             // 808c6000
constexpr float OpeningBonus[9]={2.5f,4.f,5.5f,3.5f,5.f,6.5f,4.5f,6.f,7.5f}; // 8089e254
// AIRankManager and groups.
constexpr int GroupSizes[13][3]={{0,0,0},{1,0,0},{1,1,0},{1,1,1},{2,1,1},{2,2,1},{2,2,2},{3,2,2},{3,3,2},{3,3,3},{3,4,3},{3,5,3},{3,6,3}}; // 8089e024
constexpr int AdvantageChance[3]={60,80,80};    // 808c5e9c
constexpr int OpeningMin=300,OpeningRange=300,RedistributeEvery=300; // 808c5e80..84
constexpr int NormalMin=30,NormalMax[3]={100,120,140}; // 808c5ea2..a8, seconds
constexpr int AdvantageMin[3]={30,40,50},AdvantageMax[3]={50,60,70}; // 808c5eaa..b4, seconds
constexpr float AdvantageBonus[3]={8.f,9.f,10.f}; // 808c5eb8
constexpr float AdvantageDecay=.005f;           // 8089e230
constexpr float CatchupGapMin=6400.f,CatchupGapMax=12800.f; // 808c5e88, 808c5e8c
constexpr float CatchupBonus[3]={8.f,10.f,12.f}; // 808c5e90
constexpr int ChaseMaxRank=4;                   // 808c5efc
constexpr float ChaseGapMin=500.f,ChaseGapMax=6400.f,ChaseBehindLimit=8000.f; // 808c5f00..08
constexpr float ChaseLow[3]={2.f,3.f,4.f},ChaseHigh[3]={8.f,10.f,12.f}; // 808c5edc, 808c5ed0
constexpr float ChaseRankBonus[5]={12.f,4.f,-.5f,-2.f,-4.f}; // 808c5ee8
constexpr float AdvantageValueDecay=.0008f;     // 8089e020
constexpr int TopSwapEvery=600,MiddleHold=600,MiddleClamp=240; // 808c5d70..74
// AIPowAvoider: dodge chance by CPU mode (Easy: bss 0x809C2BDC, zero (inferred: no store
// found); Normal, Hard: 0x808CA57C), rolled when the POW timer is 46 (PowMgr timer-45 == 1).
constexpr int PowAvoidChance[3]={0,20,40};
constexpr uint16_t PowAvoidTimer=46;
}

bool readCpuActionParam(const void* data,size_t size,unsigned cpuMode,std::array<CpuActionParam,12>& out){
    // AiParams_loadAction (8072d9f8): three 12x8 float tables, truncated to bytes.
    if(!data||size<0x180*3||cpuMode>2)return false;
    const auto* p=static_cast<const uint8_t*>(data)+cpuMode*0x180;
    for(unsigned r=0;r<12;++r){uint8_t v[8];
        for(unsigned k=0;k<8;++k){const uint8_t* q=p+(r*8+k)*4;uint32_t w=uint32_t(q[0])<<24|uint32_t(q[1])<<16|uint32_t(q[2])<<8|q[3];float f;std::memcpy(&f,&w,4);
            if(!std::isfinite(f))return false;v[k]=uint8_t(std::clamp(int(f),0,255));}
        out[r]={v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]};
    }
    return true;
}

uint32_t CpuAi::rand(uint32_t limit){rng_=rng_*1664525u+1013904223u;return limit?(rng_>>8)%limit:0;}
float CpuAi::randF(float limit){rng_=rng_*1664525u+1013904223u;return float(rng_>>8)*(limit/16777216.f);}
const CpuActionParam& CpuAi::action(const Racer& r) const{
    return setup_.actions[std::clamp(r.rank.position,1,12)-1];
}
Vec3 CpuAi::pointPos(unsigned index) const{return route_->point(std::min(index,route_->size()-1)).position;}
float CpuAi::widthOffset(unsigned index) const{return OffsetPerWidth*route_->point(std::min(index,route_->size()-1)).width;}

unsigned CpuAi::chooseBranch(unsigned racer,unsigned from,bool allowMushroom){
    // AIPathPoint::chooseNext (fn_1_22D000): mushroom-only routes (successor
    // setting1 1) when holding a mushroom, otherwise a uniform ordinary route.
    const auto& p=route_->point(from);
    std::array<int,6> kinds{};unsigned mushrooms=0,normal=0;
    for(unsigned k=0;k<p.nextCount;++k){if(route_->point(p.next[k]).setting1==1){kinds[k]=1;++mushrooms;}else ++normal;}
    const bool mushroom=allowMushroom&&mushrooms&&racers_[racer].mushroomRoute;
    const uint32_t pick=rand(mushroom?mushrooms:normal);const int want=mushroom?1:0;
    for(unsigned k=0,n=0;k<p.nextCount;++k)if(kinds[k]==want){if(n==pick)return k;++n;}
    return 0;
}
void CpuAi::computeCurvature(Path& p) const{
    // fn_1_22D338: signed horizontal turn angle at the current point.
    Vec3 back=pointPos(p.cur)-pointPos(p.prev),ahead=pointPos(p.next)-pointPos(p.cur);back.y=ahead.y=0;
    const Vec3 side=cross(Vec3{0,1,0},back);
    if(dot(back,back)>Epsilon)back=normalized(back);
    ahead=normalized(ahead,{0,0,0});
    const float angle=std::acos(std::clamp(dot(back,ahead),-1.f,1.f));
    p.curvature=dot(side,ahead)>0?-angle:angle;
}
void CpuAi::recalcTarget(Path& p,float inside) const{
    // fn_1_22D5D8: current point shifted along (dir x up); sharp corners
    // blend toward the inside by the CPU-mode factor.
    const Vec3 a=pointPos(p.cur),dir=normalized(pointPos(p.next)-a,{0,0,1});
    const Vec3 side=normalized(cross(dir,Vec3{0,1,0}),{1,0,0});
    float offset=p.offset;
    if(std::fabs(p.curvature)>=CurveThreshold){const float k=std::fabs(inside);const float f=p.curvature<=0?-inside:inside;offset=p.maxOffset*f+offset*(1.f-k);}
    p.target=a+side*offset;
}
float CpuAi::slotOffset(unsigned racer) const{
    // fn_1_22BFE0: twelve lateral slots per ENPT point; a CPU sharing its slot
    // with another active driver at the same point moves to the nearest free one.
    const Path& me=racers_[racer].path;if(me.maxOffset<=0)return me.offset;
    std::array<bool,12> occupied{};int mine=-1;
    auto slotOf=[&](float offset){return std::clamp(int(6.f*(offset/me.maxOffset)-.5f)+6,0,11);};
    for(unsigned j=0;j<setup_.count;++j){const auto& other=racers_[j];if(!other.active||other.path.cur!=me.cur)continue;
        if(j==racer)mine=slotOf(me.offset);else if(other.path.active)occupied[slotOf(other.path.offset)]=true;}
    if(mine<0||!occupied[mine])return me.offset;
    for(int k:SlotSearch){const int s=std::clamp(mine+k,0,11);if(!occupied[s])return me.maxOffset*((.5f+float(s-6))/6.f);}
    return me.offset;
}
void CpuAi::jitterOffset(unsigned racer,bool spacing){
    Path& p=racers_[racer].path;
    p.offset+=randF(2.f*OffsetJitter)-OffsetJitter;
    p.maxOffset=widthOffset(p.cur);
    p.offset=std::clamp(p.offset,-p.maxOffset,p.maxOffset);
    if(spacing)p.offset=std::clamp(slotOffset(racer),-p.maxOffset,p.maxOffset);
}
void CpuAi::setPoint(unsigned racer,float offset){
    // AIPathPoint::setPoint (fn_1_22C290).
    Path& p=racers_[racer].path;p.offset=offset;
    const auto& start=route_->point(p.start);
    p.branch=start.nextCount>1?int(chooseBranch(racer,p.start)):0;
    p.next=uint16_t(start.next[std::min<unsigned>(unsigned(p.branch),start.nextCount-1)]);
    p.cur=p.prev=p.log2=p.log3=p.start;
    jitterOffset(racer,false);
    p.curvature=0;
    recalcTarget(p,0);
}
void CpuAi::advance(unsigned racer,float inside){
    // fn_1_22C988: shift the point log, pick the next branch, re-roll the offset.
    Path& p=racers_[racer].path;
    const auto& from=route_->point(p.next);
    const unsigned choice=from.nextCount>1?chooseBranch(racer,p.next):0;
    const uint16_t following=uint16_t(from.next[std::min(choice,from.nextCount-1)]);
    p.log3=p.log2;p.log2=p.prev;p.prev=p.cur;p.cur=p.next;p.next=following;p.branch=int(choice);
    jitterOffset(racer,true);
    if(p.prev==p.cur||p.next==p.cur)p.curvature=0;else computeCurvature(p);
    recalcTarget(p,inside);
}
bool CpuAi::shouldGoNext(Path& p,Vec3 kartPos) const{
    // fn_1_22B99C / fn_1_22BC88: within the switch distance, or past the plane
    // through the target (mode 1 blends the incoming and outgoing directions).
    if(p.distance<p.goNext)return true;
    Vec3 normal=normalized(pointPos(p.next)-pointPos(p.cur),{0,0,0});
    if(p.passMode==1){Vec3 back=pointPos(p.cur)-pointPos(p.prev);
        if(dot(back,back)>Epsilon){back=normalized(back);const Vec3 sum=normal+back;normal=dot(sum,sum)<=Epsilon?back:sum;}}
    else if(p.passMode!=2)return false;
    return dot(normal,normal)>Epsilon&&dot(normal,kartPos-p.target)>=0;
}
void CpuAi::goNext(unsigned racer,Vec3 kartPos){
    // AIPathHandler::goNext (fn_1_22BD5C).
    Path& p=racers_[racer].path;
    p.goNext=p.goNextDefault;p.passMode=1;
    const auto& reached=route_->point(p.cur);
    p.currParam={int(reached.setting1),int(reached.driftSetting),reached.flags,p.curvature};
    p.passModeReset=p.passMode;
    advance(racer,p.inside);
    const auto& aim=route_->point(p.cur);
    p.nextParam={int(aim.setting1),int(aim.driftSetting),aim.flags,p.curvature};
    p.distance=length(p.target-kartPos);
    p.switching=true;
}
void CpuAi::updatePath(unsigned racer,Vec3 kartPos){
    // AIPathHandler::update (fn_1_22B2DC).
    Path& p=racers_[racer].path;p.active=true;p.switching=false;
    p.distance=length(p.target-kartPos);
    if(shouldGoNext(p,kartPos))goNext(racer,kartPos);
}
unsigned CpuAi::respawnLink(unsigned racer,Vec3 kartPos) const{
    // The JGPT whose grid slot (fn_1_85B8, PAL 0x8051896C) PlayerSub10_doRespawn placed the
    // racer at; the drop only lowers it, so its X/Z match. Its ENPT link, or an invalid point.
    auto* map=System::CourseMap::Instance();
    unsigned best=~0u;float bestDistance=std::numeric_limits<float>::max();
    for(u16 k=0;map&&k<map->getJugemPointCount()&&k<respawnLinks_.size();++k){
        EGG::Vector3f slot;map->getJugemPoint(k)->findRespawnPos(slot,u8(racer));
        const float dx=slot.x-kartPos.x,dz=slot.z-kartPos.z,distance=dx*dx+dz*dz;
        if(distance<bestDistance){bestDistance=distance;best=k;}
    }
    return best<respawnLinks_.size()?respawnLinks_[best]:route_->size();
}
void CpuAi::onOutOfBounds(unsigned racer,unsigned point){
    // fn_1_22B79C (PAL 0x8073BB50): a valid ENPT link restarts the path there through goNext
    // (fn_1_22BD5C 0x8073C110) in its out-of-bounds form: the default switch distance, offset
    // (AIPathPoint +0x28) 0, the initial pass mode (+0x24 = +0x2c = +0x28) and no reached-point
    // parameters (+0xc cleared).
    if(point>=route_->size())return;
    Path& p=racers_[racer].path;
    p.goNext=p.goNextDefault;p.offset=0;p.passMode=p.passModeReset=InitialPassMode;p.currParam.clear();
    // fn_1_22C988's out-of-bounds branch: the point and its log restart at the link, the branch
    // is drawn from a zeroed chooser (no mushroom route), the offset is re-rolled with the slot
    // spacing, and prev == cur gives curvature 0; the target uses inside factor 0.0 (0x808A27B4).
    const auto& from=route_->point(point);
    const unsigned choice=from.nextCount>1?chooseBranch(racer,point,false):0;
    p.cur=p.prev=p.log2=p.log3=uint16_t(point);
    p.next=uint16_t(from.next[std::min(choice,from.nextCount-1)]);p.branch=int(choice);
    jitterOffset(racer,true);
    p.curvature=0;
    recalcTarget(p,0.f);
    p.nextParam={int(from.setting1),int(from.driftSetting),from.flags,p.curvature};
    // The switch distance is measured from the link point itself; the frame's update follows.
    p.distance=length(p.target-from.position);
    p.switching=true;
}
bool CpuAi::driftHere(const Path& p) const{
    // fn_1_22B530.
    if(p.nextParam.endDrift()||p.nextParam.forbidDrift())return false;
    return std::fabs(p.curvature)>=CurveThreshold||p.nextParam.forceDrift();
}
bool CpuAi::endDriftHere(const Path& p) const{
    // fn_1_22B5D4.
    if(!p.switching||p.currParam.forceDrift())return false;
    if(p.currParam.curvature*p.nextParam.curvature<0||std::fabs(p.curvature)<CurveThreshold)return true;
    return p.currParam.endDrift()||p.currParam.forbidDrift();
}

void CpuAi::reset(const Setup& setup){
    setup_=setup;route_=setup.route&&setup.route->size()>=2?setup.route:nullptr;
    racers_={};groups_={};rng_=setup.seed?setup.seed:1;raceStarted_=false;
    rmState=0;rmFrame=0;rmDuration=0;rmPhase=0;rmInterval=-1;advantageEnabled=false;advantageState=0;advantageValue=0;
    topType=2;cpuCount=humanCount=0;
    respawnLinks_.clear();
    if(!route_)return;
    // CourseMap_init (PAL 0x80512AF4) runs fn_1_8148 (0x805184FC) on every JGPT: +0x28 is the
    // ENPT nearest ahead along the point's rotation (rot * ez) inside a 15 degree cone around it
    // (lateral in rot * ex / rot * ey), ENPT 0 when none is.
    if(auto* map=System::CourseMap::Instance()){
        const float tan=EGG::Mathf::SinFIdx(RespawnLinkCone)/EGG::Mathf::CosFIdx(RespawnLinkCone);
        for(u16 k=0;k<map->getJugemPointCount();++k){
            const auto* jugem=map->getJugemPoint(k);
            EGG::Quatf rpy;rpy.setRPY(DEG2RAD*jugem->rot().x,DEG2RAD*jugem->rot().y,DEG2RAD*jugem->rot().z);
            const EGG::Vector3f ez=rpy.rotateVector(EGG::Vector3f::ez),ex=rpy.rotateVector(EGG::Vector3f::ex),ey=rpy.rotateVector(EGG::Vector3f::ey);
            unsigned link=0xff;float nearest=RespawnLinkFar;
            for(unsigned n=0;n<route_->size()&&n<0xff;++n){
                const Vec3 a=route_->point(n).position;
                const float dx=a.x-jugem->pos().x,dy=a.y-jugem->pos().y,dz=a.z-jugem->pos().z;
                const float ahead=ez.z*dz+ez.x*dx+ez.y*dy; // retail operand order
                if(ahead<0.f)continue;
                const float cone=ahead*tan,side=ex.z*dz+ex.x*dx+ex.y*dy,up=ey.z*dz+ey.x*dx+ey.y*dy;
                if(side*side+up*up<cone*cone&&(link==0xff||ahead<nearest)){nearest=ahead;link=n;}
            }
            respawnLinks_.push_back(uint8_t(link==0xff?0:link));
        }
    }
    auto* objects=Kart::KartObjectManager::Instance();
    const unsigned engine=std::min(setup.engineClass,2u),mode=std::min(setup.cpuMode,2u);
    // Start order: grid slots along the start direction (front-most first).
    std::array<unsigned,Capacity> order{};std::array<float,Capacity> ahead{};
    const unsigned count=std::min(setup.count,Capacity);
    Vec3 forward{0,0,1};if(count)forward=native(objects->object(0)->bodyFront());
    for(unsigned i=0;i<count;++i){order[i]=i;ahead[i]=dot(native(objects->object(i)->pos()),forward);}
    std::sort(order.begin(),order.begin()+count,[&](unsigned a,unsigned b){return ahead[a]>ahead[b]||(ahead[a]==ahead[b]&&a<b);});
    std::array<unsigned,Capacity> startRank{};for(unsigned r=0;r<count;++r)startRank[order[r]]=r+1;
    for(unsigned i=0;i<count;++i){
        auto& r=racers_[i];r.active=true;r.human=(setup.humanMask>>i)&1;
        auto* kart=objects->object(i);
        r.canMiniturbo=true;
        r.speed.openingBonus=OpeningBonus[engine*3+mode];r.speed.catchup=10.f;
        r.rank.position=int(startRank[i]);
        // AIControlBase::initAfterManager / AIControlRace (fn_1_22015C, fn_1_2202F8).
        Path& p=r.path;
        // fn_1_22C244: a race starts every racer's route at ENPT point 0 (fn_1_26474).
        p.start=setup.startAtPointZero?0:uint16_t(route_->nearest(native(kart->pos()),native(kart->bodyFront()),i));
        if(p.start>=route_->size())p.start=0;
        p.goNext=p.goNextDefault=GoNextDistance;p.inside=InsideFactor[mode];p.passMode=p.passModeReset=2;
        int lane=int((startRank[i]+2)%3)-1; // ((position-1)%3)-1
        // Negated for STGI pole position 1 (fn_1_22015C has no Mirror test).
        if(setup.polePosition==1)lane=-lane;
        setPoint(i,float(lane)*.5f*widthOffset(p.start));
        p.nextParam={int(route_->point(p.cur).setting1),int(route_->point(p.cur).driftSetting),route_->point(p.cur).flags,p.curvature};
    }
    // AIRankManager::init (fn_1_2311B4 -> fn_1_23165C). sortedCpus starts in grid order (the
    // per-frame sort keeps it by position); the group selectors pick CPUs in player order.
    for(unsigned r=0;r<count;++r){const unsigned i=order[r];if(!racers_[i].human)sortedCpus[cpuCount++]=int(i);else sortedHumans[humanCount++]=int(i);}
    const auto& sizes=GroupSizes[std::min(cpuCount,12)];
    const bool firstRace=setup.raceNumber==0;
    std::array<bool,Capacity> taken{};
    auto pick=[&](bool random)->int{
        // fn_1_231E48: the best start position (first race); fn_1_231F18: the most points;
        // fn_1_231FE4: a uniform pick among the CPUs not yet in a group.
        if(random){
            unsigned n=0;for(unsigned i=0;i<count;++i)n+=!racers_[i].human&&!taken[i];
            if(!n)return -1;
            unsigned k=rand(n);
            for(unsigned i=0;i<count;++i)if(!racers_[i].human&&!taken[i]&&k--==0)return int(i);
            return -1;
        }
        int best=-1;
        for(unsigned i=0;i<count;++i){if(racers_[i].human||taken[i])continue;
            if(best<0||(firstRace?racers_[i].rank.position<racers_[best].rank.position:setup.points[i]>setup.points[unsigned(best)]))best=int(i);}
        return best;
    };
    int start=1;
    for(unsigned g=0;g<3;++g){auto& group=groups_[g];group.start=start;group.count=0;
        // Top: start position or points; middle and bottom: start position, else at random.
        for(int k=0;k<sizes[g];++k){const int i=pick(g!=0&&!firstRace);if(i<0)continue;
            taken[unsigned(i)]=true;auto& rank=racers_[unsigned(i)].rank;rank.group=int(g);rank.groupIndex=group.count;group.members[group.count++]=i;}
        if(g==0&&group.count){
            // Top: a random permutation of its target ranks (fn_1_22F494).
            std::array<int,6> perm{};for(int k=0;k<group.count;++k)perm[k]=k;
            for(int k=group.count-1;k>0;--k)std::swap(perm[k],perm[rand(uint32_t(k+1))]);
            for(int k=0;k<group.count;++k)racers_[group.members[k]].rank.target=group.start+perm[k];
        }
        // Middle, bottom: targets in member order (fn_1_22F544).
        else for(int k=0;k<group.count;++k)racers_[group.members[k]].rank.target=group.start+k;
        start+=sizes[g];
    }
    // fn_1_2296C8 (AiHolder_init): the best human's points minus the best CPU's choose the
    // top group's final-lap behaviour (no teams in this port).
    int bestHuman=0,bestCpu=0;
    for(unsigned i=0;i<count;++i)(racers_[i].human?bestHuman:bestCpu)=std::max<int>(racers_[i].human?bestHuman:bestCpu,setup.points[i]);
    topType=bestHuman-bestCpu< -3?1:bestHuman-bestCpu<4?2:3;
    advantageEnabled=int(rand(100))<AdvantageChance[mode];
}

void CpuAi::bump(unsigned racer,Vec3 direction){
    // AI::handleBump: offset += ±lerp(-500,-650) by |right·bump| in [5,15].
    // Retail getBodyForward is the first pose column (the racer's right axis).
    if(!route_||racer>=Capacity||!racers_[racer].active||racers_[racer].human)return;
    auto* kart=Kart::KartObjectManager::Instance()->object(racer);
    const float alignment=dot(native(kart->bodyForward()),direction);
    const float sign=alignment>0?1.f:-1.f;
    const float k=std::clamp((std::fabs(alignment)-5.f)/(15.f-5.f),0.f,1.f);
    Path& p=racers_[racer].path;
    if(!p.active)return;
    // addOffsetRateAndRecalcTargetTrans (8072e6e0).
    p.offset=std::clamp(p.offset+sign*(-500.f+k*(-650.f+500.f)),-p.maxOffset,p.maxOffset);
    recalcTarget(p,p.inside);
}

void CpuAi::swapTargets(Group& g){
    // Shared group helper: exchange the target ranks of two random members.
    if(g.count<2)return;
    swapWithOther(g,int(rand(uint32_t(g.count))));
}
void CpuAi::swapWithOther(Group& g,int a){
    // The member's target rank goes to a random other member (groupIndex+1+rand(count-1)).
    const int b=(a+1+int(rand(uint32_t(g.count-1))))%g.count;
    std::swap(racers_[g.members[a]].rank.target,racers_[g.members[b]].rank.target);
}
void CpuAi::updateTopGroup(unsigned finalLap,const std::array<unsigned,Capacity>& laps){
    // Top group calc (fn_1_230114). Type 2: a target swap every 600 frames. Types 1
    // (fn_1_22F9D4, the CPUs lead the points by 4+) and 3 (fn_1_22FE00, the human leads by
    // 4+): the same until the member holding target 1 reaches the final lap; then the
    // timer restarts at 300 (type 1) / 120 (type 3), a swap follows each time it passes
    // 600, and each such swap ends with the first member (the points leader after race 1)
    // giving target 1 away (type 1) or taking it (type 3).
    auto& top=groups_[0];
    if(top.count<=0)return;
    if(topType==2||top.phase==0){
        if(++top.timer>TopSwapEvery){swapTargets(top);top.timer=0;}
        if(topType==2)return;
        int leader=-1;for(int k=0;k<top.count;++k)if(racers_[top.members[k]].rank.target==1){leader=top.members[k];break;}
        if(leader>=0&&laps[unsigned(leader)]==finalLap){top.phase=1;top.timer=TopSwapEvery/(topType==1?2:5);}
        return;
    }
    if(top.phase!=1||++top.timer<=TopSwapEvery)return;
    swapTargets(top);
    if(top.count>1){
        auto& first=racers_[top.members[0]].rank;
        if(topType==1){if(first.target==1)swapWithOther(top,0);}
        else if(first.target!=1){
            for(int k=0;k<top.count;++k){auto& other=racers_[top.members[k]].rank;if(other.target==1){std::swap(first.target,other.target);break;}}
        }
    }
    top.timer=0;
}
void CpuAi::redistribute(){
    // fn_1_2324E4: middle takes the leading members of middle+bottom.
    auto& middle=groups_[1];auto& bottom=groups_[2];
    const int middleSize=middle.count;middle.count=bottom.count=0;
    for(int k=0;k<cpuCount;++k){auto& rank=racers_[sortedCpus[k]].rank;if(rank.group!=1&&rank.group!=2)continue;
        auto& g=middle.count<middleSize?middle:bottom;rank.group=&g==&middle?1:2;rank.groupIndex=g.count;g.members[g.count++]=sortedCpus[k];}
}
void CpuAi::assignGroupTargets(){
    // fn_1_232914: targets follow current order within each group.
    for(unsigned g=0;g<3;++g){int next=groups_[g].start;
        for(int k=0;k<cpuCount;++k){auto& rank=racers_[sortedCpus[k]].rank;if(rank.group==int(g))rank.target=next++;}}
}
void CpuAi::chase(float t){
    // fn_1_23311C: the leading CPUs match the best human's speed plus a bonus.
    if(!humanCount||!cpuCount)return;
    const auto& human=racers_[sortedHumans[0]];auto& lead=racers_[sortedCpus[0]];
    if(human.rank.position>ChaseMaxRank)return;
    const float gap=human.rank.distance-lead.rank.distance;
    if(gap<=-ChaseBehindLimit)return;
    const unsigned engine=std::min(setup_.engineClass,2u);
    float bonus,second=-1.f,third=-1.f;
    if(human.rank.position==1){const float k=std::clamp((gap-ChaseGapMin)/(ChaseGapMax-ChaseGapMin),0.f,1.f);
        bonus=ChaseLow[engine]+k*(ChaseHigh[engine]-ChaseLow[engine]);second=.5f*bonus;third=.2f*bonus;}
    else bonus=ChaseRankBonus[std::clamp(human.rank.position,0,4)];
    const float humanSpeed=Kart::KartObjectManager::Instance()->object(unsigned(sortedHumans[0]))->move()->speed();
    lead.speed.frameBonus+=((humanSpeed+bonus)-lead.speed.result)*t;
    if(cpuCount>1&&second>0){auto& c=racers_[sortedCpus[1]];c.speed.frameBonus+=((humanSpeed+second)-c.speed.result)*t;}
    if(cpuCount>2&&third>0){auto& c=racers_[sortedCpus[2]];c.speed.frameBonus+=((humanSpeed+third)-c.speed.result)*t;}
}
void CpuAi::updateRanks(const std::array<unsigned,Capacity>& ranks,const std::array<unsigned,Capacity>& laps,
                        const std::array<bool,Capacity>& finished,unsigned finalLap){
    // AIRankManager::update (fn_1_2312AC).
    auto* manager=System::RaceManager::Instance();
    for(unsigned i=0;i<setup_.count;++i){auto& r=racers_[i];if(!r.active)continue;
        r.rank.position=int(ranks[i]);r.rank.distance=manager->player(i).raceCompletion()*setup_.courseLength;}
    auto byPosition=[&](int a,int b){return racers_[a].rank.position<racers_[b].rank.position;};
    std::sort(sortedCpus.begin(),sortedCpus.begin()+cpuCount,byPosition);
    std::sort(sortedHumans.begin(),sortedHumans.begin()+humanCount,byPosition);
    for(int k=0;k<cpuCount;++k)racers_[sortedCpus[k]].rank.compact=k+1;
    const unsigned engine=std::min(setup_.engineClass,2u),mode=std::min(setup_.cpuMode,2u);
    // fn_1_232590: every CPU speeds up while even the last human leads far ahead.
    if(humanCount&&cpuCount){
        const float gap=racers_[sortedHumans[humanCount-1]].rank.distance-racers_[sortedCpus[0]].rank.distance;
        if(gap>CatchupGapMin){const float k=std::clamp((gap-CatchupGapMin)/(CatchupGapMax-CatchupGapMin),0.f,1.f);
            for(int k2=0;k2<cpuCount;++k2)racers_[sortedCpus[k2]].speed.frameBonus+=k*CatchupBonus[engine];}
    }
    if(cpuCount){const float lead=racers_[sortedCpus[0]].rank.distance;racers_[sortedCpus[0]].rank.gap=0;
        for(int k=1;k<cpuCount;++k){auto& rank=racers_[sortedCpus[k]].rank;rank.gap=std::max(0.f,lead-rank.distance);}}
    const bool racing=manager->isStageReached(System::RaceManager::Stage::Race);
    auto clampAtLeast=[&](Group& g,int minimum){for(int k=0;k<g.count;++k){auto& rank=racers_[g.members[k]].rank;rank.target=std::max(rank.compact,minimum);}};
    const int afterTop=groups_[0].count+1;
    bool humanFinished=false;for(int k=0;k<humanCount;++k)humanFinished|=finished[unsigned(sortedHumans[k])];
    if(humanFinished&&rmState!=4){
        // fn_1_232F90: goal.
        rmState=4;for(int k=0;k<cpuCount;++k)racers_[sortedCpus[k]].speed.advantage=0;advantageEnabled=false;
    }
    switch(rmState){
    case 0: // Ready (fn_1_23275C / fn_1_23280C)
        if(racing){rmState=1;rmDuration=OpeningMin+int(rand(OpeningRange));}
        break;
    case 1: // Opening (fn_1_232858 / fn_1_2328A4)
        clampAtLeast(groups_[1],afterTop);clampAtLeast(groups_[2],afterTop);
        if(rmDuration<rmFrame){rmState=2;redistribute();assignGroupTargets();for(auto& g:groups_)g.phase=g.timer=0;
            rmDuration=rmFrame+(NormalMin+int(rand(uint32_t(std::max(0,NormalMax[mode]-NormalMin)))))*60;}
        break;
    case 2:{ // Normal (fn_1_232914 / fn_1_232ACC)
        updateTopGroup(finalLap,laps);
        auto& middle=groups_[1];
        if(middle.count>0){
            if(middle.phase==0){swapTargets(middle);middle.timer=0;middle.phase=1;}
            else if(middle.phase==1){if(++middle.timer>MiddleHold){middle.timer=0;middle.phase=2;}}
            else{clampAtLeast(middle,afterTop);if(++middle.timer>MiddleClamp)middle.phase=0;}
        }
        clampAtLeast(groups_[2],afterTop);
        if(rmFrame%RedistributeEvery==0)redistribute();
        if(advantageEnabled&&mode<=1&&humanCount&&laps[unsigned(sortedHumans[0])]==finalLap)advantageEnabled=false;
        if(advantageEnabled&&rmDuration<rmFrame){
            // Speed advantage (fn_1_232C2C).
            rmState=3;rmPhase=0;
            const int targets[3]={1,2,4};const float offsets[3][2]={{.5f,1.f},{0.f,2.f},{0.f,4.f}};
            for(int g=0;g<3;++g)for(int k=0;k<groups_[g].count;++k){auto& rank=racers_[groups_[g].members[k]].rank;rank.target=targets[g];rank.offset=offsets[g][0]+randF(offsets[g][1]);}
            for(int k=0;k<cpuCount;++k)racers_[sortedCpus[k]].speed.advantage=randF(AdvantageBonus[engine]);
            rmDuration=rmFrame+(AdvantageMin[mode]+int(rand(uint32_t(AdvantageMax[mode]-AdvantageMin[mode]))))*60;
            if(advantageState==0)advantageState=1;
        }
        break;}
    case 3: // Speed advantage (fn_1_232E08)
        if(rmPhase==0){if(rmDuration<rmFrame){if(advantageState==1)advantageState=2;rmPhase=1;}}
        else{for(int k=0;k<cpuCount;++k){auto& s=racers_[sortedCpus[k]].speed;s.advantage=std::max(0.f,s.advantage-AdvantageDecay);}
            if(rmDuration+1200<rmFrame){rmState=2;for(int g=0;g<3;++g)for(int k=0;k<groups_[g].count;++k)racers_[groups_[g].members[k]].rank.offset=0;
                // Normal-state entry (fn_1_232914) also restarts the top and middle groups
                // (fn_1_230104, fn_1_230448: phase and timer 0).
                groups_[0].phase=groups_[0].timer=groups_[1].phase=groups_[1].timer=0;
                redistribute();assignGroupTargets();rmDuration=rmFrame+(NormalMin+int(rand(uint32_t(std::max(0,NormalMax[mode]-NormalMin)))))*60;}}
        break;
    default:break;
    }
    if(advantageState==1){advantageValue=1.f;chase(advantageValue);}
    else if(advantageState==2){advantageValue-=AdvantageValueDecay;if(advantageValue<0){advantageValue=0;advantageState=0;}chase(advantageValue);}
    ++rmFrame;
}
void CpuAi::updateSpeed(unsigned racer,unsigned lap){
    // AISpeedRace::update (fn_1_233A40).
    auto& r=racers_[racer];auto& s=r.speed;const auto& param=setup_.speed;
    s.slowdown=std::min(1.f,s.slowdown+SlowdownStep);
    if(lap>1)s.opening=std::max(0.f,s.opening-OpeningDecay);
    float speed=s.openingBonus*s.opening+SpeedBase+param.advantage;
    s.result=speed;speed+=s.frameBonus;
    const float withAdvantage=s.advantage+speed;
    if(r.rank.compact!=1&&setup_.speedReady){
        const float error=cpuTargetGap(unsigned(std::max(r.rank.target,1)),r.rank.offset,param.bias)-r.rank.gap;
        if(error<=0)speed=withAdvantage+std::clamp(-error/GapResponse,0.f,1.f)*s.catchup;
        else speed=withAdvantage+std::clamp(error/GapResponse,0.f,1.f)*s.slowdown*(param.minimum-withAdvantage);
    }
    // State bit 92 (PlayerSub10_applyInk): an inked CPU drives at 80%.
    if(r.itemView.inked)speed*=0.8f;
    Kart::KartObjectManager::Instance()->object(racer)->move()->setCpuBaseSpeed(speed);
    s.frameBonus=0;
}

void CpuAi::drive(unsigned racer){
    auto& r=racers_[racer];Path& p=r.path;Drive& d=r.drive;
    auto* kart=Kart::KartObjectManager::Instance()->object(racer);
    const auto& status=kart->status();
    const Vec3 pos=native(kart->pos());
    // AITrickHandler(Bike)::calc (AIEngine runs it first, on last tick's path
    // state): a trick roll on take-off, then the bike wheelie points.
    uint8_t trick=0;
    const bool bike=kart->isBike();
    if(status.onBit(Kart::eStatus::AirStart)&&status.offBit(Kart::eStatus::JumpPad,Kart::eStatus::InAction)&&
       int(rand(100))<int(action(r).trick))
        trick=uint8_t(1+rand(bike?4u:2u));
    if(bike&&p.switching){
        if(p.currParam.setting1==3&&int(rand(100))<int(action(r).wheelie)&&rmState!=3)r.wheelie=true;
        if(p.currParam.setting1==4){r.wheelie=false;trick=2;}
        if(r.wheelie)trick=1;
    }
    // AIControlBase::setBasicDriveInfo_ (80723178), using last tick's path state
    // (the stuck handler's recovery target while it slides back along the route).
    const Vec3 target=recoveryTarget(r);
    bool startDrift=false,endDrift=false,releaseMiniturbo=false;float driftDir=0;
    // fn_1_6E6E0: speed above 0.55 of the stat base speed (KartMove +0x14).
    if(d.state!=1&&driftHere(p)&&kart->move()->speed()>kart->move()->statBaseSpeed()*.55f&&p.distance<DriftStartDistance){
        // AIProbabilityRace::getDrift(true): one roll per opportunity.
        if(!r.driftRolled){r.driftRolled=true;if(int(rand(100))<int(action(r).drift)){startDrift=true;driftDir=p.nextParam.curvature>0?-1.f:1.f;}}
    }else r.driftRolled=false; // getDrift(false) on every other frame re-arms the roll
    const bool switched=p.switching;
    if(switched){
        d.feedForward=FeedForward*p.currParam.curvature;
        if(endDriftHere(p)){endDrift=true;
            if(!p.currParam.forbidDrift()&&r.canMiniturbo&&int(rand(100))<int(action(r).miniturbo))releaseMiniturbo=true;}
    }
    d.feedForward+=FeedForwardDecay*(0.f-d.feedForward);
    // AIControlRace::doUpdate (fn_1_2204D4): block line, then doUpdate_: route,
    // obstacle avoider, stuck handler and the kart controller (fn_1_220824).
    if(!bike&&!r.human)blockUpdate(racer);
    updatePath(racer,pos);
    avoid(racer);
    if(!r.human)stuckUpdate(racer);
    const Avoid& av=r.avoid;
    // After the goal the run is no longer recorded: retail post-race driving.
    const bool rotate=!(r.human&&setup_.inputOnlyHumans&&!driveFinished_);
    uint16_t buttons=0;float stick=0,animationStick=0;bool drifting=true;
    if(d.state==0){
        drifting=false;buttons|=1;
        // fn_1_220A00: the curvature feed-forward, or full lock away from the avoid steer,
        // goes only to the driver animation (AI +0x14) and the bike lean (KartMove +0x28C).
        // The pad stick stays neutral (fn_1_2226BC); the body rotation below steers.
        float feed=std::clamp(d.feedForward,-1.f,1.f);
        if(av.active)feed=av.steer<0?1.f:-1.f;
        animationStick=d.lean=feed;
        // Time Trial QA autopilot only (no body rotation): it steers by its input.
        if(!rotate)stick=feed;
        // The drift timer (+0x14) is not reset here; ENPT switches reset it.
        if(startDrift){d.driftDir=driftDir;d.ramp=0;d.failFrames=0;d.state=1;}
    }else{
        d.switched=switched;
        // fn_1_220AE8: hop and hold the turn direction.
        const bool hop=status.onBit(Kart::eStatus::Hop);
        stick=(!status.onBit(Kart::eStatus::TouchingGround)||hop)?-d.driftDir:d.driftDir*d.ramp;
        if(d.switched)d.timer=0;
        // The driver animation sees the stick from 8 to 38 frames after an ENPT switch and
        // from 48 on (0x808CA644/48/4C: 8, 30, 10).
        animationStick=d.timer<8.f||(d.timer>=38.f&&d.timer<48.f)?0.f:stick;
        d.ramp=std::min(1.f,d.ramp+RampStep);d.timer=std::min(9999.9f,d.timer+1.f);
        buttons|=1|8;
        if(!status.onBit(Kart::eStatus::DriftManual)&&!hop){
            if(d.failFrames<=DriftFailFrames)d.failFrames+=1.f;else d.state=0;
        }else{
            if(endDrift){if(!releaseMiniturbo){buttons&=uint16_t(~(1u|8u));buttons|=2;}d.state=0;}
            d.failFrames=0;
        }
    }
    // fn_1_2210EC: avoider yaw, drift-hop yaw correction, then (on the ground)
    // fn_1_220C90 rotates the body toward the target on the road plane.
    // AI::isDriftingOnGround: state bit 61, set by KartMove::calcDirs.
    const bool groundDirs=status.onBit(Kart::eStatus::GroundDirs);
    {
        Vec3 up=native(kart->move()->smoothedUp());if(dot(up,up)<=Epsilon)up={0,1,0};up=normalized(up);
        // The tracking direction is sampled before this frame's yaw changes:
        // the velocity direction while drifting, the body forward otherwise.
        const Vec3 dir=normalized(drifting?native(kart->move()->vel1Dir()):
                                           native(kart->dynamics()->mainRot().rotateVector(EGG::Vector3f::ez)),{0,0,1});
        auto yaw=[&](float angle){
            EGG::Quatf q;q.setAxisRotation(angle,egg(up));
            EGG::Quatf next=q*kart->dynamics()->mainRot();next.normalise();kart->dynamics()->setMainRot(next);
        };
        bool track=groundDirs&&rotate;
        if(av.active&&rotate){yaw(av.steer);track=false;}
        const float angle=DEG2RAD*kart->move()->outsideDriftAngle();
        const float turned=angle-d.lastAngle;
        if(rotate&&std::fabs(turned)>Epsilon&&status.onBit(Kart::eStatus::Hop))yaw(-std::clamp(turned,-.08f,.08f)*.5f);
        d.lastAngle=angle;
        if(track){
        const auto& rot=kart->dynamics()->mainRot();
        const Vec3 toTarget=target-pos;
        Vec3 planar=toTarget-up*dot(toTarget,up);
        if(dot(planar,planar)>Epsilon){planar=normalized(planar);
            Vec3 desired=dir+(planar-dir)*(drifting?DriftGain:DriveGain*r.block.gainScale*r.stuck.gainScale);
            if(dot(desired,desired)>Epsilon){desired=normalized(desired);
                EGG::Quatf q;q.makeVectorRotation(egg(dir),egg(desired));
                EGG::Quatf next=q*rot;next.normalise();kart->dynamics()->setMainRot(next);}
        }
        }
    }
    // AIPowAvoider::update (0x8072A7A0), after the kart controller: one roll on the frame
    // the POW timer reads 46 (fn_1_2A2AF8 == 1, the frame before the third stomp); a pass
    // presses trick Up (AITrickHandler::avoidPow 0x807352F8). Without teams isSameTeam is
    // false for every racer, the POW's user included.
    if(!r.human&&powTimer_==PowAvoidTimer&&int(rand(100))<PowAvoidChance[std::min(setup_.cpuMode,2u)])trick=1;
    r.command={buttons,stick,trick,animationStick,d.lean};
}

void CpuAi::step(uint32_t driveMask,const std::array<unsigned,Capacity>& ranks,const std::array<unsigned,Capacity>& laps,
                 const std::array<bool,Capacity>& finished,unsigned finalLap){
    if(!route_)return;
    auto* manager=System::RaceManager::Instance();
    ++frame_;obstaclesValid_=false;
    updateRanks(ranks,laps,finished,finalLap);
    const bool racing=manager->isStageReached(System::RaceManager::Stage::Race);
    for(unsigned i=0;i<setup_.count;++i){
        auto& r=racers_[i];if(!r.active)continue;
        auto* kart=Kart::KartObjectManager::Instance()->object(i);
        const auto& status=kart->status();
        const bool dropping=status.onBit(Kart::eStatus::BeforeRespawn,Kart::eStatus::InRespawn);
        // PlayerSub10_doRespawn (PAL 0x80584334) places the racer at its respawn point and calls
        // Enemy::AI::onOutOfBounds (0x807263B0 -> AIControlRace fn_1_2203A4 0x80730758); the AI
        // update later in the same PlayerSub_update (0x80596480) runs at the new position. Here:
        // the first tick that sees InRespawn, before this tick's update.
        const bool respawning=status.onBit(Kart::eStatus::InRespawn);
        if(respawning&&!r.respawning){
            onOutOfBounds(i,respawnLink(i,native(kart->pos())));
            r.drive={};
            // onOutOfBoundsInner (0x8072A2A8) re-inits the stuck handler (fn_1_234870) and the
            // avoider (fn_1_216834); fn_1_2203A4 returns block line to Search (fn_1_21936C).
            if(r.stuck.state==1)kart->move()->setStuckRecovery(false); // state 1 exit, fn_1_2352A4
            r.stuck={};r.avoid={};r.block.next=1;
        }
        r.respawning=respawning;
        if(!((driveMask>>i)&1)){
            // Humans still advance their route state for rank bookkeeping.
            updatePath(i,native(kart->pos()));
            // A CPU leaves the mask only for a Bullet: AIControlBase::doUpdate_ (0x8072A570)
            // then skips the kart controller, so AI +0x14 and KartMove +0x28C keep their values.
            r.command=r.human?CpuCommand{}:CpuCommand{0,0,0,r.command.animationStick,r.command.lean};
            continue;
        }
        if(!racing&&!r.human&&!r.startDecided&&manager->stage()==System::RaceManager::Stage::Countdown){
            // AIControlBase::setStartDashTypeToKart (8072310c) with
            // AIProbabilityRace::getStartBoost (fn_1_22EAD8).
            r.startDecided=true;
            const auto& a=action(r);const int roll=int(rand(100));const unsigned mode=std::min(setup_.cpuMode,2u);
            if(roll<int(a.startBoost))r.startBoost=int(rand(3))+int(mode)+1;
            else if(roll<int(a.startBoost)+int(a.startFail))r.startBoost=-1;
            else r.startBoost=0;
            kart->state()->setStartBoostIdx(r.startBoost);
        }
        // After its goal a CPU drives on AIEngine PostRaceDriving (USA
        // 0x80728C28): AISpeedRace vt_0x14 (0x80733F54) holds 0.8 x the
        // kart's base speed (0x808C600C) instead of the race speed manager.
        if(racing&&!r.human){if(finished[i])kart->move()->setCpuBaseSpeed(PostRaceSpeed*kart->move()->statBaseSpeed());else updateSpeed(i,laps[i]);}
        // The player is isCpu from its goal (fn_1_7EC98), so its ground limit reads the same
        // post-race speed (0x8057BD08; PAL writer 0x8074392C, 0.8 at 0x808CB514).
        else if(racing&&finished[i])kart->move()->setCpuBaseSpeed(PostRaceSpeed*kart->move()->statBaseSpeed());
        if(!racing||dropping){updatePath(i,native(kart->pos()));r.command={};r.itemCommand={};continue;}
        driveFinished_=finished[i];
        drive(i);
        if(!r.human){r.item.goal=r.item.goal||finished[i];stepItems(i);}
    }
}

// ---- Block line (AIControlRace +0x94: ctor fn_1_218F9C, vtable 808c5200) ----
// Cooldown 10 frames, then search: after 300 frames a human within 2500
// units behind (45 degree cone) is blocked when the blockLine roll passes.
// Blocking drifts the route offset toward that racer's lateral position with
// 1.8x steering gain, for up to 300 frames or until they are 3000 units away.
namespace {
constexpr int BlockCooldown=10,BlockSearch=300,BlockDuration=300; // 808c5190..94
constexpr float BlockFind=2500.f,BlockKeep=3000.f,BlockShift=800.f,BlockRate=.025f,BlockGain=1.8f; // 808c51a4..b0
}
bool CpuAi::behindWithin(unsigned i,unsigned j,float range,float cosine) const{
    // fn_1_219374: the other racer is within range and behind along our velocity.
    auto* objects=Kart::KartObjectManager::Instance();auto* kart=objects->object(i);
    const Vec3 away=native(kart->pos())-native(objects->object(j)->pos());
    const float dist=length(away);
    return dist<range&&dot(away,native(kart->move()->vel1Dir()))>dist*cosine;
}
int CpuAi::findBlockTarget(unsigned i) const{
    // fn_1_219590: local humans only.
    for(unsigned j=0;j<setup_.count;++j)
        if(j!=i&&racers_[j].active&&racers_[j].human&&behindWithin(i,j,BlockFind,.7f))return int(j);
    return -1;
}
void CpuAi::blockUpdate(unsigned i){
    // fn_1_219300.
    Racer& r=racers_[i];Block& b=r.block;
    b.gainScale=1;
    if(b.next>=0){b.state=b.next;b.next=-1;b.timer=0;}
    switch(b.state){
    case 0:if(++b.timer>BlockCooldown)b.next=1;break; // fn_1_2196E4
    case 1: // fn_1_219718
        if(b.timer>BlockSearch){
            const int target=findBlockTarget(i);
            if(target>=0){
                if(int(rand(100))<int(action(r).blockLine)){b.target=target;b.next=2;}
                else b.timer=0;
            }
        }else ++b.timer;
        break;
    case 2:{ // fn_1_2197E8
        b.gainScale=BlockGain;
        auto* objects=Kart::KartObjectManager::Instance();auto* kart=objects->object(i);
        const Vec3 side=normalized(cross(native(kart->move()->vel1Dir()),Vec3{0,1,0}),{0,0,0});
        const float lateral=dot(native(objects->object(unsigned(b.target))->pos())-native(kart->pos()),side);
        Path& p=r.path;
        p.offset=std::clamp(p.offset+std::clamp(lateral,-BlockShift,BlockShift)*BlockRate,-p.maxOffset,p.maxOffset);
        recalcTarget(p,p.inside);
        if(++b.timer>BlockDuration||!behindWithin(i,unsigned(b.target),BlockKeep,0.f))b.next=1;
        break;}
    default:break;
    }
}

// ---- Stuck handler (AIControlBase +0x40: ctor fn_1_23437C, vtable 808c6170) ----
// Detect counts frames of < 8 units movement on the ground (x3 against a wall);
// past 90 frames Recover aims at a target that slides back along the logged
// route while the racer stays against a wall, drives there at 20..40 with
// double steering gain, and returns once it reaches a point or faces the
// route again. Ten seconds of recovery, 7.5 s pinned, or 20 s without a new
// route point send the racer to Lakitu.
namespace {
constexpr int StuckFrames=90,RecoverLimit1=120,RecoverLimit3=360,RecoverTimeout=600; // 808c610c..12
constexpr int WallSampleFrames=30,WallSamples=15,NoProgressFrames=1200;           // 808c6116/18
constexpr float StuckMove=8.f,WallSampleMove=200.f,RecoverReach=300.f,RecoverGoNext=300.f; // 808c5d10
}
Vec3 CpuAi::recoveryTarget(const Racer& r) const{
    // fn_1_22C7C8: rate 0..1 blends the target to the previous point, then on
    // through the two before it.
    const Path& p=r.path;const float rate=r.stuck.rate;
    if(rate<=0)return p.target;
    if(rate<=1)return p.target+(pointPos(p.prev)-p.target)*rate;
    if(rate<=2)return pointPos(p.prev)+(pointPos(p.log2)-pointPos(p.prev))*(rate-1);
    return pointPos(p.log2)+(pointPos(p.log3)-pointPos(p.log2))*std::min(rate-2,1.f);
}
bool CpuAi::stuckWallCheck(unsigned i){
    // fn_1_2349F8: every 30 grounded frames, under 200 units of movement adds
    // a pinned sample; 15 in a row means stuck for good.
    Racer& r=racers_[i];Stuck& s=r.stuck;
    auto* kart=Kart::KartObjectManager::Instance()->object(i);const auto& st=kart->status();
    // AI::isDriftingOnGround (state bit 61) and not in an action.
    if(st.offBit(Kart::eStatus::GroundDirs)||st.onBit(Kart::eStatus::InAction))return false;
    if(++s.sampleTimer>WallSampleFrames){
        const Vec3 pos=native(kart->pos());
        if(length(pos-s.sample)>=WallSampleMove)s.wallSamples=0;else ++s.wallSamples;
        s.sample=pos;s.sampleTimer=0;
    }
    if(s.wallSamples<WallSamples)return false;
    s.wallSamples=WallSamples;return true;
}
void CpuAi::stuckUpdate(unsigned i){
    // fn_1_2347F0: sequencer calc; the gain scale applies to DriveInfo +0xc.
    Racer& r=racers_[i];Stuck& s=r.stuck;Path& p=r.path;
    auto* kart=Kart::KartObjectManager::Instance()->object(i);const auto& st=kart->status();
    const Vec3 pos=native(kart->pos());
    if(!s.primed){s.primed=true;s.last=s.sample=pos;}
    s.gainScale=1;
    if(s.next>=0){
        if(s.state==1){p.goNext=p.goNextDefault;s.rate=0;kart->move()->setStuckRecovery(false);} // fn_1_2352A4
        s.state=s.next;s.next=-1;
        switch(s.state){
        case 0:s.counter=0;s.timer=0;s.phase=0;break;                       // fn_1_234BC4
        case 1:p.goNext=RecoverGoNext;s.rate=0;s.timer=0;s.phase=0;kart->move()->setStuckRecovery(true);break;  // fn_1_234DB0
        case 2:kart->collide()->activateOob(false,nullptr,true,false);s.next=0;break; // fn_1_23530C
        }
    }
    const bool inAction=st.onBit(Kart::eStatus::InAction);
    const bool ground=st.onBit(Kart::eStatus::GroundDirs)&&!inAction;
    // fn_1_2353B8: wall contact (state bits 5/6) holds the flag for 10 frames.
    if(st.onBit(Kart::eStatus::Wall3Collision,Kart::eStatus::WallCollision))s.wallTimer=10;
    s.wallTimer=std::max(0,s.wallTimer-1);
    const bool wall=s.wallTimer>0;
    auto progressRespawn=[&]{
        // Shared tail: pinned for good, or no new route point for 20 s.
        bool respawn=stuckWallCheck(i);
        if(!respawn&&!inAction){
            ++s.noProgress;if(p.switching)s.noProgress=0;
            if(s.noProgress>NoProgressFrames){s.noProgress=NoProgressFrames;respawn=true;}
        }
        if(respawn)s.next=2;
    };
    if(s.state==0){ // fn_1_234BD8
        const float moved=length(pos-s.last);s.last=pos;
        if(moved>=StuckMove)s.counter=0;
        else if(ground)s.counter+=wall?3:1;
        if(s.counter>StuckFrames){s.counter=StuckFrames;s.next=1;}
        progressRespawn();
    }else if(s.state==1){ // fn_1_234E24
        const Vec3 aim=recoveryTarget(r);Vec3 toAim=aim-pos;const float dist=length(toAim);
        s.last=pos;s.gainScale=2.f;
        float rate=s.rate;bool done=false;
        if(s.phase==0){
            ++s.timer;
            const float limit=s.timer<RecoverLimit1?1.f:s.timer>=RecoverLimit3?3.f:2.f;
            rate+=.01f;
            if(!wall){++s.phase;rate=std::floor(rate);}
            rate=std::clamp(rate,0.f,limit);
        }else if(s.phase==1){
            if(!wall){Vec3 flat=toAim;flat.y=0;if(length(flat)<RecoverReach){rate=0;++s.phase;}}
            else s.phase=0;
        }else{
            if(!wall){
                const Vec3 a=normalized(toAim,{0,0,0}),v=normalized(native(kart->move()->vel1Dir()),{0,0,0});
                if(dot(a,v)>.8f)done=true;
            }else s.phase=0;
        }
        // fn_1_22D580: centre the current point's target, then store the rate.
        p.offset=0;recalcTarget(p,p.inside);s.rate=rate;
        const float k=std::clamp((dist-2000.f)/(4000.f-2000.f),0.f,1.f);
        kart->move()->setCpuBaseSpeed(20.f+k*20.f);
        if(p.switching||done)s.next=0;
        else if(s.timer>RecoverTimeout)s.next=2;
        progressRespawn();
    }
    r.recovering=s.state==1;
}

// ---- Obstacle avoider (AIControlBase +0x48: ctor fn_1_2161C0, vtable 808c688c) ----
// Normal probes 500..1800 units along the velocity direction; a course object
// that would react to the kart, or a landed item hazard, near the probe end
// triggers Avoid (or a weaker Late avoid when the dodge roll fails). Mega and
// Thunder Cloud racers seek nearby racers instead. Inked CPUs wobble.
namespace {
constexpr int AvoidHold=8,AvoidRelease=8,LateDelay=12,LateHold=5;      // 808c67d4..da
constexpr int InkCalm=20,InkCalmRange=40,InkSwerve=10,InkSwerveRange=10; // 808c67dc..e2
constexpr float InkSteer=.015f;                                         // 808c67c8
constexpr std::array<int,3> AvoidChance{25,50,75};                      // fn_1_229CB0
float avoidSteer(Vec3 start,Vec3 end,Vec3 obstacle){
    // fn_1_21683C: steer away from the side of the probe the obstacle is on.
    const Vec3 side=cross(end-start,Vec3{0,1,0});
    return dot(side,obstacle-end)>=0?.02f:-.02f;
}
float seekSteer(Vec3 start,Vec3 end,Vec3 obstacle){
    // fn_1_216960: proportional steer toward the target racer.
    const Vec3 side=normalized(cross(end-start,Vec3{0,1,0}),{0,0,0});
    const float k=dot(side,obstacle-end);
    return k>=0?-4e-5f*std::fabs(k):4e-5f*std::fabs(k);
}
}
void CpuAi::enterAvoidState(unsigned i){
    Racer& r=racers_[i];Avoid& a=r.avoid;
    switch(a.state){
    case 0:a.active=false;a.steer=0;break; // fn_1_216CD0
    case 1: // fn_1_216F10
        a.timer=0;a.phase=0;a.active=true;
        a.steer=a.result==2?seekSteer(a.start,a.end,a.obstacle):a.steerWeight*avoidSteer(a.start,a.end,a.obstacle);
        a.steer=std::clamp(a.steer,-.1f,.1f);break;
    case 2:a.timer=0;a.phase=0;break; // fn_1_21703C
    case 4: // fn_1_21716C
        a.timer=InkCalm+int(rand(InkCalmRange));a.phase=0;a.steer=InkSteer;
        if(rand(2)==0)a.steer=-a.steer;
        break;
    default:break;
    }
}
bool CpuAi::findObstacle(unsigned i){
    // fn_1_229D34: one course object near the probe end, else landed hazards.
    Racer& r=racers_[i];Avoid& a=r.avoid;
    auto* kart=Kart::KartObjectManager::Instance()->object(i);
    const auto& status=kart->status();
    const bool star=status.onBit(Kart::eStatus::Star),mega=status.onBit(Kart::eStatus::Mega);
    bool found=false;
    if(auto* director=Field::ObjectDirector::Instance();director&&!star&&!mega&&!kart->move()->bulletActive()){
        // Only the normal-kart column of GeoHitTableKart is loaded here.
        const Field::ObjectBase* nearest=nullptr;float best=0;
        if(!obstaclesValid_){
            obstacles_.clear();
            for(const auto* obj:director->objects())if(obj)obstacles_.push_back({obj,native(obj->pos()),400.f+obj->getCollisionRadius()});
            obstaclesValid_=true;
        }
        for(const auto& o:obstacles_){
            const Vec3 delta=o.position-a.end;const float dist=dot(delta,delta);
            if(dist<o.reach*o.reach&&(!nearest||dist<best)){nearest=static_cast<const Field::ObjectBase*>(o.object);best=dist;}
        }
        if(nearest&&std::fabs(nearest->pos().y-a.end.y)<=600.f){
            const auto& table=director->hitTableKart();const s16 slot=table.slot(nearest->id());
            if(slot!=-1&&table.reaction(slot)!=Kart::Reaction::None){
                found=true;a.chanceWeight=1.5f;a.steerWeight=1.5f;a.obstacle=native(nearest->pos());
            }
        }
    }
    if(!found&&!star&&!mega){
        for(unsigned k=0;k<hazardCount_;++k){
            const Vec3 delta=hazards_[k]-a.end;const float reach=400.f+hazardRadii_[k];
            if(std::fabs(delta.y)>600.f||dot(delta,delta)>=reach*reach)continue;
            found=true;a.chanceWeight=1.f;a.steerWeight=.6f;a.obstacle=hazards_[k];break;
        }
    }
    if(found){
        const int chance=int(float(AvoidChance[std::min(setup_.cpuMode,2u)])*a.chanceWeight);
        a.result=int(rand(100))<chance?1:3;
    }else a.result=0;
    return found;
}
void CpuAi::avoid(unsigned i){
    // AIControlBase +0x48 update (fn_1_2167E0): sequencer calc; the caller
    // reads active/steer as DriveInfo +0x28/+0x20.
    Racer& r=racers_[i];Avoid& a=r.avoid;
    if(a.next>=0){
        if(a.state==1||a.state==2||a.state==4)a.active=false; // exits fn_1_217030/21711C/217308
        a.state=a.next;a.next=-1;enterAvoidState(i);
    }
    auto* objects=Kart::KartObjectManager::Instance();
    auto* kart=objects->object(i);
    switch(a.state){
    case 0:{ // fn_1_216CE8
        const Vec3 pos=native(kart->pos());
        const float k=std::clamp((kart->move()->speed()-20.f)/(60.f-20.f),0.f,1.f);
        a.start=pos;a.end=pos+native(kart->move()->vel1Dir())*(500.f+k*(1800.f-500.f));
        a.obstacle={};a.steerWeight=a.chanceWeight=1.f;a.result=0;
        if(!r.recovering){
            if(kart->status().onBit(Kart::eStatus::Mega,Kart::eStatus::ThunderCloud)){
                // fn_1_216AC4: two racers per frame, round robin.
                const unsigned count=std::max(1u,setup_.count);unsigned j=a.seek%count;
                for(unsigned n=0;n<2;++n,j=(j+1)%count){
                    if(j==i)continue;
                    const Vec3 p=native(objects->object(j)->pos());
                    if(std::fabs(p.x-a.end.x)<=800.f&&std::fabs(p.z-a.end.z)<=800.f&&std::fabs(p.y-a.end.y)<=300.f){
                        a.obstacle=p;a.result=2;break;
                    }
                }
                a.seek=(a.seek+2)%count;
            }
            // Each racer tests obstacles on alternate frames (player index parity).
            if(((i^frame_)&1u)==0)findObstacle(i);
        }
        if(a.result==1||a.result==2)a.next=1;
        else if(a.result==3)a.next=2;
        else if(r.itemView.inked)a.next=4;
        break;}
    case 1: // fn_1_216FC0
        ++a.timer;
        if(a.phase==0){if(a.timer>AvoidHold){a.phase=1;a.timer=0;}}
        else{a.steer=0;if(a.timer>AvoidRelease)a.next=0;}
        break;
    case 2: // fn_1_21704C
        ++a.timer;
        if(a.phase==0){
            a.active=false;
            if(a.timer>LateDelay){a.steer=a.steerWeight*avoidSteer(a.start,a.end,a.obstacle)*.5f;a.timer=0;a.phase=1;}
        }else{a.active=true;if(a.timer>LateHold)a.next=0;}
        break;
    case 4: // fn_1_217200
        --a.timer;
        if(a.phase==0){a.active=false;if(a.timer<0){a.timer=InkSwerve+int(rand(InkSwerveRange));a.steer=-a.steer;a.phase=1;}}
        else{a.active=true;if(a.timer<0){a.timer=InkCalm+int(rand(InkCalmRange));a.phase=0;}}
        if(!r.itemView.inked)a.next=0;
        break;
    default:break;
    }
}

// ---- AIItemRace (ctor 8072a3dc; AIItemBase 80729210, vtable 808c5b40) ----
// States Idle/Stock/ThrowDefault/Kinoko/ThrowFront/ThrowBack/Hold/Goal run
// through Util::StateSequencer (8073b06c): a requested state is entered on
// the next frame (exit, enter, calc). The item button is a per-frame action.
namespace {
constexpr uint8_t ItemNone=20,ItemGreenShell=0,ItemRedShell=1,ItemBanana=2,ItemMushroom=4,
                  ItemTripleMushroom=5,ItemGoldenMushroom=10;
constexpr int StockDelayTriple=120,StockRangeTriple=120,StockDelay=40,StockRange=160; // 808c5a6c..72
constexpr int ThrowRange=200,MushroomRepeat=80;                                      // 808c5a6a, 808c5a68
constexpr std::array<int,3> HoldChance{0,25,60};      // fn_1_224310, top rank group only
constexpr std::array<int,3> MushroomRouteChance{0,50,100}; // fn_1_22B6C4 (handler +0x18 = CPU mode)
}
void CpuAi::stepItems(unsigned i){
    // AIItemRace::update (fn_1_2242C0): sequencer calc, then clear useItem.
    Racer& r=racers_[i];ItemAi& a=r.item;
    r.itemCommand={};
    if(a.next!=ItemState::None){
        if(a.state==ItemState::Mushroom)r.mushroomRoute=false; // Kinoko exit (fn_1_224D1C)
        a.state=a.next;a.next=ItemState::None;enterItemState(i);
    }
    calcItemState(i);
    a.useItem=false;
}
void CpuAi::enterItemState(unsigned i){
    Racer& r=racers_[i];ItemAi& a=r.item;const CpuItemView& v=r.itemView;
    switch(a.state){
    case ItemState::Idle:case ItemState::ThrowFront:case ItemState::ThrowBack:a.counter=0;break;
    case ItemState::Stock: // fn_1_2246BC
        a.delay=v.heldCount==3?StockDelayTriple+int(rand(StockRangeTriple)):StockDelay+int(rand(StockRange));break;
    case ItemState::Throw:a.delay=int(rand(ThrowRange));break; // fn_1_224880
    case ItemState::Mushroom: // fn_1_224A58
        a.delay=int(rand(ThrowRange));
        if(v.item!=ItemNone){
            const int chance=MushroomRouteChance[std::min(setup_.cpuMode,2u)];
            const bool route=chance>=100||(chance>0&&int(rand(100))<chance);
            r.mushroomRoute=v.item!=ItemGoldenMushroom&&route;
        }
        break;
    default:break;
    }
}
void CpuAi::calcItemState(unsigned i){
    Racer& r=racers_[i];ItemAi& a=r.item;const CpuItemView& v=r.itemView;CpuItemCommand& out=r.itemCommand;
    auto press=[&]{out.use=true;};
    auto go=[&](ItemState s){a.next=s;};
    auto release=[&](ItemState idle){if(a.dir==0)go(idle);else go(a.dir==1?ItemState::ThrowFront:ItemState::ThrowBack);};
    const uint8_t kind=v.item;
    switch(a.state){
    case ItemState::Idle: // fn_1_2246A4
        if(a.goal){go(ItemState::Goal);break;}
        if(v.held){go(ItemState::Stock);break;}
        if(kind==ItemNone){
            // Roulette spinning: tap every other frame to stop it sooner.
            if(v.roulette){if(a.counter==0){press();a.counter=1;}else a.counter=0;}
        }else if(kind>=16&&kind<=18){
            // Triple items: press, then two released frames, to deploy them.
            if(a.counter==0){press();a.counter=1;}else if(a.counter==1)a.counter=2;else a.counter=0;
        }else if(kind==ItemMushroom||kind==ItemTripleMushroom||kind==ItemGoldenMushroom)go(ItemState::Mushroom);
        else if((kind==ItemGreenShell||kind==ItemBanana)&&shouldHoldItem(i)){press();go(ItemState::Hold);}
        else go(ItemState::Throw);
        break;
    case ItemState::Stock: // fn_1_22478C, then AIItemBase stock calc fn_1_223CB0
        if(v.held&&!r.recovering){
            a.dir=0;
            if(--a.delay<0){a.useItem=true;a.dir=throwDirection(i,true);}
        }
        if(!v.held)go(ItemState::Idle);
        else if(a.useItem){if(a.dir==0){press();go(ItemState::Stock);}else release(ItemState::Stock);}
        break;
    case ItemState::Throw: // fn_1_2248C0
        if(kind!=ItemNone&&!r.recovering&&--a.delay<0){a.useItem=true;a.dir=throwDirection(i,false);}
        if(kind==ItemNone)go(ItemState::Idle);
        else if(a.useItem){if(a.dir==0){press();go(ItemState::Idle);}else release(ItemState::Idle);}
        break;
    case ItemState::Mushroom: // fn_1_224B30
        if(kind!=ItemNone&&!r.recovering){
            bool use=false;
            // ENPT setting3 bit 7 forbids mushrooms; the countdown pauses there.
            if(!(r.path.currParam.flags&0x80)&&--a.delay<0)use=true;
            // On a mushroom route, use one on reaching a setting1 2 point.
            if(r.mushroomRoute&&r.path.switching&&r.path.currParam.setting1==2)use=true;
            if(use){a.useItem=true;if(kind==ItemTripleMushroom||kind==ItemGoldenMushroom)a.delay=MushroomRepeat;}
        }
        if(kind==ItemNone)go(ItemState::Idle);
        else if(a.useItem){press();if(kind!=ItemTripleMushroom&&kind!=ItemGoldenMushroom)go(ItemState::Idle);}
        break;
    case ItemState::ThrowFront:case ItemState::ThrowBack: // fn_1_223F44, fn_1_22404C
        (a.state==ItemState::ThrowFront?out.forwards:out.backwards)=true;
        if(a.counter==0){press();a.counter=1;}
        else if(a.counter==1)a.counter=2;
        else go(v.held?ItemState::Stock:ItemState::Idle);
        break;
    case ItemState::Hold: // fn_1_224D30: hold the button until a new item arrives.
        if(kind!=ItemNone&&!r.recovering){a.useItem=true;a.dir=throwDirection(i,false);}
        press();
        if(a.useItem)release(ItemState::Idle);
        break;
    default:break;
    }
}
bool CpuAi::shouldHoldItem(unsigned i){
    // fn_1_224310: CPUs in the top rank group (type 1) may drag a shell or banana.
    if(racers_[i].rank.group!=0)return false;
    const int roll=int(rand(100));
    return roll<HoldChance[std::min(setup_.cpuMode,2u)];
}
int CpuAi::throwDirection(unsigned i,bool held){
    // fn_1_22450C: 0 neutral, 1 forward, 2 backward.
    const Racer& r=racers_[i];
    if(r.rank.position==1)return 2;
    if(r.rank.position==int(setup_.count))return 1;
    const int roll=int(rand(100));
    const bool red=held?r.itemView.heldRed:r.itemView.item==ItemRedShell;
    const unsigned mode=std::min(setup_.cpuMode,2u);
    if(red||mode==0)return 0;
    const int side=humanAdjacent(i);
    if(side==0||roll<(mode==1?80:20))return 0;
    return side;
}
int CpuAi::humanAdjacent(unsigned i){
    // fn_1_224404: a human directly ahead (throw forward) or behind (backward).
    const int count=std::min(humanCount,4);
    if(count<=0)return 0;
    int k=int(rand(uint32_t(count)));
    for(int n=0;n<count;++n,k=(k+1)%count){
        const int d=racers_[i].rank.position-racers_[sortedHumans[k]].rank.position;
        if(d==1)return 1;
        if(d==-1)return 2;
    }
    return 0;
}
}
