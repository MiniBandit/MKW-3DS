#include <mkw/license_identity.hpp>
#include <mkw/license_name_characters.hpp>
#include <3ds.h>
#include <algorithm>
#include <cstring>

namespace mkw {
namespace {
SwkbdCallbackResult filterName(void*,const char** message,const char* text,size_t){
 std::array<std::uint16_t,32> units{};
 const auto count=utf8_to_utf16(units.data(),reinterpret_cast<const std::uint8_t*>(text),units.size());
 if(count<=0||count>10){*message="Use 1 to 10 characters.";return SWKBD_CALLBACK_CONTINUE;}
 for(unsigned i=0;i<unsigned(count);++i)if(!licenseNameCharacter(units[i])){
  *message="This character is not supported by the game's font.";
  return SWKBD_CALLBACK_CONTINUE;
 }
 return SWKBD_CALLBACK_OK;
}
}
static IdentitySelection chooseNativeMii(MiiSelectorReturn& selected,const char* title="Choose a Mii for this license"){
 static_assert(sizeof(MiiData)==0x5c);
 MiiSelectorConf config{};
 miiSelectorInit(&config);
 miiSelectorSetTitle(&config,title);
 miiSelectorSetOptions(&config,MIISELECTOR_CANCEL|MIISELECTOR_GUESTS);
 selected={};selected.no_mii_selected=1;
 miiSelectorLaunch(&config,&selected);
 if(selected.no_mii_selected)return IdentitySelection::Cancelled;
 if(!miiSelectorChecksumIsValid(&selected)||selected.mii.version!=3)return IdentitySelection::Failed;

 return IdentitySelection::Accepted;
}
IdentitySelection selectLicenseMii(NativeMii& result){
 MiiSelectorReturn selected{};
 const auto chosen=chooseNativeMii(selected);
 if(chosen==IdentitySelection::Accepted)std::memcpy(result.data(),&selected.mii,sizeof selected.mii);
 return chosen;
}
IdentitySelection selectRaceMii(NativeMii& result){
 MiiSelectorReturn selected{};
 const auto chosen=chooseNativeMii(selected,"Choose a Mii driver");
 if(chosen==IdentitySelection::Accepted)std::memcpy(result.data(),&selected.mii,sizeof selected.mii);
 return chosen;
}
IdentitySelection selectLicenseIdentity(LicenseIdentity& result){
 MiiSelectorReturn selected{};
 const auto chosen=chooseNativeMii(selected);
 if(chosen!=IdentitySelection::Accepted)return chosen;
 LicenseIdentity candidate;
 std::memcpy(candidate.nativeMii.data(),&selected.mii,sizeof selected.mii);
 // Suggest the explicitly selected Mii name, never the system nickname.
 char initial[64]{};
 miiSelectorReturnGetName(&selected,initial,sizeof initial);
 SwkbdState keyboard{};
 swkbdInit(&keyboard,SWKBD_TYPE_NORMAL,2,10);
 swkbdSetHintText(&keyboard,"License name");
 swkbdSetInitialText(&keyboard,initial);
 swkbdSetValidation(&keyboard,SWKBD_NOTEMPTY_NOTBLANK,SWKBD_FILTER_CALLBACK,0);
 swkbdSetFilterCallback(&keyboard,filterName,nullptr);
 swkbdSetButton(&keyboard,SWKBD_BUTTON_LEFT,"Cancel",false);
 swkbdSetButton(&keyboard,SWKBD_BUTTON_RIGHT,"OK",true);
 char text[64]{};
 const auto button=swkbdInputText(&keyboard,text,sizeof text);
 if(button==SWKBD_BUTTON_LEFT)return IdentitySelection::Cancelled;
 if(button!=SWKBD_BUTTON_RIGHT)return IdentitySelection::Failed;
 std::array<std::uint16_t,32> units{};
 const auto count=utf8_to_utf16(units.data(),reinterpret_cast<const std::uint8_t*>(text),units.size());
 if(count<=0||count>10)return IdentitySelection::Failed;
 for(unsigned i=0;i<unsigned(count);++i)if(!licenseNameCharacter(units[i]))return IdentitySelection::Failed;
 std::copy_n(units.begin(),count,candidate.name.begin());
 result=candidate;
 return IdentitySelection::Accepted;
}
}
