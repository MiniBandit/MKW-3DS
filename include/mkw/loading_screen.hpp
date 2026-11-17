#pragma once
#include <cstdint>
namespace mkw {
class Renderer;
// Only instantiate around blocking main-thread work, outside a GPU frame.
// The spinner owns no filesystem or Citro3D state on its timer thread.
void initializeLoadingScreen(Renderer&);
void shutdownLoadingScreen();
// GPU wait screens pass their own start time; normal scene frames never call this.
void drawLoadingWheel(Renderer&,std::uint64_t waitStarted);
// LoadingScopes opened so far: each may have drawn on the lower LCD.
unsigned loadingScopes();
class LoadingScope {
public:
 LoadingScope();~LoadingScope();
 LoadingScope(const LoadingScope&)=delete;LoadingScope& operator=(const LoadingScope&)=delete;
private:bool owner_=false;
};
// The top screen while a race loads (tools/cook_loading_picture.py): the
// picture fades in, its periods count up, and the destructor fades it out to
// black. Drawn into the framebuffer by a timer thread like the lower screen's
// wheel, so open it around blocking main-thread work only, with no GPU frames
// in between. Without the cooked picture it does nothing (black, as before).
class RaceLoadingPicture {
public:
 RaceLoadingPicture();~RaceLoadingPicture();
 RaceLoadingPicture(const RaceLoadingPicture&)=delete;RaceLoadingPicture& operator=(const RaceLoadingPicture&)=delete;
private:bool active_=false;
};
}
