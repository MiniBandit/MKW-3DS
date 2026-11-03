#pragma once
#include <cstdint>
#include <memory>

namespace mkw {
class Texture;
// Full-rate offline-decoded retail THP frames, streamed as GPU-native ETC1.
// File access runs on a dedicated stream thread. The main thread only picks
// the wanted frame/rows (request/setRows) and, inside a frame after
// Renderer::begin (GPU idle), copies a completed frame (upload). A frame that
// is still being read is never visible; a slow card drops frames instead of
// stalling presentation.
class MenuMovie {
public:
 MenuMovie();
 ~MenuMovie();
 MenuMovie(const MenuMovie&)=delete;
 MenuMovie& operator=(const MenuMovie&)=delete;
 // Starts asynchronous opening; failure is reported later by failed().
 bool open(const char* path,bool loop=true);
 void close();
 // Selects the frame for this 60 Hz clock. Never blocks.
 void request(std::uint64_t elapsed60HzTicks);
 void setPaused(bool paused);
 // Image rows [first,end) (top origin, texels) that visible quads sample.
 // Other rows keep their previous contents. Never blocks.
 void setRows(unsigned first,unsigned end);
 // Main thread, GPU idle: publishes the newest completed read.
 void upload();
 // QA/capture only: blocks until the requested frame has been uploaded.
 bool uploadBlocking(std::uint64_t elapsed60HzTicks);
 const Texture* texture()const;
 bool ready()const;
 bool failed()const;
 bool finished()const;
 unsigned currentFrame()const;
 unsigned frameCount()const;
 unsigned height()const;
 float framesPerSecond()const;
 struct Impl;
private:
 std::shared_ptr<Impl> impl_;
};
}
