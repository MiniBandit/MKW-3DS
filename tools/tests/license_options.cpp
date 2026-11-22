#include <mkw/menu_transition.hpp>
#include <mkw/license_save.hpp>
#include <mkw/menu_assets.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <unistd.h>
using namespace mkw;
std::string bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(){
 char root[]="/tmp/mkw-options-XXXXXX";assert(mkdtemp(root));auto file=std::filesystem::path(root)/"rksys.dat";
 std::array<LicenseSave,4> saves{};
 for(unsigned i=0;i<4;++i){auto& s=saves[i];s.occupied=true;s.settings.license=i;s.nativeMii[0]=3;s.nativeMii[4]=i+1;s.miiId=licenseMiiId(s.nativeMii);assert(writeLicenseSave(root,i,s));}
 assert(loadLicenseSaves(root,saves)==LicenseLoad::Loaded);
 const auto retail=bytes(file);
 for(unsigned i=0;i<4;++i){auto& s=saves[i];s.settings.ledMode=i;s.settings.ledBrightness=50+10*i;s.settings.raceTimer=i%3;s.settings.mapTurns=i!=2;s.settings.speedometer=i==3;assert(writeLicenseSave(root,i,s));}
 assert(bytes(file)==retail); // Port settings must not alter any retail/ghost byte.
 std::array<LicenseSave,4> loaded;assert(loadLicenseSaves(root,loaded)==LicenseLoad::Loaded);
 for(unsigned i=0;i<4;++i){assert(loaded[i].settings.ledMode==i);assert(loaded[i].settings.ledBrightness==50+10*i);
  assert(loaded[i].settings.raceTimer==i%3&&loaded[i].settings.mapTurns==(i!=2)&&loaded[i].settings.speedometer==(i==3));}
 {// MKWO0001, 24 bytes: byte 19 holds the speedometer (bit 0), the timer (bits 1-2) and a fixed map (bit 3).
  const auto data=bytes(std::filesystem::path(root)/"license-options-2.dat");
  assert(data.size()==24&&!data.compare(0,8,"MKWO0001")&&(data[19]&8));
 }
 {// A file from before the map option (bit 3 clear) keeps the turning map.
  const auto path=std::filesystem::path(root)/"license-options-2.dat";std::string v=bytes(path).substr(0,20);v[19]&=~8;
  std::uint32_t c=~0u;for(unsigned char b:v){c^=b;for(int k=0;k<8;++k)c=c&1?(c>>1)^0xEDB88320u:c>>1;}c=~c;
  for(int k=3;k>=0;--k)v.push_back(char(c>>(8*k)&255));
  for(auto suffix:{"",".bak"})std::filesystem::remove(std::string(path)+suffix);
  {std::ofstream f(path,std::ios::binary);f<<v;}
  LicenseSave old;assert(loadLicenseSave(root,2,old)==LicenseLoad::Loaded&&old.settings.mapTurns&&old.settings.ledMode==2);
 }
 // Legacy saves and corrupt optional files retain safe defaults, not a damaged license.
 for(auto suffix:{"",".bak"})std::filesystem::remove(std::filesystem::path(root)/(std::string("license-options-0.dat")+suffix));
 assert(loadLicenseSave(root,0,loaded[0])==LicenseLoad::Loaded);assert(loaded[0].settings.ledMode==0&&loaded[0].settings.ledBrightness==100);
 {std::ofstream f(std::filesystem::path(root)/"license-options-0.dat");f<<"bad";}
 assert(loadLicenseSave(root,0,loaded[0])==LicenseLoad::Loaded&&loaded[0].settings.ledMode==0);
 LicenseSave empty;empty.settings.license=1;assert(writeLicenseSave(root,1,empty));
 LicenseSave created=saves[1];created.settings=UiSelection{};created.settings.license=1;assert(writeLicenseSave(root,1,created));
 assert(loadLicenseSave(root,1,loaded[1])==LicenseLoad::Loaded&&loaded[1].settings.ledMode==0);
 MenuState m;m.enter(UiPage::LicenseSettings);m.advance(120);assert(m.count()==7);
 UiInput key{};key.right=true;m.cursor=2;m.input(key);assert(m.selection.ledMode==1&&m.page==UiPage::LicenseSettings);
 key={};key.left=true;m.input(key);assert(m.selection.ledMode==0);
 key={};key.accept=true;m.cursor=3;m.input(key);assert(m.selection.ledBrightness==50&&m.eraseLicenseRequest<0);
 key={};key.left=true;m.input(key);assert(m.selection.ledBrightness==50);
 key={};key.right=true;m.input(key);assert(m.selection.ledBrightness==60);
 m.cursor=4;key={};key.accept=true;const bool speed=m.selection.speedometer;m.input(key);assert(m.selection.speedometer!=speed);
 // Timer: Minimal (1), Full (2), Off (0).
 m.selection.raceTimer=1;m.cursor=5;key={};key.right=true;m.input(key);assert(m.selection.raceTimer==2);m.input(key);assert(m.selection.raceTimer==0);
 key={};key.left=true;m.input(key);assert(m.selection.raceTimer==2);
 m.selection.mapTurns=true;m.cursor=6;key={};key.left=true;m.input(key);assert(!m.selection.mapTurns);key={};key.accept=true;m.input(key);assert(m.selection.mapTurns&&m.page==UiPage::LicenseSettings);
 std::vector<UiQuad> source,top,bottom;
 for(unsigned level=0;level<4;++level){m.selection.ledMode=level;composeMenu(m,{},source);layoutMenuScreens(m,source,top,bottom);
  std::set<unsigned> rows;float lowest=0;
  for(const auto& q:bottom)if(q.menu.role==UiMenuRole::Choice){rows.insert(q.menu.group);assert(q.menu.group<7&&q.x>=0&&q.y>=0&&q.x+q.w<=320.01f&&q.y+q.h<=240.01f);lowest=std::max(lowest,q.y+q.h);}
  // Every row sits above the Back bar.
  assert(rows.size()==7&&lowest<=212.f);
 }
 m.cursor=0;key={};key.accept=true;m.input(key);assert(m.editLicenseRequest==0);
 m.cursor=1;m.input(key);assert(m.page==UiPage::EraseLicense);
 std::filesystem::remove_all(root);puts("PASS per-license options (MKWO0001 with timer and map)/retail bytes/defaults/erase, seven menu rows, controls");
}
