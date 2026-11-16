#pragma once
#include <memory>
namespace mkw {
// Streams the original opening THP's decoded PCM on NDSP channel zero.
// File reads run on the AssetLoader at Realtime priority; update() only
// queues completed buffers. Call update outside the graphics frame. Missing DSP service/firmware leaves
// video playback usable and is logged once per process.
class MenuMovieAudio {
public:
 MenuMovieAudio();~MenuMovieAudio();
 MenuMovieAudio(const MenuMovieAudio&)=delete;
 MenuMovieAudio& operator=(const MenuMovieAudio&)=delete;
 bool open(const char* path,bool loop=false,unsigned loopStartSample=0);
 void update();
 void setVolume(float gain);
 void close();
 bool ready()const;
 bool finished()const;
 // Loads the DSP component once (call at boot); open() also ensures it.
 static void initialize();
 static bool dspReady();
 // System Settings sound (config block 0x70001), as an NDSP output mode:
 // 0 mono, 1 stereo, 2 surround. Read once in initialize().
 static int outputMode();
 // Releases NDSP at application exit.
 static void shutdown();
private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
