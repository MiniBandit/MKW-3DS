#include <mkw/battle_rules.hpp>
#include <mkw/balloon_battle.hpp>
#include <game/kart/BattleRecovery.hh>
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace mkw;

int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    assert(bytes.size() == 0x718);
    constexpr std::array<unsigned,10> initial{40,40,28,42,30,21,30,30,30,28};
    constexpr std::array<unsigned,10> maximum{71,67,55,71,55,53,57,57,63,55};
    for (unsigned arena=0; arena<10; ++arena) {
        for (auto mode : {BattleMode::Balloon, BattleMode::Coin}) {
            BattleRules rules;
            assert(rules.load(bytes.data(),bytes.size(),32+arena,12,mode));
            assert(rules.durationTicks==10800 && rules.initialCoins==initial[arena] && rules.maxCoins==maximum[arena]);
            assert(rules.enemyHit==1 && rules.enemyDefeat==0 && rules.allBalloonsLost==-1);
            assert(rules.hitByEnemy==0 && rules.teammateHit==0 && rules.hitByTeammate==0);
        }
    }
    BattleRules rules;
    assert(rules.load(bytes.data(),bytes.size(),32,2,BattleMode::Coin));
    assert(rules.initialCoins==12 && rules.maxCoins==21); // source player-count column
    assert(rules.coinsLost(0)==0 && rules.coinsLost(2)==2 && rules.coinsLost(5)==3);
    assert(rules.coinsLost(7)==4 && rules.coinsLost(20)==10 && rules.coinsLost(99)==50);
    assert(rules.coinsLost(20,true)==3 && rules.coinsLost(2,true)==2);

    // Non-stock fixture proves selected arena/mode/count and signed score
    // values are consumed, rather than replacing KMG with hardcoded defaults.
    auto modified=bytes;
    auto put=[&](size_t at,int value){modified[at]=uint16_t(value)>>8;modified[at+1]=uint8_t(value);};
    put(0x1dc+3*22+4,95);put(0x1c8,2);put(0x1cc,-4);put(0x716,25);
    assert(rules.load(modified.data(),modified.size(),35,4,BattleMode::Coin));
    assert(rules.durationTicks==5700 && rules.enemyHit==2 && rules.allBalloonsLost==-4 && rules.coinsLost(20)==5);
    put(0x712,9);put(0x714,2);
    assert(rules.load(modified.data(),modified.size(),35,4,BattleMode::Coin));
    assert(rules.coinsLost(20)==9 && rules.coinsLost(20,true)==2 && rules.coinStealCount==2);
    assert(!rules.load(bytes.data(),bytes.size()-1,32,12,BattleMode::Coin));
    assert(!rules.load(bytes.data(),bytes.size(),31,12,BattleMode::Coin));
    assert(!rules.load(bytes.data(),bytes.size(),42,12,BattleMode::Coin));
    assert(!rules.load(bytes.data(),bytes.size(),32,1,BattleMode::Coin));
    assert(!rules.load(bytes.data(),bytes.size(),32,13,BattleMode::Coin));
    modified[0]='X';assert(!rules.load(modified.data(),modified.size(),35,4,BattleMode::Coin));
    assert(rules.durationTicks==5700 && rules.enemyHit==2); // failed load is atomic

    assert(rules.load(bytes.data(),bytes.size(),32,12,BattleMode::Balloon));
    std::array<RaceTeam,12> teams;for(unsigned i=0;i<12;++i)teams[i]=RaceTeam(i&1);
    BalloonBattle battle;battle.configure(rules,teams);battle.reset(12);
    for(unsigned i=0;i<8;++i){
        if(!battle.balloons(1))assert(battle.replenish(1));
        assert(battle.loseBalloon(1,0));
    }
    assert(battle.score(0)==8);
    for(unsigned i=0;i<3;++i)assert(battle.loseBalloon(0,1));
    assert(battle.score(0)==7 && battle.score(1)==3); // regression: never halve score
    assert(battle.balloons(0)==0 && !battle.loseBalloon(0,1));
    assert(battle.replenish(0) && battle.balloons(0)==3 && !battle.replenish(0));
    unsigned before=battle.score(0);assert(battle.loseBalloon(2,0));assert(battle.score(0)==before);
    assert(battle.balloons(2)==2); // accepted friendly hit can remove a balloon, but gives no enemy point
    assert(!battle.stealBalloon(0,2) && battle.balloons(2)==2); // transfers reject teammates
    assert(battle.loseBalloon(3,2) && battle.score(2)==1);
    assert(battle.loseBalloon(2,0) && battle.loseBalloon(2,0));
    assert(battle.balloons(2)==0 && battle.score(2)==1); // no last-balloon penalty for friendly hits
    assert(battle.durationTicks()==10800);
    assert(battle.stealBalloon(0,1) && battle.balloons(0)==4);
    assert(battle.replenish(1));
    assert(battle.stealBalloon(0,1) && battle.balloons(0)==5);
    assert(battle.stealBalloon(0,1) && battle.balloons(0)==5 && battle.balloons(1)==1);
    assert(!battle.stealBalloon(0,0) && !battle.stealBalloon(12,0));

    using Recovery=Kinoko::Kart::BattleRecovery;
    Recovery recovery;
    assert(recovery.visible() && !recovery.protectedFromDamage());
    recovery.empty();
    // Rescue update suspension is supplied by KartMove. Eligible
    // refill ticks pause independently from the protection clock.
    for(unsigned i=0;i<50;++i)assert(recovery.step(false)==Recovery::Event::None);
    for(unsigned i=0;i<100;++i)assert(recovery.step(true)==Recovery::Event::None);
    assert(!recovery.protectedFromDamage() && recovery.emptyPending());
    for(unsigned i=0;i<49;++i)assert(recovery.step(true)==Recovery::Event::None);
    assert(recovery.step(true)==Recovery::Event::Vanish);
    assert(recovery.scale()==1.f);
    for(unsigned i=0;i<19;++i)assert(recovery.step(true)==Recovery::Event::None);
    assert(recovery.step(true)==Recovery::Event::Hidden);
    assert(recovery.hidden() && !recovery.visible() && recovery.scale()==.001f);
    // The 200 ticks run from entering the vanish wait, including shrinking.
    for(unsigned i=20;i<199;++i)assert(recovery.step(true)==(i==110?Recovery::Event::Wipe:Recovery::Event::None));
    assert(recovery.step(true)==Recovery::Event::Respawn);
    recovery.reset();assert(recovery.visible() && !recovery.emptyPending() && recovery.scale()==1.f);
    recovery.protect();
    for(unsigned i=0;i<19;++i){recovery.step(true);assert(!recovery.visible());}
    recovery.step(true);assert(recovery.visible());
    for(unsigned i=20;i<150;++i)recovery.step(true);
    assert(!recovery.protectedFromDamage() && recovery.visible() && !recovery.emptyPending());
    puts("PASS KMG tables and coin losses, signed scoring, no immediate refill, steals/max-five, independent protection/refill clocks, vanish/hidden/200-tick rescue transition and reset");
}
