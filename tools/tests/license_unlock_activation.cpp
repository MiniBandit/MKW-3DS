#include <mkw/license_save.hpp>
#include <cassert>
#include <mkw/menu_unlock_texts.hpp>
#include <cstdio>
#include <chrono>
#include <filesystem>
using namespace mkw;
int main(){
 const auto dir=std::filesystem::temp_directory_path()/("mkw-unlock-activation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 assert(std::filesystem::create_directory(dir));const auto path=dir.string();const char* root=path.c_str();
 LicenseSave save;save.occupied=true;save.settings.license=0;
 save.cupResult(0,0,1,-1);save.cupResult(0,1,1,-1);save.cupResult(0,2,1,-1);
 assert(writeLicenseSave(root,0,save));
 const auto initial=save.unlockFlags;
 LicenseUnlockNotice notice;notice.completionBit=127;notice.messageId=123;
 const auto missing=std::string(root)+"/missing";
 assert(activateNextLicenseUnlock(missing.c_str(),0,save,notice)==LicenseUnlockActivation::SaveFailed);
 assert(save.unlockFlags==initial&&notice.completionBit==127&&notice.messageId==123);
 assert(activateNextLicenseUnlock(root,1,save,notice)==LicenseUnlockActivation::SaveFailed);
 assert(save.unlockFlags==initial&&notice.completionBit==127);
 assert(activateNextLicenseUnlock(root,0,save,notice)==LicenseUnlockActivation::Ready);
 assert(notice.condition==0&&notice.completionBit==14);
 assert(licenseCompleted(save.unlockFlags,14)&&!licenseCompleted(save.unlockFlags,18)&&!licenseCompleted(save.unlockFlags,9));
 LicenseSave loaded;assert(loadLicenseSave(root,0,loaded)==LicenseLoad::Loaded);
 assert(loaded.unlockFlags==save.unlockFlags);
 assert(loaded.availableUnlocks().front().condition==4);
 LicenseSave other;other.occupied=true;other.settings.license=1;other.cups=save.cups;
 assert(writeLicenseSave(root,1,other));
 assert(activateNextLicenseUnlock(root,1,other,notice)==LicenseUnlockActivation::Ready);
 assert(notice.condition==0);
 LicenseSave retained;assert(loadLicenseSave(root,0,retained)==LicenseLoad::Loaded);
 assert(retained.unlockFlags==loaded.unlockFlags);
 assert(activateNextLicenseUnlock(root,0,loaded,notice)==LicenseUnlockActivation::Ready);assert(notice.condition==4);
 assert(activateNextLicenseUnlock(root,0,loaded,notice)==LicenseUnlockActivation::Ready);assert(notice.condition==31);
 const auto last=notice.completionBit;
 assert(activateNextLicenseUnlock(root,0,loaded,notice)==LicenseUnlockActivation::None);assert(notice.completionBit==last);
 assert(loadLicenseSave(root,0,retained)==LicenseLoad::Loaded);assert(retained.availableUnlocks().empty());
 LicenseSave star150;star150.occupied=true;star150.settings.license=2;star150.cupResult(2,2,1,-1);
 assert(writeLicenseSave(root,2,star150));
 for(unsigned expected: {6u,47u}){
  assert(activateNextLicenseUnlock(root,2,star150,notice)==LicenseUnlockActivation::Ready);
  assert(notice.condition==expected);
  bool visible=false;for(const auto& row:UiUnlockTexts)if(row.condition==notice.condition&&row.bit==notice.completionBit)visible=true;
  assert(visible);
  assert(loadLicenseSave(root,2,retained)==LicenseLoad::Loaded);
  assert(retained.unlockFlags==star150.unlockFlags);
 }
 assert(licenseVehicleUnlocked(retained.unlockFlags,27)); // small Quacker
 assert(activateNextLicenseUnlock(root,2,star150,notice)==LicenseUnlockActivation::None);
 star150.cupResult(2,3,1,-1);
 assert(activateNextLicenseUnlock(root,2,star150,notice)==LicenseUnlockActivation::Ready);
 assert(notice.condition==24&&notice.completionBit==6); // Daisy remains independent
 std::filesystem::remove_all(root);
 puts("Unlock activation: real RKSD save/reload, one reward per activation, retry after write failure, wrong-slot rejection and slot isolation passed");
}
