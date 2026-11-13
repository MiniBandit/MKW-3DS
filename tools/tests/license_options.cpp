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
 for(unsigned i=0;i<4;++i){auto& s=saves[i];s.settings.ledMode=i;s.settings.dynamicShadows=i%2;s.settings.ledBrightness=50+10*i;s.settings.raceTimer=i!=1;s.settings.mapTurns=i!=2;assert(writeLicenseSave(root,i,s));}
 assert(bytes(file)==retail); // Port settings must not alter any retail/ghost byte.
 std::array<LicenseSave,4> loaded;assert(loadLicenseSaves(root,loaded)==LicenseLoad::Loaded);
 for(unsigned i=0;i<4;++i){assert(loaded[i].settings.ledMode==i);assert(loaded[i].settings.ledBrightness==50+10*i);assert(loaded[i].settings.dynamicShadows==bool(i%2));
  assert(loaded[i].settings.raceTimer==(i!=1)&&loaded[i].settings.mapTurns==(i!=2));}
 assert(bytes(std::filesystem::path(root)/"license-options-1.dat").size()==32&&!bytes(std::filesystem::path(root)/"license-options-1.dat").compare(0,8,"MKWO0002"));
 {// An MKWO0001 file (older builds) keeps its options; the new ones default to on, then it is rewritten as MKWO0002.
  const auto path=std::filesystem::path(root)/"license-options-3.dat";std::string v2=bytes(path);
  std::string v1=v2.substr(0,20);v1.replace(0,8,"MKWO0001");v1[16]=2;v1[17]=0;v1[18]=70;v1[19]=2;
  std::uint32_t c=~0u;for(unsigned char b:v1){c^=b;for(int k=0;k<8;++k)c=c&1?(c>>1)^0xEDB88320u:c>>1;}c=~c;
  for(int k=3;k>=0;--k)v1.push_back(char(c>>(8*k)&255));
  for(auto suffix:{"",".bak"})std::filesystem::remove(std::string(path)+suffix);
  {std::ofstream f(path,std::ios::binary);f<<v1;}
  LicenseSave old;assert(loadLicenseSave(root,3,old)==LicenseLoad::Loaded);
  assert(old.settings.ledMode==2&&!old.settings.dynamicShadows&&old.settings.ledBrightness==70&&old.settings.graphics==2&&old.settings.raceTimer&&old.settings.mapTurns);
  old.settings.mapTurns=false;assert(writeLicenseSave(root,3,old));
  assert(bytes(path).size()==32&&!bytes(path).compare(0,8,"MKWO0002"));
  LicenseSave again;assert(loadLicenseSave(root,3,again)==LicenseLoad::Loaded&&again.settings.ledBrightness==70&&!again.settings.mapTurns&&again.settings.raceTimer);
 }
 // Legacy saves and corrupt optional files retain safe defaults, not a damaged license.
 for(auto suffix:{"",".bak"})std::filesystem::remove(std::filesystem::path(root)/(std::string("license-options-0.dat")+suffix));
 assert(loadLicenseSave(root,0,loaded[0])==LicenseLoad::Loaded);assert(loaded[0].settings.ledMode==0&&loaded[0].settings.dynamicShadows&&loaded[0].settings.ledBrightness==100);
 {std::ofstream f(std::filesystem::path(root)/"license-options-0.dat");f<<"bad";}
 assert(loadLicenseSave(root,0,loaded[0])==LicenseLoad::Loaded&&loaded[0].settings.ledMode==0);
 LicenseSave empty;empty.settings.license=1;assert(writeLicenseSave(root,1,empty));
 LicenseSave created=saves[1];created.settings=UiSelection{};created.settings.license=1;assert(writeLicenseSave(root,1,created));
 assert(loadLicenseSave(root,1,loaded[1])==LicenseLoad::Loaded&&loaded[1].settings.ledMode==0);
 MenuState m;m.enter(UiPage::LicenseSettings);m.advance(120);assert(m.count()==8);
 UiInput key{};key.right=true;m.cursor=2;m.input(key);assert(m.selection.ledMode==1&&m.page==UiPage::LicenseSettings);
 key={};key.left=true;m.input(key);assert(m.selection.ledMode==0);
 key={};key.accept=true;m.cursor=3;m.input(key);assert(!m.selection.dynamicShadows&&m.eraseLicenseRequest<0);
 m.cursor=4;m.input(key);assert(m.selection.ledBrightness==50);
 key={};key.left=true;m.input(key);assert(m.selection.ledBrightness==50);
 key={};key.right=true;m.input(key);assert(m.selection.ledBrightness==60);
 assert(m.selection.raceTimer&&m.selection.mapTurns);
 m.cursor=6;key={};key.right=true;m.input(key);assert(!m.selection.raceTimer);key={};key.accept=true;m.input(key);assert(m.selection.raceTimer);
 m.cursor=7;key={};key.left=true;m.input(key);assert(!m.selection.mapTurns);key={};key.accept=true;m.input(key);assert(m.selection.mapTurns&&m.page==UiPage::LicenseSettings);
 std::vector<UiQuad> source,top,bottom;
 for(unsigned level=0;level<4;++level){m.selection.ledMode=level;composeMenu(m,{},source);layoutMenuScreens(m,source,top,bottom);
  std::set<unsigned> rows;unsigned glyphs[8]{};float lowest=0;
  for(const auto& q:bottom)if(q.menu.role==UiMenuRole::Choice){rows.insert(q.menu.group);assert(q.menu.group<8&&q.x>=0&&q.y>=0&&q.x+q.w<=320.01f&&q.y+q.h<=240.01f);
   if(q.sprite<std::size(UiSprites)&&!std::strncmp(UiSprites[q.sprite].key,"name_glyph_",11)){++glyphs[q.menu.group];lowest=std::max(lowest,q.y+q.h);}}
  // Every label's text sits above the Back bar (the six-row layout ended at 208).
  assert(rows.size()==8&&glyphs[2]>7&&glyphs[3]>15&&glyphs[4]>15&&glyphs[5]>10&&glyphs[6]>14&&glyphs[7]>15&&lowest<=210.f);
 }
 UiTransitionView preload{};preload.preload=true;
 composeMenu(m,{},source,preload);std::set<unsigned> resident;
 for(const auto&q:source)resident.insert(q.sprite);
 for(unsigned v=50;v<=100;v+=10){m.selection.ledBrightness=v;m.selection.raceTimer=v%20;m.selection.mapTurns=v%20==0;m.selection.graphics=v/10%3;composeMenu(m,{},source);
 for(const auto&q:source)if(q.sprite<std::size(UiSprites)&&!std::strncmp(UiSprites[q.sprite].key,"name_glyph_",11)&&!resident.count(q.sprite))assert(false&&"brightness glyph must be preloaded");}
 m.cursor=0;key={};key.accept=true;m.input(key);assert(m.editLicenseRequest==0);
 m.cursor=1;m.input(key);assert(m.page==UiPage::EraseLicense);
 std::filesystem::remove_all(root);puts("PASS per-license options (MKWO0002, MKWO0001 upgrade)/retail bytes/defaults/erase, eight menu rows and native glyphs, controls");
}
