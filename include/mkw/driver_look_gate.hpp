#pragma once
#include <cstring>
#include <initializer_list>
namespace mkw {
// USA807c0454..807c04a4 and animation descriptors at 808a1bb8 (24-byte
// stride). Base states 0..7 except 6(back); states 8/9 require driver +100.
// +100 is the Mii-specific animation object: 807b8e70 calls 80589e68 ->
// 80523178 (character IDs 24..44). All current roster IDs 0..23 leave it null.
// Do not enable this exception merely because a normal driver has finished.
inline bool driverLookClipAllowed(const char* clip,bool resultLookEnabled=false){
 if(!clip)return false;
 for(const char* allowed:{"drive","dash","drift_l","drift_r","wheelie","wait2","wait"})
  if(std::strcmp(clip,allowed)==0)return true;
 return resultLookEnabled&&(std::strcmp(clip,"1st")==0||std::strcmp(clip,"good")==0);
}
// USA807c04b8..04c8 reads KartState+8: the second status word. 0x440
// is bit 38 (InATrick) and bit 42 (OverZipper), not hit or cannon state.
inline bool driverLookAllowed(const char* clip,bool inATrick,bool overZipper,bool resultLookEnabled=false){
 return !inATrick&&!overZipper&&driverLookClipAllowed(clip,resultLookEnabled);
}

}
