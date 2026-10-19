#pragma once
#include <cstddef>
namespace mkw {
// The update overlay: files carried by a quick update's RomFS (romfs:/overlay/)
// that take the place of the same relative path in the game's files (SD folder
// or packs). It lets an update ship new or changed small assets (UI images,
// icons, layouts, Mii placeholders) without a rebuild. See docs/UI_AND_MII_FOUNDATION.md.
//
// init() lists the overlay once at start-up, before any other thread runs;
// afterwards every query is read-only. A game without an overlay (or without a
// RomFS) pays nothing: active() is false and fopen is untouched.
namespace overlay {
void init();
bool active();
// True when the relative path (as the game opens it, with or without "./") is in the overlay.
bool has(const char* path);
// When has(path): writes the real romfs path and returns true.
bool resolve(const char* path,char* out,std::size_t size);
}
}
