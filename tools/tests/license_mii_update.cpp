#include <mkw/license_identity.hpp>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

// Compile license_mii_store.cpp separately with -Drename=miiTestRename.
// Exercise a filesystem that rejects replacing an existing destination and
// inject failures between backup rotation and publication.
static int failPublication=0;
static bool failRestore=false;
extern "C" int miiTestRename(const char* from,const char* to){
 const std::string source(from);
 if((source.ends_with(".tmp")&&failPublication>0&&--failPublication>=0)||
    (source.ends_with(".bak")&&failRestore)){errno=EIO;return -1;}
 if(std::filesystem::exists(to)){errno=EEXIST;return -1;}
 std::error_code ec;std::filesystem::rename(from,to,ec);
 if(ec){errno=ec.value();return -1;}return 0;
}
int main(){
 char root[]="/tmp/mkw-mii-update-XXXXXX";assert(mkdtemp(root));
 mkw::NativeMii original{};original[0]=3;original[4]=17;original[12]=0x90;
 const auto id=mkw::licenseMiiId(original);
 assert(mkw::writeNativeMii(root,original));
 const auto file=std::filesystem::directory_iterator(root)->path();
 auto edited=original;edited[50]=0xff;edited[51]=0x7f; // opaque facial bytes
 assert(mkw::licenseMiiId(edited)==id);
 assert(mkw::writeNativeMii(root,edited));
 mkw::NativeMii loaded{};assert(mkw::loadNativeMii(root,id,loaded)&&loaded==edited);
 auto second=edited;second[52]=0x81;
 assert(mkw::writeNativeMii(root,second));
 assert(mkw::loadNativeMii(root,id,loaded)&&loaded==second);
 failPublication=1;
 assert(!mkw::writeNativeMii(root,original));
 assert(mkw::loadNativeMii(root,id,loaded)&&loaded==second);
 failPublication=1;failRestore=true;
 assert(!mkw::writeNativeMii(root,original));
 assert(!std::filesystem::exists(file));
 assert(mkw::loadNativeMii(root,id,loaded)&&loaded==second);
 failRestore=false;failPublication=0;
 assert(mkw::writeNativeMii(root,second)); // recover primary, not backup-only no-op
 assert(std::filesystem::exists(file));
 {std::ofstream f(file,std::ios::binary|std::ios::trunc);f.put('x');}
 assert(mkw::loadNativeMii(root,id,loaded)&&loaded==second);
 assert(mkw::writeNativeMii(root,edited));
 assert(mkw::loadNativeMii(root,id,loaded)&&loaded==edited);
 auto newIdentity=original;newIdentity[4]=18;
 assert(mkw::writeNativeMii(root,newIdentity));
 assert(mkw::loadNativeMii(root,mkw::licenseMiiId(newIdentity),loaded)&&loaded==newIdentity);
 std::filesystem::remove_all(root);
 puts("Mii update: same-ID edits, no-replace rename, failed publication/rollback, backup recovery, new identity passed");
}
