#include <mkw/coin_presentation.hpp>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
namespace mkw::pack {bool read(const char*,std::vector<uint8_t>&){return false;}}
using namespace mkw;
namespace {std::vector<uint8_t> read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);assert(f);return {(std::istreambuf_iterator<char>(f)),{}};}}
int main(int argc,char**argv){
    assert(argc==3);const std::filesystem::path assets=argv[1];
    auto kmg=read(assets/"common/minigame.kmg"),kmp=read(assets/"courses/block_battle/course.kmp");BattleRules rules;
    assert(rules.load(kmg.data(),kmg.size(),33,12,BattleMode::Coin));std::array<RaceTeam,12> teams{};teams[1]=RaceTeam::Blue;
    CoinBattle battle;assert(battle.load(kmp.data(),kmp.size(),rules,12,teams));battle.assignWaves();battle.activate(0);
    auto effects=std::make_unique<RaceEffects>();assert(effects->load(argv[2]));
    CoinPresentation view;view.reset(battle.coins().size());std::array<Vec3,12> owners{};owners[0]={100,1000,-500};owners[1]={600,1000,-500};
    std::vector<uint8_t> visible(battle.coins().size(),1);
    for(unsigned i=0;i<3;++i)assert(battle.collect(i,0));
    auto step=[&]{battle.stepDropped(nullptr,33);view.step(battle.coins(),33,owners,visible,*effects);effects->step();
        for(unsigned i=0;i<battle.coins().size();++i)battle.setHeldReady(i,view.heldReady(i));};
    for(unsigned t=0;t<60;++t)step();assert(battle.readyHeld(0)==3);
    view.takeArrivalCues(); // discard the preceding ordinary acquisitions
    assert(battle.requestSteal(1,0)&&battle.stepRequests());assert(battle.readyHeld(0)==0&&battle.readyHeld(1)==0);
    for(unsigned i=0;i<3;++i){const auto& c=battle.coins()[i];assert(c.pickupStartDelay==i*6&&c.pickupTailDelay==(2-i)*6);}
    unsigned cues=0;Vec3 waiting{};
    for(unsigned t=0;t<64;++t){
        owners[1].x+=1;if(t==6)waiting=view.unit(1).position;
        step();const auto cue=view.takeArrivalCues();
        if(t==0){assert(view.unit(0).model&&!view.unit(1).model&&!view.unit(2).model);}
        if(t==6){assert(view.unit(1).model&&!view.unit(2).model);
            assert(std::abs(view.unit(1).pickupOffset.x-(waiting.x-owners[1].x))<.001f);}
        if(t==12)assert(view.unit(2).model);
        if(t==45||t==51||t==57){assert(cue==2);++cues;}else assert(!cue);
        if(t<62)assert(!view.heldReady(0)&&!view.heldReady(1)&&!view.heldReady(2)&&battle.readyHeld(1)==0);
        else assert(view.heldReady(0)&&view.heldReady(1)&&view.heldReady(2)&&battle.readyHeld(1)==3);
    }
    assert(cues==3);effects->unload();
    std::puts("PASS actual Coin bank/pool transfer: native6-update stagger, owner captured at delayed entry,45/51/57 cues, shared63-update settled count and no premature score. No target audio/visual acceptance.");
}
