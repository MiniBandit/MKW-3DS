#pragma once
#include <mkw/menu_ui.hpp>
#include <mkw/asset_loader.hpp>
#include <memory>
namespace mkw {
class Renderer;
class MenuVehicleIcons;
// Texture residency for the authored menu sprites and movie streams.
// Asynchronous use (menus): request() queues loads on the AssetLoader,
// ready() reports whether a set is resident, update() publishes finished loads
// and evicts least-recently-used textures beyond the LINEAR budget. Nothing
// here waits for the GPU; evicted textures are retired through the Renderer.
// prepare() is the synchronous path for QA captures and the race overlay.
class MenuRenderer {
public:
 MenuRenderer();~MenuRenderer();
 void request(const std::vector<UiQuad>& resident,AssetPriority priority);
 bool ready(const std::vector<UiQuad>& resident)const;
 // Pins the sets most recently requested at Urgent/Normal priority, publishes
 // completed loads and evicts unpinned LRU textures down to the budget.
 void update(Renderer&);
 // Movie streams for the visible quads: opens/closes streams, selects rows
 // and the frame for this clock. Never blocks.
 void streamMovies(const std::vector<UiQuad>& quads,std::uint64_t elapsed60HzTicks,bool advance=true);
 // Inside a frame (after Renderer::begin): copies completed movie frames.
 void uploadMovies();
 bool moviesFinished() const;
 bool hasMovies() const;
 int cupMovieChapter() const;
 // Synchronous residency (outside a frame, GPU already drained).
 bool needsPrepare(const std::vector<UiQuad>&,bool stableClock=true) const;
 bool prepare(const std::vector<UiQuad>&,bool stableClock=true);
 bool updateMovies(std::uint64_t elapsed60HzTicks); // blocking, QA only
 void draw(Renderer&,const std::vector<UiQuad>&,bool bottom,unsigned first=0,unsigned end=~0u,MenuVehicleIcons* icons=nullptr);
 // Publish an identity's already-composed RGBA portrait outside a frame.
 // Old GPU references are retired; invalid input preserves the previous image.
 bool setLicensePortrait(Renderer&,unsigned slot,unsigned width,unsigned height,const std::uint8_t* rgba,std::size_t bytes);
 bool hasLicensePortrait(unsigned slot) const;
 void clearLicensePortrait(Renderer&,unsigned slot);
 // Retires every texture and stream (GPU-safe) and cancels pending loads.
 void release(Renderer&);
 void release();
 std::uint32_t residentBytes() const;
 unsigned loadsPending() const;
 void logPending(const std::vector<UiQuad>& resident,const char* path)const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
