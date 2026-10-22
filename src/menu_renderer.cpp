#include <mkw/menu_renderer.hpp>
#include <mkw/menu_assets.hpp>
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
struct BorderResidency {unsigned base=std::size(UiSprites);Residency atlases{};};
const auto& borderResidency(){
 static const auto value=[](){
  std::array<BorderResidency,std::size(UiBorderLoops)> result{};
  for(unsigned i=0;i<result.size();++i){const auto& loop=UiBorderLoops[i];auto& entry=result[i];entry.base=uiFindSpriteKey(loop.key);
   for(unsigned at=loop.firstSlice;at<loop.firstSlice+loop.sliceCount;++at){const unsigned sprite=uiFindSpriteKey(UiBorderSlices[at].key);if(sprite<entry.atlases.size())entry.atlases[sprite]=true;}
  }
  return result;
 }();return value;
}
Residency neededSprites(const std::vector<UiQuad>& quads){
 Residency needed{};
 for(const auto& q:quads)if(q.vehicleIcon>=12&&q.movie>=std::size(UiMovieRegions)&&q.sprite<needed.size())needed[q.sprite]=true;
 for(const auto& border:borderResidency())if(border.base<needed.size()&&needed[border.base])needed|=border.atlases;
 return needed;
}
Streams neededStreams(const std::vector<UiQuad>& quads){
 Streams needed{};
 for(const auto& q:quads)if(q.movie<std::size(UiMovieRegions))needed[UiMovieRegions[q.movie].stream]=true;
 return needed;
}
bool loops(unsigned stream){
 for(const auto& r:UiMovieRegions)if(r.stream==stream&&!std::strcmp(r.key,"intro_movie"))return false;
 return true;
}
// Keep this much LINEAR memory free for models, movies and render buffers.
// During a race the pause menu shares LINEAR memory with the course.
constexpr std::uint32_t LinearReserve=3u<<20,CacheCapacity=24u<<20;
struct TextureJob final:AssetJob {
 const char* path;std::unique_ptr<Texture> texture;
 explicit TextureJob(const char* p):path(p){label=p;}
 const char* detail()const override{return timing;}
 void run()override{
  // Texture::load reads from the open menu.pak when present (one seek+read).
  const auto start=svcGetSystemTick();
  auto t=std::make_unique<Texture>();if(t->load(path))texture=std::move(t);

#ifndef MKW_RELEASE
  std::snprintf(timing,sizeof timing,"%s %.1f ms",pack::contains(path)?"pack":"file",float((svcGetSystemTick()-start)*1000.0/SYSCLOCK_ARM11));
#endif

 }
 char timing[96]{};
};
}
struct MenuRenderer::Impl {
 // deferred: a prefetch load failed; only a request that needs it now retries.
 struct Slot {std::unique_ptr<Texture> texture;std::shared_ptr<TextureJob> job;std::uint32_t used=0;unsigned failures=0;bool deferred=false;};
 std::array<Slot,std::size(UiSprites)> slots;
 std::array<std::unique_ptr<MenuMovie>,std::size(UiMovieStreams)> movies;
 Residency pinned{},pinning{};Streams streamsPinned{},streamsPinning{};
 std::uint32_t frame=1,bytes=0;
 // Synchronous (prepare) bookkeeping.
 Residency wanted{};Streams streams{};
 // loaded[i]==bool(slots[i].texture): install/remove are the only writers.
 Residency loaded{};
 void install(unsigned i,std::unique_ptr<Texture> texture){bytes+=texture->bytes();slots[i].texture=std::move(texture);loaded[i]=bool(slots[i].texture);}
 std::unique_ptr<Texture> remove(unsigned i){auto& s=slots[i];if(s.texture)bytes-=s.texture->bytes();loaded[i]=false;return std::move(s.texture);}
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
  if(priority!=AssetPriority::Prefetch)slot.deferred=false;
  if(slot.texture||slot.failures>=3||slot.deferred)continue;
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
 for(unsigned i=0;i<needed.size();++i)if(needed[i]&&!s.slots[i].texture&&s.slots[i].failures<3)return false;
 const auto streams=neededStreams(resident);
 for(unsigned i=0;i<streams.size();++i)if(streams[i]&&s.movies[i]&&!s.movies[i]->ready()&&!s.movies[i]->failed())return false;
 return true;
}
void MenuRenderer::update(Renderer& renderer){
 auto& s=*impl_;
 s.pinned=s.pinning;s.pinning.reset();s.streamsPinned=s.streamsPinning;s.streamsPinning.fill(false);
 // Texture::load fails when LINEAR has no contiguous block for it. Only a
 // texture needed now counts towards giving up (ready() then stops waiting).
 bool flush=false;
 for(unsigned i=0;i<s.slots.size();++i){auto& slot=s.slots[i];
  if(!slot.job||!slot.job->done())continue;
  auto job=std::move(slot.job);
  if(job->texture){s.install(i,std::move(job->texture));slot.failures=0;slot.deferred=false;}
  else if(!job->cancelled()){if(s.pinned[i]){++slot.failures;flush=true;}else slot.deferred=true;}
 }
 // Each needed texture still loading is one allocation (a 512x1024 RGBA8
 // border is 2 MiB): keep it free beyond the reserve.
 std::uint32_t incoming=0;
 for(unsigned i=0;i<s.slots.size();++i)if(s.pinned[i]&&!s.slots[i].texture&&s.slots[i].failures<3)incoming+=UiSprites[i].gpuBytes;
 // Least recently requested first; never the pinned page/incoming sets.
 // Retired memory returns at the next frame, so count it as already free.
 std::uint32_t retiring=0;
 auto evict=[&](bool stale){
  unsigned victim=~0u;std::uint32_t oldest=~0u;
  for(unsigned i=0;i<s.slots.size();++i)if(s.slots[i].texture&&!s.pinned[i]&&s.slots[i].used<oldest&&(!stale||s.slots[i].used!=s.frame)){oldest=s.slots[i].used;victim=i;}
  if(victim==~0u)return false;
  auto texture=s.remove(victim);retiring+=texture->bytes();renderer.retire(std::move(texture));return true;
 };
 while((s.bytes>CacheCapacity||linearSpaceFree()+retiring<LinearReserve)&&evict(false)){}
 // That room comes from textures no request wants this frame; after a failed
 // allocation all of them go, so the retry finds the largest free blocks.
 while((flush||linearSpaceFree()+retiring<LinearReserve+incoming)&&evict(true)){}
 for(unsigned i=0;i<s.movies.size();++i)if(s.movies[i]&&!s.streamsPinned[i])renderer.retire(std::move(s.movies[i]));
 ++s.frame;
}
void MenuRenderer::streamMovies(const std::vector<UiQuad>& quads,std::uint64_t ticks){
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
  s.movies[i]->request(ticks);
 }
}
void MenuRenderer::uploadMovies(){for(auto& m:impl_->movies)if(m)m->upload();}
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
 for(unsigned i=0;i<needed.size();++i)if(needed[i]&&!s.slots[i].texture){auto t=std::make_unique<Texture>();if(t->load(UiSprites[i].path))s.install(i,std::move(t));else ok=false;}
 for(unsigned i=0;i<streams.size();++i)if(streams[i])s.openStream(i);
 return ok;
}
std::uint32_t MenuRenderer::missingBytes(const std::vector<UiQuad>& quads)const{
 const auto& s=*impl_;const auto needed=neededSprites(quads);std::uint32_t bytes=0;
 for(unsigned i=0;i<needed.size();++i)if(needed[i]&&!s.slots[i].texture)bytes+=UiSprites[i].gpuBytes;
 return bytes;
}
bool MenuRenderer::updateMovies(std::uint64_t ticks){bool ok=true;for(auto& m:impl_->movies)if(m)ok=m->uploadBlocking(ticks)&&ok;return ok;}
void MenuRenderer::draw(Renderer& renderer,const std::vector<UiQuad>& quads,bool bottom,unsigned first,unsigned end,MenuVehicleIcons* icons){
 for(unsigned i=first;i<std::min(end,unsigned(quads.size()));++i){const auto& q=quads[i];
  if(q.bottom!=bottom)continue;
  if(q.vehicleIcon<12){if(icons)icons->draw(renderer,q);}
  else if(q.movie<std::size(UiMovieRegions)){
   const auto& r=UiMovieRegions[q.movie];const auto& movie=impl_->movies[r.stream];
   if(movie&&movie->texture())renderer.drawScreenSpriteRegion(*movie->texture(),q.x,q.y,q.w,q.h,r.u0+(r.u1-r.u0)*q.u0,r.v0+(r.v1-r.v0)*q.v0,r.u0+(r.u1-r.u0)*q.u1,r.v0+(r.v1-r.v0)*q.v1,q.alpha,q.tint,q.corners.data(),q.additive);
  }else if(q.sprite<impl_->slots.size()&&impl_->slots[q.sprite].texture){const auto& info=UiSprites[q.sprite];
   if(q.material){
    // Corners TL, TR, BL, BR; the tint multiplies every corner colour.
    const float corners[4][2]={{q.x,q.y},{q.x+q.w,q.y},{q.x,q.y+q.h},{q.x+q.w,q.y+q.h}};
    const float uv[4][2]={{info.u*q.u0,info.v*q.v0},{info.u*q.u1,info.v*q.v0},{info.u*q.u0,info.v*q.v1},{info.u*q.u1,info.v*q.v1}};
    uint32_t colors[4];
    for(unsigned k=0;k<4;++k){colors[k]=0;for(unsigned shift:{24u,16u,8u,0u})colors[k]|=((((q.corners[k]>>shift)&255u)*((q.tint>>shift)&255u)+127u)/255u)<<shift;}
    renderer.drawScreenMaterialQuad(impl_->slots[q.sprite].texture.get(),corners,uv,colors,q.color0,q.color1,q.alpha);
   }else renderer.drawScreenSpriteRegion(*impl_->slots[q.sprite].texture,q.x,q.y,q.w,q.h,info.u*q.u0,info.v*q.v0,info.u*q.u1,info.v*q.v1,q.alpha,q.tint,q.corners.data(),q.additive);}
 }
}
void MenuRenderer::release(Renderer& renderer){
 auto& s=*impl_;
 for(unsigned i=0;i<s.slots.size();++i){auto& slot=s.slots[i];if(slot.job){slot.job->cancel();slot.job.reset();}slot.failures=0;slot.deferred=false;renderer.retire(s.remove(i));}
 for(auto& m:s.movies)renderer.retire(std::move(m));
 s.pinned.reset();s.pinning.reset();s.streamsPinned.fill(false);s.streamsPinning.fill(false);
}
void MenuRenderer::release(){
 auto& s=*impl_;
 for(unsigned i=0;i<s.slots.size();++i){auto& slot=s.slots[i];if(slot.job){slot.job->cancel();slot.job.reset();}slot.failures=0;slot.deferred=false;s.remove(i);}
 for(auto& m:s.movies)m.reset();
 s.pinned.reset();s.pinning.reset();s.streamsPinned.fill(false);s.streamsPinning.fill(false);
}
std::uint32_t MenuRenderer::residentBytes()const{return impl_->bytes;}
unsigned MenuRenderer::loadsPending()const{unsigned n=0;for(const auto& slot:impl_->slots)n+=slot.job&&!slot.job->done();return n;}
}
