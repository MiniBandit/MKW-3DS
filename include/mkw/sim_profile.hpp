#pragma once
// DIAG physics breakdown (Old 3DS work, Nov 6): where one physics tick's time
// goes. Scopes inside Kinoko's RaceScene::calcEngines and the kart step add
// system ticks to fixed slots; main logs per-tick averages ("sim profile")
// every 600 ticks of racing. Off (no clock reads) until data.on is set.
// Off the 3DS (host Kinoko builds) every scope is empty.
#if defined(__3DS__)
extern "C" { // <3ds.h> adds this; these two headers alone do not
#include <3ds/types.h>
#include <3ds/svc.h>
}
#endif
namespace mkw::simprof {
enum Slot : unsigned {
    RaceMgr, BoxCol, Objects, Karts, Jugem, Items,           // RaceScene::calcEngines
    K0State, K0Move, K0Action, K0Physics, K0Hitboxes,        // KartSub::calcPass0
    K1Total, K1Search, K1KartCol, K1ObjCol, K1ItemCol, K1Body, K1Wheels, // KartSub::calcPass1
    K1Model,                                                 // KartObject::calc: model()->calc()
    Kinoko,                                                  // main: scenes.calc()
    PortItemsBefore, PortAi, PortItemsAfter, PortCamera, PortLakitu, PortAudio, // main: the port's work around it
    TickTotal,                                               // main: the tick's logic (to the step-time stamp)
    PresPlayer, PresOpponents, PresKartFx, PresParticles, PostTick, // main: presentation in the same tick, after it
    OppVehicle, OppController, OppLook,                      // RacerVisuals::step, all 11 opponents
    FxInput, FxKart, FxLanding, FxWheelSpin, FxBrake, FxRemote, FxSurface, FxOppDrive, // pres_kart_fx split
    ItGrid, ItSnapshot, ItActors, ItHolders,                 // port_items_after split
    AuFill, AuStep,                                          // port_audio split
    EfSparks, EfBuild, EfSubmit, EfFinish, EfBloom,          // render: the effects stage (per drawn frame, shown per tick)
    Count
};
inline constexpr const char* Names[Count]={
    "race_manager","box_col","objects","karts","jugem","items",
    "k0_state","k0_move","k0_action","k0_physics","k0_hitboxes",
    "k1_total","k1_box_search","k1_kart_col","k1_object_col","k1_item_col","k1_body_col","k1_wheels",
    "k1_model","kinoko",
    "port_items_before","port_ai","port_items_after","port_camera","port_lakitu","port_audio","tick_total",
    "pres_player","pres_opponents","pres_kart_fx","pres_particles","post_tick",
    "opp_vehicle","opp_controller","opp_look",
    "fx_input","fx_kart","fx_landing","fx_wheelspin","fx_brake","fx_remote","fx_surface","fx_oppdrive",
    "it_grid","it_snapshot","it_actors","it_holders","au_fill","au_step",
    "ef_sparks","ef_build","ef_submit","ef_finish","ef_bloom"};
struct Data {unsigned long long ticks[Count]{};unsigned steps=0;bool on=false;};
inline Data data{};
#if defined(__3DS__)
struct Scope {
    Slot slot;u64 start;
    explicit Scope(Slot s):slot(s),start(data.on?svcGetSystemTick():0){}
    ~Scope(){if(data.on)data.ticks[slot]+=svcGetSystemTick()-start;}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
// Manual start/stop for blocks a scope can't wrap.
inline u64 now(){return data.on?svcGetSystemTick():0;}
inline void add(Slot s,u64 start){if(data.on&&start)data.ticks[s]+=svcGetSystemTick()-start;}
#else
struct Scope {explicit Scope(Slot){}};
inline unsigned long long now(){return 0;}
inline void add(Slot,unsigned long long){}
#endif
}
#define MKW_SIM_SCOPE(slot) ::mkw::simprof::Scope mkwSimScope_##slot(::mkw::simprof::slot)
