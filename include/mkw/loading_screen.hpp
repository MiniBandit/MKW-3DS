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
class LoadingScope {
public:
 LoadingScope();~LoadingScope();
 LoadingScope(const LoadingScope&)=delete;LoadingScope& operator=(const LoadingScope&)=delete;
private:bool owner_=false;
};
}
