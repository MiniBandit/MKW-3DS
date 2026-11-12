#pragma once
// DIAG Kinoko scene load breakdown (Nov 12): scopes in Kinoko's archive loads
// and RaceScene's engine setup add system ticks to fixed slots while data.on
// (main sets it around the "kinoko scene" stage and logs "kinoko scene
// profile"). Release and host builds: every scope is empty.
#if defined(__3DS__)&&!defined(MKW_RELEASE)
extern "C" {
#include <3ds/types.h>
#include <3ds/svc.h>
}
#define MKW_LOAD_PROFILE 1
#endif
namespace mkw::loadprof {
enum Slot : unsigned {
    Read, Decode, Mount,
    CreateCamera, CreateCourseMap, CreateRaceManager, CreateBoxCol, CreateKarts, CreateCollision, CreateItems, CreateObjects,
    InitKarts, InitCamera, InitRaceManager, InitItems, InitObjects,
    Count
};
inline constexpr const char* Names[Count]={"read","yaz0","mount",
    "new_camera","new_course_map","new_race_manager","new_box_col","new_karts","new_collision","new_items","new_objects",
    "init_karts","init_camera","init_race_manager","init_items","init_objects"};
struct Data {unsigned long long ticks[Count]{};unsigned long long decodeIn=0,decodeOut=0;bool on=false;};
inline Data data{};
#ifdef MKW_LOAD_PROFILE
struct Scope {
    Slot slot;u64 start;
    explicit Scope(Slot s):slot(s),start(data.on?svcGetSystemTick():0){}
    ~Scope(){if(data.on)data.ticks[slot]+=svcGetSystemTick()-start;}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
#else
struct Scope {explicit Scope(Slot){}};
#endif
}
#define MKW_LOAD_SCOPE(slot) ::mkw::loadprof::Scope mkwLoadScope_##slot(::mkw::loadprof::slot)
