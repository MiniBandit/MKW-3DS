#pragma once
#include <array>
#include <cstdint>
namespace mkw {
// Retain observations separately so the records UI need not infer history
// from best times, which lose repeated plays and character/vehicle identity.
struct LicenseRecords {
 std::array<std::uint32_t,3> finishes{}; // GP, VS, Time Trials
 std::array<std::uint32_t,2> firstPlaces{}; // GP, VS (solo TT is not a win)
 std::array<std::uint32_t,24> characters{};
 std::array<std::uint32_t,36> vehicles{}; // retail vehicle IDs, including weight
 std::array<std::uint32_t,32> courses{}; // port course catalog order
 // Retail record fields retained independently of the native race observations.
 std::uint32_t battles=0,wheelRaces=0,wheelBattles=0,tricks=0;
 std::uint32_t hitsDelivered=0,hitsReceived=0,ghostWins=0,ghostLosses=0;
 std::uint16_t tournaments=0;
 std::array<std::uint32_t,10> battleStages{}; // menu stage order
 bool finish(unsigned mode,unsigned character,unsigned retailVehicle,unsigned course,unsigned rank);
 bool operator==(const LicenseRecords&)const=default;
};
bool loadLicenseRecords(const char* directory,unsigned slot,LicenseRecords& records);
bool saveLicenseRecords(const char* directory,unsigned slot,const LicenseRecords& records);
}
