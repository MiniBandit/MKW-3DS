#include <mkw/mii_store.hpp>
#include <cassert>
#include <cstring>
int main(){
 using namespace mkw;
 const char text[]=
  "# comment\n"
  "mario_1|Mario|custom|miis/mario_1.t3x|\n"
  "  jake | Jake |console| miis/jake.t3x | 0A0b0C\r\n"
  "bad id|Nope|custom||\n"          // space in the id
  "noname||custom||\n"              // empty name
  "wrongsrc|X|gamecube||\n"         // unknown source
  "oddhex|X|wii||ABC\n"             // odd hex length
  "badhex|X|wii||ZZ\n"              // not hex
  "short|Short|wii\n"               // three fields are enough
  "toolong|123456789012345678901234567890|custom||\n";
 MiiEntry entries[8];
 const unsigned count=parseMiiManifest(text,sizeof text-1,entries,8);
 assert(count==4);
 assert(!std::strcmp(entries[0].id,"mario_1")&&!std::strcmp(entries[0].name,"Mario")&&entries[0].source==MiiEntry::Custom);
 assert(!std::strcmp(entries[0].portrait,"miis/mario_1.t3x")&&entries[0].dataBytes==0);
 assert(!std::strcmp(entries[1].id,"jake")&&entries[1].source==MiiEntry::Console&&entries[1].dataBytes==3);
 assert(entries[1].data[0]==0x0a&&entries[1].data[1]==0x0b&&entries[1].data[2]==0x0c);
 assert(!std::strcmp(entries[2].id,"short")&&entries[2].source==MiiEntry::WiiSave&&!entries[2].portrait[0]);
 assert(std::strlen(entries[3].name)==23); // truncated, terminated
 assert(parseMiiManifest(text,sizeof text-1,entries,2)==2); // capacity
 assert(parseMiiManifest(nullptr,0,entries,8)==0);
 return 0;
}
