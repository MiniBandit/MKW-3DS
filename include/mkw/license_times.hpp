#pragma once
#include <array>
#include <cstdint>
namespace mkw {
using CourseBestTimes=std::array<std::uint32_t,32>;
// Legacy native storage, read only by the main program for migration.
// Active saves are now stored in retail rksys.dat by license_save.
bool loadLicenseTimes(const char* directory,unsigned license,CourseBestTimes& times);
bool saveLicenseTimes(const char* directory,unsigned license,const CourseBestTimes& times);
}
