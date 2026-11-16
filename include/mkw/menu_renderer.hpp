#pragma once
#include <mkw/menu_ui.hpp>
#include <mkw/asset_loader.hpp>
#include <memory>
namespace mkw {
class Renderer;
class MenuVehicleIcons;
// QA (menu-still.txt): the stripes' ambient scroll at this many seconds instead
// of the wall clock, so captures of different runs can match pixel for pixel.
inline double menuStripeSeconds=-1;
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
 // Old 3DS race load (pause preload): moves the loaded sprite textures of 8 or
 // more bits a texel, largest first, from LINEAR to VRAM while `reserve` bytes
 // of VRAM stay free (Renderer::moveToVram). Returns the bytes moved.
 std::size_t moveToVram(Renderer&,std::size_t reserve);
 // New 3DS menus, inside a frame before its sprites are drawn: the resident
 // sprites this frame draws move to VRAM (Renderer::copyTextureInFrame), most
 // screen area per byte first, while `reserve` bytes of VRAM stay free and up
 // to `budget` bytes (and at most 4 textures: citro3d's GX queue) are copied
 // in the frame. Sprites under 32 KB stay in LINEAR.
 // With no room, sprites in VRAM that no page shown or loading needs and this
 // frame doesn't draw go back to LINEAR first. 4-bit textures stay in LINEAR.
 // Returns the bytes copied. release() frees their VRAM with the rest.
 std::size_t placeInVram(Renderer&,const std::vector<UiQuad>& drawn,std::size_t reserve,std::size_t budget);
 std::uint32_t vramBytes() const;
 std::uint64_t vramCopied(bool toVram) const;
 // quads[0] (the page backdrop) is resident, opaque in every texel, drawn at
 // full alpha and covers the whole upper LCD: nothing drawn under it shows.
 bool coversScreen(const std::vector<UiQuad>& quads) const;
 std::uint32_t residentBytes() const;
 unsigned loadsPending() const;
 void logPending(const std::vector<UiQuad>& resident,const char* path)const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
