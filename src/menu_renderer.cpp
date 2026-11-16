#include <mkw/menu_movie_chapters.hpp>
#include <mkw/menu_renderer.hpp>
#include <mkw/menu_background_motion.hpp>
#include <mkw/menu_assets.hpp>
#include <mkw/menu_glyph_atlas.hpp>
#include <mkw/menu_index.hpp>
#include <mkw/menu_movie.hpp>
#include <mkw/menu_movie_assets.hpp>
#include <mkw/menu_vehicle_icons.hpp>
#include <mkw/renderer.hpp>
#include <mkw/asset_pack.hpp>
#include <algorithm>
#include <bitset>
#include <cstdio>
#include <vector>
#include <cmath>
#include <cstring>
#include <string>
namespace mkw {
namespace {
using Residency=std::bitset<std::size(UiSprites)>;
using Streams=std::array<bool,std::size(UiMovieStreams)>;
Residency neededSprites(const std::vector<UiQuad>& quads){
 Residency needed{};
 // Runtime values can change without changing page/cursor residency keys.
 // Keep every brightness digit resident even on the synchronous/recovery path.
 const bool options=std::any_of(quads.begin(),quads.end(),[](const auto& q){return q.menu.page==UiPage::LicenseSettings;});
 if(options)for(unsigned i=0;i<needed.size();++i){const char* key=UiSprites[i].key;
  if(!std::strcmp(key,"name_glyph_0025")||(!std::strncmp(key,"name_glyph_003",14)&&key[14]>='0'&&key[14]<='9'&&!key[15]))needed[uiTextureSprite(i)]=true;
 }
 for(const auto& q:quads)if(q.vehicleIcon>=12&&q.movie>=std::size(UiMovieRegions)&&q.sprite<needed.size())needed[uiTextureSprite(q.sprite)]=true;
 // composeMenu(preload=true) already emits every atlas a page can draw,
 // including later animation frames. Do not add all atlases of a base panel:
 // some authored strips are clipped for this control/page placement, and
 // retaining them costs a megabyte in the pause menu without a visible quad.
 return needed;
}
Streams neededStreams(const std::vector<UiQuad>& quads){
 Streams needed{};
 for(const auto& q:quads)if(q.movie<std::size(UiMovieRegions))needed[UiMovieRegions[q.movie].stream]=true;
 return needed;
}
bool inVram(const Texture& t){const u32 at=reinterpret_cast<u32>(t.gpu.data);return at>=OS_VRAM_VADDR&&at<OS_VRAM_VADDR+OS_VRAM_SIZE;}
bool fourBit(const Texture& t){const auto f=t.gpu.fmt;return f==GPU_ETC1||f==GPU_L4||f==GPU_A4;}
// Every texel opaque (alpha 255), padding included: a backdrop drawn with it at
// full alpha hides whatever was under it exactly. Only textures large enough
// to back a page are scanned (on the loading thread).
bool opaqueTexture(const Texture& t){
 switch(t.gpu.fmt){
 case GPU_RGB8:case GPU_RGB565:case GPU_ETC1:return true;
 case GPU_RGBA8:{
  if(unsigned(t.gpu.width)*t.gpu.height<256u*256u)return false;
  const auto* p=static_cast<const std::uint8_t*>(t.gpu.data);const std::size_t n=t.bytes();
  for(std::size_t i=0;i<n;i+=4)if(p[i]!=255)return false; // tex3ds RGBA8 texels: A,B,G,R
  return true;}
 default:return false;
 }
}
bool loops(unsigned stream){
 for(const auto& r:UiMovieRegions)if(r.stream==stream&&!std::strcmp(r.key,"intro_movie"))return false;
 return true;
}
// Keep this much LINEAR memory free for models, movies and render buffers.
// During a race the pause menu shares LINEAR memory with the course.
constexpr std::uint32_t LinearReserve=3u<<20,CacheCapacity=24u<<20;
// placeInVram: the smallest sprite worth a copy, and the most copies in one frame.
constexpr std::size_t VramMinBytes=32*1024;constexpr unsigned VramCopiesPerFrame=4;
struct TextureJob final:AssetJob {
 const char* path;std::unique_ptr<Texture> texture;
 explicit TextureJob(const char* p):path(p){label=p;}
 const char* detail()const override{return timing;}
 void run()override{
  // Texture::load reads from the open menu.pak when present (one seek+read).
  const auto start=svcGetSystemTick();
  auto t=std::make_unique<Texture>();if(t->load(path)){opaque=opaqueTexture(*t);texture=std::move(t);}

#ifndef MKW_RELEASE
  std::snprintf(timing,sizeof timing,"%s %.1f ms",pack::contains(path)?"pack":"file",float((svcGetSystemTick()-start)*1000.0/SYSCLOCK_ARM11));
#endif

 }
 char timing[96]{};bool opaque=false;
};
}
struct MenuRenderer::Impl {
 std::array<std::unique_ptr<Texture>,4> portraits;
 struct Slot {std::unique_ptr<Texture> texture;std::shared_ptr<TextureJob> job;std::uint32_t used=0,lastRetry=0;unsigned failures=0;bool opaque=false;};
 std::array<Slot,std::size(UiSprites)> slots;
 std::array<std::unique_ptr<MenuMovie>,std::size(UiMovieStreams)> movies;
 Residency pinned{},pinning{};Streams streamsPinned{},streamsPinning{};
 std::uint32_t frame=1,bytes=0;
 // placeInVram: bytes copied to VRAM and back (totals), sprite bytes now in VRAM.
 std::uint64_t vramIn=0,vramOut=0;std::uint32_t vramResident=0;
 // Synchronous (prepare) bookkeeping.
 Residency wanted{};Streams streams{};
 // loaded[i]==bool(slots[i].texture): install/remove are the only writers.
 Residency loaded{};
 void install(unsigned i,std::unique_ptr<Texture> texture,bool opaque){
  bytes+=texture->bytes();if(inVram(*texture))vramResident+=texture->bytes();
  slots[i].texture=std::move(texture);slots[i].opaque=opaque;loaded[i]=bool(slots[i].texture);
 }
 std::unique_ptr<Texture> remove(unsigned i){
  auto& s=slots[i];if(s.texture){bytes-=s.texture->bytes();if(inVram(*s.texture))vramResident-=s.texture->bytes();}
  loaded[i]=false;return std::move(s.texture);
 }
 void openStream(unsigned i){
  if(movies[i])return;
  movies[i]=std::make_unique<MenuMovie>();movies[i]->open(UiMovieStreams[i].path,loops(i));
 }
};
MenuRenderer::MenuRenderer():impl_(std::make_unique<Impl>()){}
MenuRenderer::~MenuRenderer()=default;
void MenuRenderer::request(const std::vector<UiQuad>& resident,AssetPriority priority){
 auto& s=*impl_;const auto needed=neededSprites(resident);
 for(unsigned i=0;i<needed.size();++i){if(!needed[i])continue;
  auto& slot=s.slots[i];slot.used=s.frame;
  if(slot.texture)continue;
  // Failed speculative loads must not permanently remove a later visible
  // caption. Leave room for current-page resources, and retry visible failures
  // once a second after the initial attempts (also handles recovered memory).
  if(priority==AssetPriority::Prefetch&&(slot.failures||linearSpaceFree()<LinearReserve))continue;
  if(slot.failures>=3&&s.frame-slot.lastRetry<60)continue;
  slot.lastRetry=s.frame;
  if(!slot.job||(slot.job->done()&&slot.job->cancelled()))slot.job=std::make_shared<TextureJob>(UiSprites[i].path);
  if(!slot.job->done())AssetLoader::instance().submit(slot.job,priority);
 }
 if(priority==AssetPriority::Prefetch)return;
 s.pinning|=needed;
 const auto streams=neededStreams(resident);
 for(unsigned i=0;i<streams.size();++i)if(streams[i]){s.streamsPinning[i]=true;s.openStream(i);}
}
bool MenuRenderer::ready(const std::vector<UiQuad>& resident)const{
 const auto& s=*impl_;const auto needed=neededSprites(resident);
 // A completed retry is not a resident texture. Keep the incoming page
 // hidden until its captions and borders actually loaded; request() continues
 // retrying failed visible assets after its cooldown.
 for(unsigned i=0;i<needed.size();++i)if(needed[i]&&!s.slots[i].texture)return false;
 const auto streams=neededStreams(resident);
 for(unsigned i=0;i<streams.size();++i)if(streams[i]&&s.movies[i]&&!s.movies[i]->ready()&&!s.movies[i]->failed())return false;
 return true;
}
void MenuRenderer::update(Renderer& renderer){
 auto& s=*impl_;
 s.pinned=s.pinning;s.pinning.reset();s.streamsPinned=s.streamsPinning;s.streamsPinning.fill(false);
 bool requiredLoadFailed=false;
 for(unsigned i=0;i<s.slots.size();++i){auto& slot=s.slots[i];
  if(!slot.job||!slot.job->done())continue;
  auto job=std::move(slot.job);
  if(job->texture){s.install(i,std::move(job->texture),job->opaque);slot.failures=0;}
  else if(!job->cancelled()){++slot.failures;requiredLoadFailed|=s.pinned[i];}
 }
 // Least recently requested first; never the pinned page/incoming sets.
 // Retired memory returns at the next frame, so count it as already free.
 std::uint32_t retiring=0;
 // linear: only textures in LINEAR (one in VRAM frees none of it).
 auto evict=[&](bool linear=false){
  unsigned victim=~0u;std::uint32_t oldest=~0u;
  for(unsigned i=0;i<s.slots.size();++i)if(s.slots[i].texture&&!s.pinned[i]&&s.slots[i].used<oldest&&!(linear&&inVram(*s.slots[i].texture))){oldest=s.slots[i].used;victim=i;}
  if(victim==~0u)return false;
  auto texture=s.remove(victim);if(!inVram(*texture))retiring+=texture->bytes();renderer.retire(std::move(texture));return true;
 };
 // A failed required allocation can mean fragmentation even when the total
 // free byte count exceeds the reserve. Drop speculative/old-page textures
 // now instead of retrying against exactly the same allocation layout.
 if(requiredLoadFailed){
  while(evict()){}
  if(retiring)for(unsigned i=0;i<s.slots.size();++i)
   if(s.pinned[i]&&!s.slots[i].texture)s.slots[i].failures=0;
 }
 while(s.bytes>CacheCapacity&&evict()){}
 while(linearSpaceFree()+retiring<LinearReserve&&evict(true)){}
 for(unsigned i=0;i<s.movies.size();++i)if(s.movies[i]&&!s.streamsPinned[i])renderer.retire(std::move(s.movies[i]));
 ++s.frame;
}
void MenuRenderer::streamMovies(const std::vector<UiQuad>& quads,std::uint64_t ticks,bool advance){
 auto& s=*impl_;
 std::array<unsigned,std::size(UiMovieStreams)> first{},end{};first.fill(~0u);
 for(const auto& q:quads)if(q.movie<std::size(UiMovieRegions)){
  const auto& r=UiMovieRegions[q.movie];const float height=float(UiMovieStreams[r.stream].height);
  const float top=r.v0+(r.v1-r.v0)*q.v0,bottom=r.v0+(r.v1-r.v0)*q.v1;
  first[r.stream]=std::min(first[r.stream],unsigned(std::max(0.f,std::floor(std::min(top,bottom)*height))));
  end[r.stream]=std::max(end[r.stream],unsigned(std::min(height,std::ceil(std::max(top,bottom)*height))));
  s.streamsPinning[r.stream]=true;s.openStream(r.stream);
 }
 for(unsigned i=0;i<s.movies.size();++i)if(s.movies[i]){
  // Bilinear sampling reads one texel beyond the visible edge.
  if(first[i]!=~0u)s.movies[i]->setRows(first[i]>0?first[i]-1:0,end[i]+1);
  s.movies[i]->setPaused(!advance);
  if(advance)s.movies[i]->request(ticks);
 }
}
void MenuRenderer::uploadMovies(){for(auto& m:impl_->movies)if(m)m->upload();}
int MenuRenderer::cupMovieChapter()const{
 for(const auto& r:UiMovieRegions)if(!std::strcmp(r.key,"cup_movie_0")){
  const auto& movie=impl_->movies[r.stream];
  if(!movie||!movie->ready())return -1;
  const unsigned frame=movie->currentFrame();
  return frame<std::size(CupMovieChapters)&&CupMovieChapters[frame]<4?int(CupMovieChapters[frame]):-1;
 }
 return -1;
}
bool MenuRenderer::hasMovies()const{for(const auto& m:impl_->movies)if(m)return true;return false;}
bool MenuRenderer::moviesFinished()const{bool any=false;for(const auto& m:impl_->movies)if(m){any=true;if(!m->finished())return false;}return any;}
bool MenuRenderer::needsPrepare(const std::vector<UiQuad>& quads,bool)const{
 const auto& s=*impl_;const auto needed=neededSprites(quads);const auto streams=neededStreams(quads);
 if(needed!=s.loaded)return true;
 for(unsigned i=0;i<streams.size();++i)if(streams[i]!=bool(s.movies[i]))return true;
 return false;
}
bool MenuRenderer::prepare(const std::vector<UiQuad>& quads,bool){
 // Synchronous path: the caller has drained the GPU, so retirement is immediate.
 auto& s=*impl_;const auto needed=neededSprites(quads);const auto streams=neededStreams(quads);
 for(unsigned i=0;i<needed.size();++i){auto& slot=s.slots[i];if(slot.job){slot.job->cancel();slot.job.reset();}if(!needed[i])s.remove(i);}
 for(unsigned i=0;i<streams.size();++i)if(!streams[i])s.movies[i].reset();
 bool ok=true;
 for(unsigned i=0;i<needed.size();++i)if(needed[i]&&!s.slots[i].texture){auto t=std::make_unique<Texture>();if(t->load(UiSprites[i].path)){const bool opaque=opaqueTexture(*t);s.install(i,std::move(t),opaque);}else ok=false;}
 for(unsigned i=0;i<streams.size();++i)if(streams[i])s.openStream(i);
 return ok;
}
bool MenuRenderer::updateMovies(std::uint64_t ticks){bool ok=true;for(auto& m:impl_->movies)if(m)ok=m->uploadBlocking(ticks)&&ok;return ok;}
void MenuRenderer::draw(Renderer& renderer,const std::vector<UiQuad>& quads,bool bottom,unsigned first,unsigned end,MenuVehicleIcons* icons){
 for(unsigned i=first;i<std::min(end,unsigned(quads.size()));++i){const auto& q=quads[i];
  if(q.bottom!=bottom)continue;
  if(q.licensePortrait<impl_->portraits.size()){
   if(const auto& image=impl_->portraits[q.licensePortrait]){
    float corners[4][2]={{q.x,q.y},{q.x+q.w,q.y},{q.x,q.y+q.h},{q.x+q.w,q.y+q.h}};
    if(q.affine)for(unsigned k=0;k<4;++k)for(unsigned axis=0;axis<2;++axis)corners[k][axis]=q.positions[k][axis];
    const float uv[4][2]={{q.u0,q.v0},{q.u1,q.v0},{q.u0,q.v1},{q.u1,q.v1}};
    uint32_t colors[4];
    for(unsigned k=0;k<4;++k){colors[k]=0;for(unsigned shift:{24u,16u,8u,0u})
     colors[k]|=((((q.corners[k]>>shift)&255)*((q.tint>>shift)&255)+127)/255)<<shift;}
    renderer.drawScreenMaterialQuad(image.get(),corners,uv,colors,0,0xffffffff,q.alpha,q.additive);
   }
  }else if(q.vehicleIcon<12){if(icons)icons->draw(renderer,q);}
  else if(q.movie<std::size(UiMovieRegions)){
   const auto& r=UiMovieRegions[q.movie];const auto& movie=impl_->movies[r.stream];
   if(movie&&movie->texture())renderer.drawScreenSpriteRegion(*movie->texture(),q.x,q.y,q.w,q.h,r.u0+(r.u1-r.u0)*q.u0,r.v0+(r.v1-r.v0)*q.v0,r.u0+(r.u1-r.u0)*q.u1,r.v0+(r.v1-r.v0)*q.v1,q.alpha,q.tint,q.corners.data(),q.additive);
  }else if(q.sprite<impl_->slots.size()&&impl_->slots[uiTextureSprite(q.sprite)].texture){
   const auto& info=UiSprites[q.sprite];const auto& texture=*impl_->slots[uiTextureSprite(q.sprite)].texture;
   // A name glyph's place in its atlas page.
   const auto origin=uiTexelOrigin(q.sprite);
   const float u0=float(origin.x)/texture.gpu.width,v0=float(origin.y)/texture.gpu.height;
   // The stripes: "background" (dark), "background_lines" (stripes alone,
   // over the title picture) and "background_option" (the Settings page).
   if(!std::strncmp(info.key,"background",10)){
    // One wall clock for upper/lower backgrounds and every menu page. Release
    // and page/section clocks must not restart this ambient scroll.
    static const auto origin=svcGetSystemTick();
    const double seconds=menuStripeSeconds>=0?menuStripeSeconds:double(svcGetSystemTick()-origin)/SYSCLOCK_ARM11;
    for(const auto& part:menuStripeSlices(q.y,q.h,q.v0,q.v1,seconds))if(part.h>0)
     renderer.drawScreenSpriteRegion(texture,q.x,part.y,q.w,part.h,info.u*q.u0,info.v*part.v0+1.f/texture.gpu.height,
       info.u*q.u1,info.v*part.v1+1.f/texture.gpu.height,q.alpha,q.tint,q.corners.data(),q.additive);
   }else if(q.fontRemap||q.affine){
    float corners[4][2]={{q.x,q.y},{q.x+q.w,q.y},{q.x,q.y+q.h},{q.x+q.w,q.y+q.h}};
    if(q.affine)for(unsigned k=0;k<4;++k)for(unsigned axis=0;axis<2;++axis)corners[k][axis]=q.positions[k][axis];
    const float uv[4][2]={{u0+info.u*q.u0,v0+info.v*q.v0},{u0+info.u*q.u1,v0+info.v*q.v0},{u0+info.u*q.u0,v0+info.v*q.v1},{u0+info.u*q.u1,v0+info.v*q.v1}};
    uint32_t colors[4];
    for(unsigned k=0;k<4;++k){colors[k]=0;for(unsigned shift:{24u,16u,8u,0u})
     colors[k]|=((((q.corners[k]>>shift)&255)*((q.tint>>shift)&255)+127)/255)<<shift;}
    renderer.drawScreenMaterialQuad(&texture,corners,uv,colors,q.fontColor0,q.fontColor1,q.alpha,q.additive);
   }else renderer.drawScreenSpriteRegion(texture,q.x,q.y,q.w,q.h,u0+info.u*q.u0,v0+info.v*q.v0,u0+info.u*q.u1,v0+info.v*q.v1,q.alpha,q.tint,q.corners.data(),q.additive);
  }
 }
}
bool MenuRenderer::setLicensePortrait(Renderer& renderer,unsigned slot,unsigned width,unsigned height,const std::uint8_t* rgba,std::size_t bytes){
 if(slot>=impl_->portraits.size())return false;
 auto image=std::make_unique<Texture>();if(!image->initRgba8(width,height,rgba,bytes))return false;
 clearLicensePortrait(renderer,slot);
 impl_->bytes+=image->bytes();impl_->portraits[slot]=std::move(image);return true;
}
bool MenuRenderer::hasLicensePortrait(unsigned slot) const{return slot<impl_->portraits.size()&&bool(impl_->portraits[slot]);}
void MenuRenderer::clearLicensePortrait(Renderer& renderer,unsigned slot){
 if(slot>=impl_->portraits.size())return;
 auto& image=impl_->portraits[slot];if(image)impl_->bytes-=image->bytes();renderer.retire(std::move(image));
}
void MenuRenderer::release(Renderer& renderer){
 auto& s=*impl_;
 for(unsigned i=0;i<s.portraits.size();++i)clearLicensePortrait(renderer,i);
 for(unsigned i=0;i<s.slots.size();++i){auto& slot=s.slots[i];if(slot.job){slot.job->cancel();slot.job.reset();}slot.failures=0;renderer.retire(s.remove(i));}
 for(auto& m:s.movies)renderer.retire(std::move(m));
 s.pinned.reset();s.pinning.reset();s.streamsPinned.fill(false);s.streamsPinning.fill(false);
}
void MenuRenderer::release(){
 auto& s=*impl_;
 for(auto& image:s.portraits){if(image)s.bytes-=image->bytes();image.reset();}
 for(unsigned i=0;i<s.slots.size();++i){auto& slot=s.slots[i];if(slot.job){slot.job->cancel();slot.job.reset();}slot.failures=0;s.remove(i);}
 for(auto& m:s.movies)m.reset();
 s.pinned.reset();s.pinning.reset();s.streamsPinned.fill(false);s.streamsPinning.fill(false);
}
std::size_t MenuRenderer::moveToVram(Renderer& renderer,std::size_t reserve){
 auto& s=*impl_;std::vector<std::pair<std::size_t,unsigned>> order;
 for(unsigned i=0;i<s.slots.size();++i)if(const auto& t=s.slots[i].texture){
  // 4-bit textures stay in LINEAR whatever unit draws them (3dbrew GPU/Pitfalls).
  const auto format=t->gpu.fmt;if(format==GPU_ETC1||format==GPU_L4||format==GPU_A4)continue;
  order.push_back({std::size_t(C3D_TexCalcTotalSize(t->gpu.size,t->gpu.maxLevel)),i});
 }
 std::sort(order.rbegin(),order.rend());
 std::size_t moved=0;
 for(const auto& [bytes,i]:order)if(renderer.moveToVram(*s.slots[i].texture,reserve)){moved+=bytes;s.vramResident+=std::uint32_t(bytes);}
 return moved;
}
bool MenuRenderer::coversScreen(const std::vector<UiQuad>& quads)const{
 if(quads.empty())return false;
 const auto& q=quads[0];const auto& s=*impl_;
 if(q.bottom||q.sprite>=s.slots.size()||uiTextureSprite(q.sprite)!=q.sprite||!s.slots[q.sprite].texture||!s.slots[q.sprite].opaque)return false;
 if(q.licensePortrait<s.portraits.size()||q.vehicleIcon<12||q.movie<std::size(UiMovieRegions)||q.fontRemap||q.affine||q.additive)return false;
 // Full alpha everywhere: the blend then writes the texel colour alone.
 if(!(q.alpha>=1.f)||(q.tint&255)!=255)return false;
 for(const auto c:q.corners)if((c&255)!=255)return false;
 if(!(q.x<=0.f&&q.y<=0.f&&q.x+q.w>=400.f&&q.y+q.h>=240.f))return false;
 // The stripes draw nothing for an empty V span (menuStripeSlices).
 return std::strncmp(UiSprites[q.sprite].key,"background",10)||q.v1>q.v0;
}
std::size_t MenuRenderer::placeInVram(Renderer& renderer,const std::vector<UiQuad>& drawn,std::size_t reserve,std::size_t budget){
 auto& s=*impl_;
 // Screen area each sprite covers in this frame. Only the entries the last
 // frame touched are zeroed and only drawn sprites are visited (no scan of
 // every slot); the candidates and their sorted order are the same.
 static std::vector<float> area;static std::vector<unsigned> touched;static std::vector<char> marked;
 if(area.size()!=s.slots.size()){area.assign(s.slots.size(),0.f);marked.assign(s.slots.size(),0);touched.clear();}
 for(const unsigned i:touched){area[i]=0.f;marked[i]=0;}
 touched.clear();
 for(const auto& q:drawn)if(q.sprite<s.slots.size()&&q.movie>=std::size(UiMovieRegions)&&q.vehicleIcon>=12&&q.licensePortrait>=s.portraits.size()){
  const unsigned k=uiTextureSprite(q.sprite);
  if(!marked[k]){marked[k]=1;touched.push_back(k);}
  area[k]+=std::min(std::fabs(q.w*q.h),400.f*240.f)*std::clamp(q.alpha,0.f,1.f);
 }
 // Most screen area per byte first (the full-screen stripes and pictures).
 // Small sprites (glyphs, captions) cost little fill: they stay.
 std::vector<std::pair<float,unsigned>> wanted;
 for(const unsigned i:touched){const auto& t=s.slots[i].texture;
  if(t&&area[i]>0&&!inVram(*t)&&!fourBit(*t)&&t->bytes()>=VramMinBytes)wanted.push_back({area[i]/float(t->bytes()),i});}
 if(wanted.empty())return 0;
 std::sort(wanted.rbegin(),wanted.rend());
 auto swap=[&](unsigned i,std::unique_ptr<Texture> copy){
  const bool opaque=s.slots[i].opaque;auto old=s.remove(i);s.install(i,std::move(copy),opaque);renderer.retire(std::move(old));
 };
 // Each copy takes two entries of citro3d's 32-entry GX queue (a command list
 // split and the copy), beside the frame's own fills and transfers: a full
 // queue panics. A few copies a frame, whatever their size.
 std::size_t copied=0;unsigned copies=0;
 for(const auto& [score,i]:wanted){
  if(copied>=budget||copies>=VramCopiesPerFrame)break;
  const std::size_t bytes=s.slots[i].texture->bytes();
  if(vramSpaceFree()>=bytes+reserve){
   if(auto copy=renderer.copyTextureInFrame(*s.slots[i].texture,true,reserve)){swap(i,std::move(copy));copied+=bytes;++copies;s.vramIn+=bytes;}
   continue;
  }
  // No room: sprites in VRAM that this frame doesn't draw and no shown or
  // incoming page needs go back to LINEAR (least recently requested first).
  // Their VRAM is free once this frame is drawn; the move waits for a later frame.
  std::size_t freeing=0;
  // No sprite in VRAM: no victim to find.
  while(s.vramResident&&vramSpaceFree()+freeing<bytes+reserve&&copied<budget&&copies<VramCopiesPerFrame){
   unsigned victim=~0u;std::uint32_t oldest=~0u;
   for(unsigned k=0;k<s.slots.size();++k){const auto& t=s.slots[k].texture;
    if(t&&inVram(*t)&&!s.pinned[k]&&area[k]<=0&&s.slots[k].used<oldest){oldest=s.slots[k].used;victim=k;}}
   if(victim==~0u)break;
   const std::size_t size=s.slots[victim].texture->bytes();
   std::unique_ptr<Texture> copy;
   if(linearSpaceFree()>=size+LinearReserve)copy=renderer.copyTextureInFrame(*s.slots[victim].texture,false,0);
   if(copy){swap(victim,std::move(copy));copied+=size;++copies;s.vramOut+=size;}
   else renderer.retire(s.remove(victim));
   freeing+=size;
  }
  break;
 }
 return copied;
}
std::uint32_t MenuRenderer::vramBytes()const{return impl_->vramResident;}
std::uint64_t MenuRenderer::vramCopied(bool toVram)const{return toVram?impl_->vramIn:impl_->vramOut;}
std::uint32_t MenuRenderer::residentBytes()const{return impl_->bytes;}
unsigned MenuRenderer::loadsPending()const{unsigned n=0;for(const auto& slot:impl_->slots)n+=slot.job&&!slot.job->done();return n;}
void MenuRenderer::logPending(const std::vector<UiQuad>& resident,const char* path)const{
 FILE* f=std::fopen(path,"a");if(!f)return;
 const auto& s=*impl_;const auto needed=neededSprites(resident);
 std::fprintf(f,"menu load stalled: frame=%lu linear_free=%lu cache=%lu queue=%u\n",
  (unsigned long)s.frame,(unsigned long)linearSpaceFree(),(unsigned long)s.bytes,AssetLoader::instance().pending());
 for(unsigned i=0;i<needed.size();++i)if(needed[i]&&!s.slots[i].texture){const auto& v=s.slots[i];
  std::fprintf(f,"texture %s failures=%u job=%s\n",UiSprites[i].path,v.failures,!v.job?"none":v.job->done()?"done":v.job->queued()?"queued":"running");}
 const auto streams=neededStreams(resident);
 for(unsigned i=0;i<streams.size();++i)if(streams[i]&&(!s.movies[i]||!s.movies[i]->ready()))
  {char state[200]="none";if(s.movies[i])s.movies[i]->describe(state,sizeof state);
  std::fprintf(f,"movie %s failed=%d %s\n",UiMovieStreams[i].path,s.movies[i]&&s.movies[i]->failed(),state);}
 std::fclose(f);
}

}
