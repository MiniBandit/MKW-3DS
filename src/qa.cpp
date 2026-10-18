#include <mkw/qa.hpp>
#include <mkw/race_grid.hpp>
#include <game/system/KPadDirector.hh>
#include <game/system/RaceManager.hh>
#include <game/system/CourseMap.hh>
#include <game/system/map/MapdataJugemPoint.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/Status.hh>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>
namespace mkw {
namespace K=Kinoko;
void QaRaceWatch::step(unsigned now,const RaceGrid& grid,void (*log)(const char*)){
    using St=K::Kart::eStatus;
    constexpr unsigned StallSteps=1200;constexpr float StallProgress=.01f;
    auto* race=K::System::RaceManager::Instance();auto* objects=K::Kart::KartObjectManager::Instance();
    if(!race||!objects||!grid.count())return;
    char line[320];
    auto* kart=objects->object(0);const auto& st=kart->status();const auto& me=race->player(0);
    const auto pos=kart->pos();
    if(grid.driveLocal!=route_){
        route_=grid.driveLocal;
        snprintf(line,sizeof line,"QA route driver %s racer=0 step=%u lap=%u finished=%d comp=%.3f",route_?"on":"off",now,grid.lap(),int(grid.finished()),me.raceCompletion());
        log(line);
    }
    const bool fall=st.onBit(St::BeforeRespawn),respawn=st.onBit(St::InRespawn),landing=st.onBit(St::AfterRespawn);
    if(fall&&!fall_){
        snprintf(line,sizeof line,"QA fall racer=0 step=%u pos=%.0f,%.0f,%.0f lap=%d cp=%u jugem=%d comp=%.3f speed=%.1f route=%d",
                 now,pos.x,pos.y,pos.z,int(me.currentLap()),unsigned(me.checkpointId()),int(me.jugemId()),me.raceCompletion(),kart->move()->speed(),int(grid.driveLocal));
        log(line);
    }
    if(respawn&&!respawn_){
        // The JGPT whose grid slot (fn_1_85B8, PAL 0x8051896C) the racer was placed at.
        int point=-1;float best=1e30f;
        if(auto* map=K::System::CourseMap::Instance())
            for(K::u16 k=0;k<map->getJugemPointCount();++k){
                K::EGG::Vector3f slot;map->getJugemPoint(k)->findRespawnPos(slot,0);
                const float d=(slot.x-pos.x)*(slot.x-pos.x)+(slot.z-pos.z)*(slot.z-pos.z);
                if(d<best){best=d;point=int(k);}
            }
        snprintf(line,sizeof line,"QA respawn racer=0 step=%u jgpt=%d pos=%.0f,%.0f,%.0f cp=%u jugem=%d",now,point,pos.x,pos.y,pos.z,unsigned(me.checkpointId()),int(me.jugemId()));
        log(line);
    }
    if(landing_&&!landing){
        const Vec3 t=grid.ai().target(0);
        snprintf(line,sizeof line,"QA land racer=0 step=%u pos=%.0f,%.0f,%.0f lap=%d cp=%u comp=%.3f ai_point=%u target=%.0f,%.0f,%.0f route=%d",
                 now,pos.x,pos.y,pos.z,int(me.currentLap()),unsigned(me.checkpointId()),me.raceCompletion(),grid.ai().routePoint(0),t.x,t.y,t.z,int(grid.driveLocal));
        log(line);
    }
    fall_=fall;respawn_=respawn;landing_=landing;
    if(!race->isStageReached(K::System::RaceManager::Stage::Race)){racing_=false;return;}
    if(!racing_){
        racing_=true;
        for(unsigned i=0;i<grid.count()&&i<Capacity;++i){best_[i]=race->player(i).raceCompletion();since_[i]=now;}
    }
    for(unsigned i=0;i<grid.count()&&i<Capacity;++i){
        const auto& p=race->player(i);
        if(p.finished()||grid.ghost(i)||p.raceCompletion()>best_[i]+StallProgress){
            best_[i]=std::max(best_[i],p.raceCompletion());since_[i]=now;continue;
        }
        const unsigned stalled=now-since_[i];
        if(!stalled||stalled%StallSteps)continue;
        auto* k=objects->object(i);const auto kp=k->pos();const auto& ks=k->status();
        char flags[8]{};unsigned n=0;
        for(auto [bit,c]:{std::pair{St::TouchingGround,'G'},{St::WallCollision,'W'},{St::InAction,'A'},{St::BeforeRespawn,'F'},{St::InRespawn,'R'},{St::AfterRespawn,'L'}})
            if(ks.onBit(bit)&&n<7)flags[n++]=c;
        const Vec3 t=grid.ai().target(i);
        snprintf(line,sizeof line,"QA stall racer=%u step=%u secs=%u lap=%d comp=%.3f best=%.3f cp=%u jugem=%d pos=%.0f,%.0f,%.0f speed=%.1f st=%s ai_point=%u target=%.0f,%.0f,%.0f stuck=%d route=%d",
                 i,now,stalled/60,int(p.currentLap()),p.raceCompletion(),best_[i],unsigned(p.checkpointId()),int(p.jugemId()),kp.x,kp.y,kp.z,k->move()->speed(),flags,
                 grid.ai().routePoint(i),t.x,t.y,t.z,grid.ai().stuckState(i),int(i?grid.ai().ready():grid.driveLocal));
        log(line);
    }
}
bool runControlChecks(K::EGG::SceneManager& scenes){
    bool valid=true,drift=false,ground=false,wall=false;
    double total=0,worst=0;unsigned steps=0;float speedBefore=0,speedAfter=0,boostBefore=0,boostAfter=0;
    unsigned freeBytes[4]{};
    auto sample=[&](unsigned buttons,int stick,K::System::Trick trick=K::System::Trick::None){
        auto* pad=K::System::KPadDirector::Instance()->hostController();
        if(!pad->setInputsRawStick(buttons,stick+7,7,trick))valid=false;
        auto start=std::chrono::steady_clock::now();scenes.calc();
        double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
        total+=us;if(us>worst)worst=us;steps++;
        auto* kart=K::Kart::KartObjectManager::Instance()->object(0);auto p=kart->pos();
        valid&=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
        drift|=kart->status().onBit(K::Kart::eStatus::DriftManual);
        wall|=kart->status().onBit(K::Kart::eStatus::WallCollision);
    };
    auto frames=[&](int n,unsigned b,int x=0){for(int i=0;i<n;i++)sample(b,x);};
    auto speed=[](){return K::Kart::KartObjectManager::Instance()->object(0)->move()->speed();};
    auto reachSpeedRatio=[&](float minimum){
        auto* move=K::Kart::KartObjectManager::Instance()->object(0)->move();
        for(unsigned i=0;i<240&&move->speedRatioCapped()<minimum;++i)sample(1,0);
        valid&=move->speedRatioCapped()>=minimum;
    };
    auto position=[](){return K::Kart::KartObjectManager::Instance()->object(0)->pos();};
    auto initial=position();freeBytes[0]=scenes.currentScene()->heap()->getAllocatableSize();
    frames(412,0);auto settled=position();ground=settled.y>-10000.f&&settled.y<10000.f;
    frames(90,1);speedBefore=speed();frames(30,2);speedAfter=speed();
    for(int reset=1;reset<=3;reset++){
        scenes.currentScene()->heap()->enableAllocation();scenes.changeSiblingScene(2);
        freeBytes[reset]=scenes.currentScene()->heap()->getAllocatableSize();
        valid&=position()==initial;
        frames(412,0);frames(75,1);
        if(reset==1){
            // Allow speed margin for the hop: merely reaching the 55% start
            // threshold can drop slower vehicles out of drift before landing.
            reachSpeedRatio(0.75f);
            valid&=K::Kart::KartObjectManager::Instance()->object(0)->move()->canStartDrift();
            frames(45,11,7);frames(120,1);
        }
        if(reset==2){boostBefore=speed();frames(1,5);frames(10,1);boostAfter=speed();}
        if(reset==3){frames(600,1);}
    }
    // Synthetic trickable launch: real buffered D-pad input, airborne simulation
    // and landing boost. No trick state or boost result is forced.
    scenes.currentScene()->heap()->enableAllocation();scenes.changeSiblingScene(2);frames(412,0);frames(90,1);
    // Source tricks require >50% base speed. Fixed acceleration time is not
    // sufficient for every starter; reach it using real throttle input.
    reachSpeedRatio(0.65f);
    auto* kart=K::Kart::KartObjectManager::Instance()->object(0);auto launch=kart->pos();launch.y+=600; kart->setPos(launch);
    bool trickStarted=false,trickBoost=false;
    for(int frame=0;frame<180;++frame){
        if(frame<8)kart->state()->setTrickableTimer(20);
        sample(1,0,frame<8?K::System::Trick::Up:K::System::Trick::None);
        trickStarted|=kart->status().onBit(K::Kart::eStatus::InATrick);
        trickBoost|=kart->move()->boostActive(K::Kart::KartBoost::Type::TrickAndZipper);
    }
    bool stable=freeBytes[0]==freeBytes[1]&&freeBytes[1]==freeBytes[2]&&freeBytes[2]==freeBytes[3];
    bool pass=valid&&ground&&drift&&trickStarted&&trickBoost&&speedBefore>20&&speedAfter<speedBefore&&boostAfter>boostBefore&&stable;
    if(FILE* f=fopen("sdmc:/3ds/mkw-3ds/controls-result.json","w")){
        fprintf(f,"{\"pass\":%s,\"finite_and_restart_position\":%s,\"ground_bounds\":%s,\"drift_observed\":%s,\"wall_observed\":%s,\"synthetic_trick_input_started\":%s,\"trick_landing_boost\":%s,\"speed_before_brake\":%.5f,\"speed_after_brake\":%.5f,\"speed_before_boost\":%.5f,\"speed_after_boost\":%.5f,\"heap_free\":[%u,%u,%u,%u],\"steps\":%u,\"mean_step_us\":%.3f,\"max_step_us\":%.3f}\n",pass?"true":"false",valid?"true":"false",ground?"true":"false",drift?"true":"false",wall?"true":"false",trickStarted?"true":"false",trickBoost?"true":"false",speedBefore,speedAfter,boostBefore,boostAfter,freeBytes[0],freeBytes[1],freeBytes[2],freeBytes[3],steps,total/steps,worst);fclose(f);
    }
    return pass;
}
}
