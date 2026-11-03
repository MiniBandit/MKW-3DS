#include <mkw/award_ui.hpp>
#include <mkw/award_board_motion.hpp>
#include <mkw/gp_rank.hpp>
#include <mkw/race_flow_draw.hpp>
#include <mkw/race_intro_layout.hpp>
#include <mkw/renderer.hpp>
#include <array>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace {
const char* message(unsigned id){
 for(const auto& m:FlowAwardMessages)if(m.id==id)return m.text;
 return "";
}
int texture(const char* name){
 for(unsigned i=0;i<std::size(FlowAwardTextureNames);++i)
  if(!std::strcmp(name,FlowAwardTextureNames[i]))return int(i);
 return -1;
}
int characterIcon(unsigned id){
 constexpr const char* names[]={"mario","baby_peach","waluigi","koopa","baby_daisy","karon","baby_mario","luigi",
  "kinopio","donky","yoshi","wario","baby_luigi","kinopico","noko","daisy","peach","catherine","didy","teresa",
  "koopa_jr","hone_koopa","funky","roseta"};
 if(id>=24&&id<42)return texture((id-24)%6<2?"tt_mii_suit_a_64x64.tpl":"tt_mii_suit_b_64x64.tpl");
 if(id>=std::size(names))return -1;
 char name[64];std::snprintf(name,sizeof name,"tt_%s_64x64.tpl",names[id]);return texture(name);
}
}
struct AwardUi::Impl {
 std::array<Texture,std::size(FlowAwardTextures)> textures;
 FlowDrawer drawer;AwardResults results;
 std::array<std::array<FlowPlay,6>,12> rows{};
 FlowPlay congratulations;
 AwardBoardMotion board;
 unsigned pageFrame=0;
 bool loaded=false,winner=false,tie=false;
 unsigned congratulationsId=1222;
 Renderer* renderer=nullptr;
 const FlowLayoutDef& rowLayout()const{return winner?FlowAwardItemWinLayout:FlowAwardItemLoseLayout;}
 const FlowControl& rowControl(unsigned i)const{
  const unsigned variant=results.boardSlot(i);
  return (winner?FlowAwardItemWinVariants[variant]:FlowAwardItemLoseVariants[variant]).pose;
 }
 const FlowControl& titleControl()const{return (winner?FlowAwardTypeWinVariants[0]:FlowAwardTypeLoseVariants[0]).pose;}
 FlowControl boardControl(const FlowControl& source)const{
  auto control=source;control.x+=board.offset(source.delay);return control;
 }
 const FlowLayoutDef& congratulationsLayout()const{
  return tie?FlowAwardCongratulationsDrawLayout:winner?FlowAwardCongratulationsWinLayout:FlowAwardCongratulationsLoseLayout;
 }
 static void submit(const FlowBatch& batch,void* context){
  auto& m=*static_cast<Impl*>(context);
  auto* t=batch.texture>=0&&unsigned(batch.texture)<m.textures.size()?&m.textures[batch.texture]:nullptr;
  if(batch.texture>=0&&(!t||!t->ready))return;
  auto wrap=[](unsigned v){return v==1?GPU_REPEAT:v==2?GPU_MIRRORED_REPEAT:GPU_CLAMP_TO_EDGE;};
  static_assert(sizeof(FlowQuad)==sizeof(Renderer::MaterialQuad));
  m.renderer->drawScreenMaterialQuads(t,reinterpret_cast<const Renderer::MaterialQuad*>(batch.quads),batch.count,
   batch.color0,batch.color1,wrap(batch.wrapS),wrap(batch.wrapT));
 }
 void draw(const FlowLayoutDef& def,const FlowControl& control,const FlowPlay* plays,unsigned playCount,
           const FlowAwardBinding* bindings,unsigned bindingCount){
  drawer.drawAward(def,control,plays,playCount,bindings,bindingCount,submit,this);
 }
};
AwardUi::AwardUi():impl_(std::make_unique<Impl>()){}
AwardUi::~AwardUi()=default;
bool AwardUi::load(){
 auto& m=*impl_;if(m.loaded)return true;
 for(unsigned i=0;i<m.textures.size();++i)if(!m.textures[i].load(FlowAwardTextures[i])){unload();return false;}
 m.loaded=true;return true;
}
void AwardUi::unload(){for(auto& t:impl_->textures)t.reset();impl_->loaded=false;}
void AwardUi::configure(const AwardResults& results,bool winner,bool draw){
 auto& m=*impl_;m.results=results;m.winner=winner;m.tie=draw;
 m.board.reset();
 m.congratulations=flowPlay(m.congratulationsLayout(),"Fade","Hide");
 unsigned rank=12;
 for(unsigned i=0;i<results.count;++i)if(results.players[i].human){rank=i+1;break;}
 m.congratulationsId=draw?1218:results.teams?
  (winner?1216:1223)+(results.humanTeam==RaceTeam::Blue?1:0):rank<=3?1218+rank:1222;
 const auto& def=m.rowLayout();
 for(unsigned i=0;i<results.count;++i){
  const auto& player=results.players[i];auto& p=m.rows[i];
  p[0]=flowPlay(def,"Loop","Loop");
  p[1]=flowPlay(def,"Player",player.human?"Player":"CPU");
  p[2]=flowPlay(def,"PlayerColorOn",player.human?"PlayerColorOn":"PlayerColorOff");
  p[3]=flowPlay(def,"PlayerColorType",results.teams?
   (player.team==RaceTeam::Red?"PlayerColorRed":"PlayerColorBlue"):"PlayerColor1P");
  p[4]=flowPlay(def,"CPUColorType",results.teams?
   (player.team==RaceTeam::Red?"CPUColorRed":"CPUColorBlue"):"CPUColorOff");
  p[5]=flowPlay(def,"PlayerFlash",player.human?"FlashPlayer":"FlashCPU");
 }
}
void AwardUi::showCongratulations(bool show){
 auto& m=*impl_;m.congratulations=flowPlay(m.congratulationsLayout(),"Fade",show?"Fadein":"Fadeout");
}
void AwardUi::enterBoard(){impl_->board.enter();impl_->pageFrame=0;}
void AwardUi::leaveBoard(){impl_->board.leave();}
bool AwardUi::boardTransitionFinished()const{
 const auto& m=*impl_;float delay=m.titleControl().delay;
 for(unsigned i=0;i<m.results.count;++i)delay=std::max(delay,m.rowControl(i).delay);
 return m.board.finished(delay);
}
unsigned AwardUi::step(){
 auto& m=*impl_;m.board.step();flowAdvance(m.congratulationsLayout(),m.congratulations,1);
 for(unsigned i=0;i<m.results.count;++i)for(auto& p:m.rows[i])flowAdvance(m.rowLayout(),p,1);
 if(m.board.active()&&!m.board.leaving()){
  ++m.pageFrame;
  // AwardResults::AfterControlUpdate, USA 806223dc: Fadein at tick 10.
  if(m.pageFrame==10)showCongratulations(true);
  if(m.pageFrame==180)return gpGradeSound(m.results.grade);
 }
 return 0;
}
void AwardUi::draw(Renderer& renderer){
 auto& m=*impl_;if(!m.loaded||!m.results.valid)return;m.renderer=&renderer;
 const auto& r=m.results;
 const auto& congratulations=m.tie?FlowAwardCongratulationsDrawVariants[0]:m.winner?FlowAwardCongratulationsWinVariants[0]:FlowAwardCongratulationsLoseVariants[0];
 const char* text=message(m.congratulationsId);
 const FlowAwardBinding congratulationsText[]={{"text_00",text},{"text_light",text}};
 m.draw(m.congratulationsLayout(),congratulations.pose,&m.congratulations,1,congratulationsText,2);
 if(!m.board.active())return;
 constexpr const char* cupIcons[]={"kinoko","flower","star","oukan","koura","banana","konoha","thunder"};
 char title[128],icon[80];
 const auto* engine=message(r.mirror?1420:1417+std::min(r.engineClass,2u));
 if(r.mode==0){
  std::snprintf(title,sizeof title,"%s %s",engine,IntroCupNames[std::min(r.cup,7u)]);
  std::snprintf(icon,sizeof icon,"tt_cup_icon_%s_00.tpl",cupIcons[std::min(r.cup,7u)]);
 }else if(r.mode==3){
  // AwardType's original Battle messages1415/1416 and matching mode icon.
  std::snprintf(title,sizeof title,"%s",message(r.coinRunners?1416:1415));
  std::snprintf(icon,sizeof icon,"st_battle_icon_%s.tpl",r.coinRunners?"coin":"balloon");
 }else{
  std::snprintf(title,sizeof title,"%s%s",engine,message(1422));
  std::snprintf(icon,sizeof icon,"st_battle_icon_flag.tpl");
 }
 const FlowAwardBinding type[]={{"cup_name",title},{"cup_icon",nullptr,texture(icon)}};
 m.draw(m.winner?FlowAwardTypeWinLayout:FlowAwardTypeLoseLayout,
  m.boardControl(m.titleControl()),nullptr,0,type,2);
 if(r.mode==0&&r.grade>=0&&r.grade<8){
  const auto& variant=m.winner?FlowAwardRankWinVariants[0]:FlowAwardRankLoseVariants[0];
  const FlowAwardBinding rank[]={{"rank",message(variant.message)},
   {"star",message(1424+unsigned(r.grade)),-2,&FlowAwardRankFont}};
  m.draw(m.winner?FlowAwardRankWinLayout:FlowAwardRankLoseLayout,
   m.boardControl(variant.pose),nullptr,0,rank,2);
 }
 for(unsigned i=0;i<r.count;++i){
  const auto& player=r.players[i];char points[16],rank[16];
  std::snprintf(points,sizeof points,"%u",player.points);
  std::snprintf(rank,sizeof rank,"%u%s",i+1,i==0?"st":i==1?"nd":i==2?"rd":"th");
  const int iconId=characterIcon(player.character);
  const FlowAwardBinding bindings[]={{"position",r.teams?"":rank},{"point",points},{"pts",player.points==1?"pt":"pts"},
   {"mii_name",player.character<FlowCharacterNames.size()?FlowCharacterNames[player.character]:"Mii"},
   {"chara_icon",nullptr,iconId},{"chara_icon_sha",nullptr,iconId}};
  m.draw(m.rowLayout(),m.boardControl(m.rowControl(i)),
   m.rows[i].data(),m.rows[i].size(),bindings,std::size(bindings));
 }
}
}
