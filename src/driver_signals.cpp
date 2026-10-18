#include <mkw/driver_signals.hpp>
#include <mkw/race_items.hpp>
#include <mkw/blooper.hpp>
#include <game/kart/KartObject.hh>
#include <game/kart/KartMove.hh>
#include <game/kart/KartJump.hh>
#include <game/kart/KartHalfPipe.hh>
#include <game/kart/KartState.hh>
#include <game/system/RaceConfig.hh>
#include <game/render/KartModel.hh>

namespace mkw {
namespace K=Kinoko;
DriverSignals DriverSignalReader::read(const K::Kart::KartObject& kart){
    using St=K::Kart::eStatus;
    const auto& st=kart.status();
    const auto* move=kart.move();
    DriverSignals s;
    s.accelerate=st.onBit(St::Accelerate);s.brake=st.onBit(St::Brake);
    s.drifting=st.onBit(St::DriftManual,St::DriftAuto);
    s.beforeRespawn=st.onBit(St::BeforeRespawn);
    s.anyWheel=st.onBit(St::AnyWheelCollision);s.allWheels=st.onBit(St::AllWheelsCollision);
    s.stickLeft=st.onBit(St::StickLeft);s.stickRight=st.onBit(St::StickRight);
    s.touchingGround=st.onBit(St::TouchingGround);s.hop=st.onBit(St::Hop);
    s.boost=st.onBit(St::Boost);s.rampBoost=st.onBit(St::RampBoost);s.wheelie=st.onBit(St::Wheelie);
    s.inAction=st.onBit(St::InAction);s.inCannon=st.onBit(St::InCannon);
    s.inATrick=st.onBit(St::InATrick);s.overZipper=st.onBit(St::OverZipper);
    s.burnout=st.onBit(St::Burnout);s.chargingSsmt=st.onBit(St::ChargingSSMT);
    s.slipstreamBoost=move->slipstreamBoost();
    // Bit 52: releaseMt (0x80582F9C) sets it with the mini-turbo's own timer (KartMove
    // +0x102, fn_1_72EC8) and clearBoost clears it; Kinoko does not keep the bit. The
    // AllMt boost a drift release starts runs for exactly that time.
    const bool mt=move->boostActive(K::Kart::KartBoost::Type::AllMt);
    const bool charged=move->driftState()>=K::Kart::KartMove::DriftState::ChargedMt;
    if(!mt)mtBoost_=false;else if(wasCharged_&&!s.drifting)mtBoost_=true;
    wasCharged_=charged&&s.drifting;s.mtBoost=mtBoost_;
    s.frozen=K::System::RaceConfig::Instance()->raceScenario().course==K::Course::N64_Sherbet_Land&&st.onBit(St::InRespawn,St::AfterRespawn);
    s.speed=move->speed();s.speedRatio=move->speedRatioCapped();
    s.hopStickX=move->hopStickX();s.appliedHopStickX=move->getAppliedHopStickX();
    s.steering=kart.model()->steeringInput();
    const auto& input=kart.inputs()->currentState();
    // AI::getStick (0x80726204): a CPU (or the player after its goal) steers by body
    // rotation with a centred pad; its driver model reads the AI's animation stick.
    s.stickX=move->cpuSteer()?move->cpuStick():input.stick.x;s.accelButton=input.accelerate();
    const bool zipperTrick=st.onBit(St::ZipperTrick);
    if(s.inATrick&&!wasTrick_){s.trickVariant=int(move->jump()->variant());s.trickDirection=int(move->jump()->nextTrick());}
    else if(zipperTrick&&!wasZipperTrick_){s.trickVariant=2;s.trickDirection=int(move->halfPipe()->trick());}
    wasTrick_=s.inATrick;wasZipperTrick_=zipperTrick;
    return s;
}
void readItemEvents(DriverSignals& s,const RaceItems& items,unsigned player){
    for(const auto& e:items.sounds())
        if(e.cue==ItemSoundCue::Release&&e.player==player)s.itemThrow=e.flag?1:-1;
    for(const auto& e:items.events())
        if(e.action>=0&&e.source==player&&e.player!=player)s.gut=true;
}
int inkFrames(const RaceItems& items,unsigned player){
    const unsigned ink=items.hud(player).ink;
    return ink?int(BlooperInkDuration-std::min(ink,BlooperInkDuration)):-1;
}
}
