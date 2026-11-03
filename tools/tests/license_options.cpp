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
#include <unistd.h>
using namespace mkw;
std::string bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(){
 char root[]="/tmp/mkw-options-XXXXXX";assert(mkdtemp(root));auto file=std::filesystem::path(root)/"rksys.dat";
 std::array<LicenseSave,4> saves{};
 for(unsigned i=0;i<4;++i){auto& s=saves[i];s.occupied=true;s.settings.license=i;s.nativeMii[0]=3;s.nativeMii[4]=i+1;s.miiId=licenseMiiId(s.nativeMii);assert(writeLicenseSave(root,i,s));}
 assert(loadLicenseSaves(root,saves)==LicenseLoad::Loaded);
 const auto retail=bytes(file);
 for(unsigned i=0;i<4;++i){auto& s=saves[i];s.settings.ledMode=i;s.settings.dynamicShadows=i%2;s.settings.ledBrightness=50+10*i;assert(writeLicenseSave(root,i,s));}
 assert(bytes(file)==retail); // Port settings must not alter any retail/ghost byte.
 std::array<LicenseSave,4> loaded;assert(loadLicenseSaves(root,loaded)==LicenseLoad::Loaded);
 for(unsigned i=0;i<4;++i){assert(loaded[i].settings.ledMode==i);assert(loaded[i].settings.ledBrightness==50+10*i);assert(loaded[i].settings.dynamicShadows==bool(i%2));}
 // Legacy saves and corrupt optional files retain safe defaults, not a damaged license.
 for(auto suffix:{"",".bak"})std::filesystem::remove(std::filesystem::path(root)/(std::string("license-options-0.dat")+suffix));
 assert(loadLicenseSave(root,0,loaded[0])==LicenseLoad::Loaded);assert(loaded[0].settings.ledMode==0&&loaded[0].settings.dynamicShadows&&loaded[0].settings.ledBrightness==100);
 {std::ofstream f(std::filesystem::path(root)/"license-options-0.dat");f<<"bad";}
 assert(loadLicenseSave(root,0,loaded[0])==LicenseLoad::Loaded&&loaded[0].settings.ledMode==0);
 LicenseSave empty;empty.settings.license=1;assert(writeLicenseSave(root,1,empty));
 LicenseSave created=saves[1];created.settings=UiSelection{};created.settings.license=1;assert(writeLicenseSave(root,1,created));
 assert(loadLicenseSave(root,1,loaded[1])==LicenseLoad::Loaded&&loaded[1].settings.ledMode==0);
 MenuState m;m.enter(UiPage::LicenseSettings);m.advance(120);assert(m.count()==5);
 UiInput key{};key.right=true;m.cursor=2;m.input(key);assert(m.selection.ledMode==1&&m.page==UiPage::LicenseSettings);
 key={};key.left=true;m.input(key);assert(m.selection.ledMode==0);
 key={};key.accept=true;m.cursor=3;m.input(key);assert(!m.selection.dynamicShadows&&m.eraseLicenseRequest<0);
 m.cursor=4;m.input(key);assert(m.selection.ledBrightness==50);
 key={};key.left=true;m.input(key);assert(m.selection.ledBrightness==50);
 key={};key.right=true;m.input(key);assert(m.selection.ledBrightness==60);
 std::vector<UiQuad> source,top,bottom;
 for(unsigned level=0;level<4;++level){m.selection.ledMode=level;composeMenu(m,{},source);layoutMenuScreens(m,source,top,bottom);
  std::set<unsigned> rows;unsigned glyphs[5]{};
  for(const auto& q:bottom)if(q.menu.role==UiMenuRole::Choice){rows.insert(q.menu.group);assert(q.x>=0&&q.y>=0&&q.x+q.w<=320.01f&&q.y+q.h<=240.01f);if(q.sprite<std::size(UiSprites)&&!std::strncmp(UiSprites[q.sprite].key,"name_glyph_",11))++glyphs[q.menu.group];}
  assert(rows.size()==5&&glyphs[2]>7&&glyphs[3]>15&&glyphs[4]>15);
 }
 UiTransitionView preload{};preload.preload=true;
 composeMenu(m,{},source,preload);std::set<unsigned> resident;
 for(const auto&q:source)resident.insert(q.sprite);
 for(unsigned v=50;v<=100;v+=10){m.selection.ledBrightness=v;composeMenu(m,{},source);
 for(const auto&q:source)if(q.sprite<std::size(UiSprites)&&!std::strncmp(UiSprites[q.sprite].key,"name_glyph_",11)&&!resident.count(q.sprite))assert(false&&"brightness glyph must be preloaded");}
 m.cursor=0;key={};key.accept=true;m.input(key);assert(m.editLicenseRequest==0);
 m.cursor=1;m.input(key);assert(m.page==UiPage::EraseLicense);
 std::filesystem::remove_all(root);puts("PASS per-license options/retail bytes/defaults/erase, five menu rows and native glyphs, controls");
}
