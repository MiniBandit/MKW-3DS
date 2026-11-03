#pragma once
#include <mkw/menu_ui.hpp>
#include <mkw/license_times.hpp>
#include <mkw/license_records.hpp>
#include <mkw/license_identity.hpp>
#include <mkw/license_unlocks.hpp>
#include <vector>
namespace mkw {
struct LicenseUnlockNotice {
 unsigned completionBit=0;
 // 0..60: retail condition; 61: race-count alternative.
 unsigned condition=61;
 unsigned messageId=0;
 unsigned engineMessageArgument=0;
 unsigned raceRequirement=0;
};
struct LicenseSave {
 bool occupied=false;
 // Retail identity: ten UTF-16 code units and the eight-byte Mii ID.
 // Keep raw units so loading/saving an existing license is lossless.
 std::array<std::uint16_t,10> name{};
 std::array<std::uint8_t,8> miiId{};
 LicenseUnlockFlags unlockFlags{}; // Retail RKPD +0x30; bit n uses 1u << (n % 32).
 NativeMii nativeMii{}; // Optional companion; zero for an imported Wii license.
 UiSelection settings{};
 CourseBestTimes times{};
 LicenseRecords records{};
 // 0 = no result. Low byte = finishing place; next byte = grade + 1.
 std::array<std::uint32_t,32> cups{};
 // Ordered, deduplicated rewards; querying does not consume completion bits.
 std::vector<LicenseUnlockNotice> availableUnlocks() const;
 bool refreshUnlocks();
 void cupResult(unsigned cc,unsigned cup,unsigned place,int grade);
};
// Before license activation the menu may still contain default/transient
// selections. Never let strap/title/license browsing overwrite loaded rules.
inline bool licenseSelectionNeedsSave(const MenuState& menu,const LicenseSave& save,bool damaged){
 switch(menu.page){
 case UiPage::Strap:case UiPage::Intro:case UiPage::Title:case UiPage::License:return false;
 default:break;
 }
 return !menu.rulePage()&&save.occupied&&!damaged&&save.settings!=menu.selection;
}
enum class LicenseLoad {Missing,Loaded,Damaged};
// Retail RKSD0006 rksys.dat, shared by four licenses; unknown data is preserved.
// Startup reads and validates the shared core once before decoding all slots.
LicenseLoad loadLicenseSaves(const char* directory,std::array<LicenseSave,4>& saves);
LicenseLoad loadLicenseSave(const char* directory,unsigned slot,LicenseSave& save);
bool writeLicenseSave(const char* directory,unsigned slot,const LicenseSave& save);
enum class LicenseUnlockActivation {None,Ready,SaveFailed};
// Call when entering the retail Unlock page, not when recording a race or
// opening an unrelated menu. Only the displayed reward is consumed.
// On failure neither the in-memory flags nor the output notice changes.
LicenseUnlockActivation activateNextLicenseUnlock(const char* directory,unsigned slot,
 LicenseSave& save,LicenseUnlockNotice& notice);
}
