#pragma once
namespace mkw {
// Persisted race settings, independent of menu pages and navigation.
inline constexpr unsigned SavedVsRaceCounts[]={2,3,4,5,8,10,12,16,32};
struct SavedRaceSettings {
 unsigned license=0,driver=0,vehicle=0;
 bool automatic=false;
 unsigned vsClass=2,vsCpu=1,vsVehicles=2,vsCourses=0,vsItems=0,vsRaces=4;
 bool operator==(const SavedRaceSettings&)const=default;
};
}
