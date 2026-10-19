#include <mkw/license_identity.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <cstdio>
int main(){
 char root[]="/tmp/mkw-mii-store-XXXXXX";assert(mkdtemp(root));
 mkw::NativeMii a{};a[0]=3;a[4]=0x11;a[5]=0x22;a[6]=0x33;a[7]=0x44;a[12]=0x90;
 const mkw::LicenseMiiId expected={0x81,0xe1,0x87,0xe0,0x11,0x22,0x33,0x44};
 assert(mkw::licenseMiiId(a)==expected);
 a[15]=2;auto next=mkw::licenseMiiId(a);assert(next[3]==0xe1);
 a[12]=0x10;assert(mkw::licenseMiiId(a)[0]==0x41);a[12]=0x90;
 for(unsigned i=24;i<a.size();++i)a[i]=i*7;
 assert(mkw::writeNativeMii(root,a));mkw::NativeMii b{};
 assert(mkw::loadNativeMii(root,mkw::licenseMiiId(a),b)&&a==b);
 assert(mkw::writeNativeMii(root,a));
 auto other=mkw::licenseMiiId(a);other[7]^=1;assert(!mkw::loadNativeMii(root,other,b)&&b==a);
 auto file=*std::filesystem::directory_iterator(root);
 {std::fstream f(file.path(),std::ios::binary|std::ios::in|std::ios::out);f.seekp(40);char wrong=0;f.write(&wrong,1);}
 assert(!mkw::loadNativeMii(root,mkw::licenseMiiId(a),b)&&b==a);
 assert(mkw::writeNativeMii(root,a));
 {std::ofstream f(file.path(),std::ios::binary|std::ios::app);f.put('x');}
 assert(!mkw::loadNativeMii(root,mkw::licenseMiiId(a),b)&&b==a);
 a[0]=0;assert(!mkw::writeNativeMii(root,a));
 std::filesystem::remove_all(root);
 puts("Native Mii: epoch conversion, full appearance roundtrip, wrong ID, corruption, trailing bytes and invalid version passed");
}
