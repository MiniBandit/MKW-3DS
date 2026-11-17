#include <mkw/qa.hpp>
#include <game/system/KPadDirector.hh>
#include <game/system/RaceManager.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/Status.hh>
#include <chrono>
#include <cmath>
#include <cstdio>
namespace mkw {
namespace K=Kinoko;
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
