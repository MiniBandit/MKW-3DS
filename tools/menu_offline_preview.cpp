// Host-only fixture exporter. Production composition, animation and layout are
// linked unchanged; this file supplies states and serializes their draw lists.
#include <mkw/menu_ui.hpp>
#include <mkw/menu_transition.hpp>
#include <mkw/menu_assets.hpp>
#include <mkw/menu_unlock_texts.hpp>
#include <mkw/menu_movie_assets.hpp>
#include <mkw/menu_movie_chapters.hpp>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
using namespace mkw;
static std::string quote(const char* value){
 std::string out="\"";for(unsigned char c:std::string(value)){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32)out+=' ';else out+=c;}return out+'"';
}
static void quad(std::ostream& o,const UiQuad& q){
 const UiSpriteInfo dynamic{"license_portrait","",0,0,1,1,"",0,0,0};
 const auto& a=q.sprite<std::size(UiSprites)?UiSprites[q.sprite]:dynamic;o<<"{\"key\":"<<quote(a.key)<<",\"path\":"<<quote(a.path)<<",\"text\":"<<quote(a.text)<<",\"spriteUV\":["<<a.u<<','<<a.v<<"],\"rect\":["<<q.x<<','<<q.y<<','<<q.w<<','<<q.h<<"],\"uv\":["<<q.u0<<','<<q.v0<<','<<q.u1<<','<<q.v1<<"],\"alpha\":"<<q.alpha<<",\"tint\":"<<q.tint<<",\"corners\":[";
 for(int i=0;i<4;++i)o<<(i?",":"")<<q.corners[i];
 o<<"],\"additive\":"<<q.additive<<",\"fontRemap\":"<<q.fontRemap<<",\"fontColors\":["<<q.fontColor0<<','<<q.fontColor1<<"],\"vehicleIcon\":"<<q.vehicleIcon<<",\"vehicleColors\":["<<q.vehicleColor0<<','<<q.vehicleColor1<<"],\"affine\":"<<q.affine<<",\"positions\":[";
 for(int i=0;i<4;++i)o<<(i?",":"")<<'['<<q.positions[i][0]<<','<<q.positions[i][1]<<']';
 o<<"],\"licensePortrait\":"<<q.licensePortrait<<",\"page\":"<<unsigned(q.menu.page)<<",\"role\":"<<unsigned(q.menu.role)<<",\"group\":"<<q.menu.group;
 if(q.movie<std::size(UiMovieRegions)){const auto& r=UiMovieRegions[q.movie];const auto& s=UiMovieStreams[r.stream];o<<",\"movie\":{\"key\":"<<quote(r.key)<<",\"path\":"<<quote(s.path)<<",\"uv\":["<<r.u0<<','<<r.v0<<','<<r.u1<<','<<r.v1<<"]}";}
 o<<'}';
}
static void list(std::ostream& o,const std::vector<UiQuad>& qs){o<<'[';bool first=true;for(const auto& q:qs){if(!first)o<<',';first=false;quad(o,q);}o<<']';}
struct Fixture {std::string name;MenuState state;UiTransitionView motion{};};
int main(int argc,char** argv){
 if(argc!=2)return 2;std::vector<Fixture> fixtures;
 auto add=[&](const char* name,UiPage page,unsigned cursor=0,UiMode mode=UiMode::GrandPrix,unsigned age=120)->MenuState&{
  MenuState s;s.selection.mode=mode;s.selection.driver=0;s.selection.vehicle=0;s.selection.license=0;s.licenseOccupied={true,false,true,false};s.licenseCups[0][0]=3;s.enter(page);s.cursor=cursor;s.advance(age);
  if(page==UiPage::Cup){const unsigned frame=unsigned(double(age)*59.94/60.0);if(frame<std::size(CupMovieChapters))s.previewCourse=CupMovieChapters[frame];}
  fixtures.push_back({name,s});return fixtures.back().state;
 };
 for(const auto& notice:UiUnlockTexts){
  if(notice.condition!=0&&notice.condition!=16&&notice.condition!=17&&notice.condition!=32&&notice.condition!=37&&!(notice.condition==61&&notice.bit==52))continue;
  auto& state=add(("unlock-"+std::to_string(notice.condition)+"-"+std::to_string(notice.bit)).c_str(),UiPage::Unlock);
  state.unlockCondition=notice.condition;state.unlockBit=notice.bit;
 }
 add("strap-a",UiPage::Strap);add("strap-b",UiPage::Strap,0,UiMode::GrandPrix,240);add("intro",UiPage::Intro,0,UiMode::GrandPrix,900);add("title",UiPage::Title);
 for(auto page:{UiPage::License,UiPage::Title,UiPage::LicenseSettings}){
  auto& s=add(("named-license-"+std::to_string(unsigned(page))).c_str(),page);
  s.licenseNames[0]={'M','a','r','i','o',' ',0x00e9,'1','2','3'};
 }
 auto& unlockedCard=add("license-progress",UiPage::LicenseSettings);unlockedCard.licenseUnlocks[0][0]=1u<<14;unlockedCard.licenseCups[0][2]=1;
 auto& mirrorCard=add("license-mirror",UiPage::LicenseSettings);mirrorCard.licenseUnlocks[0].fill(~0u);mirrorCard.licenseCups[0].fill(1);
 for(unsigned c:{0u,1u,3u,4u})add(("license-"+std::to_string(c)).c_str(),UiPage::License,c);
 for(unsigned c=0;c<8;++c)add(("main-"+std::to_string(c)).c_str(),UiPage::MainMenu,c);
 for(unsigned c=0;c<4;++c)add(("mode-"+std::to_string(c)).c_str(),UiPage::Mode,c);
 for(unsigned c=0;c<4;++c)add(("class-"+std::to_string(c)).c_str(),UiPage::EngineClass,c);
 add("class-mixed",UiPage::EngineClass,0).selection.mixedEngineTypes=true;
 for(unsigned c:{0u,8u,23u,24u})add(("character-"+std::to_string(c)).c_str(),UiPage::Character,c);
 add("character-unavailable",UiPage::Character,24).unavailable=true;
 for(unsigned c:{0u,1u,10u,11u})add(("vehicle-"+std::to_string(c)).c_str(),UiPage::Vehicle,c);
 for(unsigned e=0;e<2;++e)for(unsigned c:{0u,5u})add(((e?"vehicle-bikes-":"vehicle-karts-")+std::to_string(c)).c_str(),UiPage::Vehicle,c).selection.engineClass=UiEngineClass(e);
 for(unsigned d:{4u,11u}){auto& s=add(("vehicle-weight-"+std::to_string(d)).c_str(),UiPage::Vehicle,0);s.selection.driver=d;}
 for(unsigned c=0;c<3;++c)add(("drift-"+std::to_string(c)).c_str(),UiPage::Drift,c);
 add("drift-help",UiPage::DriftHelp);
 for(unsigned c=0;c<8;++c)add(("cup-"+std::to_string(c)).c_str(),UiPage::Cup,c);
 for(unsigned c=0;c<4;++c)add(("course-"+std::to_string(c)).c_str(),UiPage::Course,c,UiMode::VS);
 add("course-last-cup",UiPage::Course,3,UiMode::VS).selection.course=31;
 for(unsigned c=0;c<3;++c)add(("vs-mode-"+std::to_string(c)).c_str(),UiPage::VSMode,c,UiMode::VS);
 for(unsigned c=0;c<7;++c)add(("vs-rules-"+std::to_string(c)).c_str(),UiPage::VSRules,c,UiMode::VS);
 auto& rules=add("vs-rules-max",UiPage::VSRules,5,UiMode::VS);rules.selection.vsClass=3;rules.selection.vsCpu=2;rules.selection.vsVehicles=2;rules.selection.vsCourses=2;rules.selection.vsItems=3;rules.selection.vsRaces=8;
 for(unsigned c=0;c<2;++c){auto& s=add(("team-confirm-"+std::to_string(c)).c_str(),UiPage::TeamConfirm,c,UiMode::VS);s.selection.teams=true;}
 for(unsigned c=0;c<2;++c)add(("battle-mode-"+std::to_string(c)).c_str(),UiPage::BattleMode,c,UiMode::Battle);
 for(unsigned c=0;c<2;++c)add(("battle-cup-"+std::to_string(c)).c_str(),UiPage::BattleCup,c,UiMode::Battle);
 for(unsigned c=0;c<5;++c)add(("battle-stage-"+std::to_string(c)).c_str(),UiPage::BattleStage,c,UiMode::Battle);
 add("battle-stage-retro",UiPage::BattleStage,4,UiMode::Battle).selection.battleStage=9;
 add("tt-solo",UiPage::Ghost,0,UiMode::TimeTrials);
 for(unsigned c=0;c<3;++c)add(("settings-"+std::to_string(c)).c_str(),UiPage::Settings,c);
 add("settings-unavailable",UiPage::Settings,0).unavailable=true;
 for(unsigned c=0;c<1;++c)add("license-settings",UiPage::LicenseSettings,c);
 for(unsigned c=0;c<2;++c)add(("erase-license-"+std::to_string(c)).c_str(),UiPage::EraseLicense,c);
 for(unsigned m=0;m<3;++m)add(("confirm-"+std::to_string(m)).c_str(),UiPage::Confirm,0,UiMode(m));
 for(unsigned c=0;c<2;++c)add(("pause-gp-"+std::to_string(c)).c_str(),UiPage::Pause,c);
 for(unsigned c=0;c<5;++c)add(("pause-tt-"+std::to_string(c)).c_str(),UiPage::Pause,c,UiMode::TimeTrials);
 for(unsigned c=0;c<2;++c)add(("quit-confirm-"+std::to_string(c)).c_str(),UiPage::QuitConfirm,c);
 add("error",UiPage::Error);
 // Capture real transition states for two particularly dense pages.
 for(auto page:{UiPage::Vehicle,UiPage::Cup}){
  MenuState state;state.selection.mode=UiMode::VS;state.enter(page);state.advance(120);MenuTransition transition;UiInput key{};key.accept=true;transition.input(state,key);
  for(unsigned frame=0;frame<36;++frame){if(frame==0||frame==8||frame==20)fixtures.push_back({std::string(page==UiPage::Vehicle?"vehicle-exit-":"cup-exit-")+std::to_string(frame),transition.visual(state),transition.view()});transition.advance(state,1);}
 }
 for(unsigned frame:{0u,3u,6u,9u,20u}){
  auto& state=add(("slide-check-"+std::to_string(frame)).c_str(),UiPage::Mode);
  fixtures.back().motion.active=true;fixtures.back().motion.elapsed=float(frame);
  fixtures.back().motion.maxDelay=uiMenuMaxDelay(state);
 }
 for(unsigned frame:{0u,3u,6u,9u,10u}){
  auto& state=add(("title-transfer-"+std::to_string(frame)).c_str(),UiPage::Title);
  auto& motion=fixtures.back().motion;motion.active=true;motion.to=UiPage::License;motion.elapsed=float(frame);motion.exitHold=0;
 }
 for(auto page:{UiPage::Character,UiPage::Vehicle,UiPage::Cup}){
  auto& state=add(("glow-"+std::to_string(unsigned(page))).c_str(),page);
  auto& motion=fixtures.back().motion;motion.confirming=true;motion.selectedControl=uiSelectedMenuControl(state);motion.holdFrame=4;
 }
 for(auto page:{UiPage::Character,UiPage::Vehicle,UiPage::Cup}){auto& s=add(("select-glow-"+std::to_string(unsigned(page))).c_str(),page);UiInput right{};right.down=true;s.input(right);s.advance(4);}
 for(unsigned frame:{4u,12u,20u})add(("title-sweep-"+std::to_string(frame)).c_str(),UiPage::Title,0,UiMode::GrandPrix,frame);
 for(unsigned frame:{0u,3u,6u,9u,10u}){
  auto& state=add(("license-return-"+std::to_string(frame)).c_str(),UiPage::License);
  auto& motion=fixtures.back().motion;motion.active=true;motion.to=UiPage::Title;motion.back=true;motion.elapsed=float(frame);motion.exitHold=0;
 }
 std::ofstream out(argv[1]);out<<std::setprecision(9)<<"{\"version\":1,\"fixtures\":[";bool first=true;
 for(const auto& f:fixtures){std::vector<UiQuad> source,top,bottom,bg;composeMenu(f.state,{},source,f.motion);layoutMenuScreens(f.state,source,top,bottom);
  const bool reflow=f.state.page!=UiPage::Intro&&f.state.page!=UiPage::Strap&&f.state.page!=UiPage::Pause&&f.state.page!=UiPage::QuitConfirm;
  if(!source.empty()){bg.push_back(source[0]);if(!reflow&&!top.empty())top.erase(top.begin());}
  if(!first)out<<',';first=false;out<<"{\"name\":"<<quote(f.name.c_str())<<",\"page\":"<<unsigned(f.state.page)<<",\"cursor\":"<<f.state.cursor<<",\"mode\":"<<unsigned(f.state.selection.mode)<<",\"driver\":"<<f.state.selection.driver<<",\"vehicle\":"<<f.state.selection.vehicle<<",\"age\":"<<f.state.age<<",\"topBackground\":";list(out,bg);out<<",\"top\":";list(out,top);out<<",\"bottom\":";list(out,bottom);out<<'}';
 }
 out<<"]}\n";return out?0:1;
}
