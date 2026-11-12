#pragma once
#include <mkw/math.hpp>
#include <mkw/intro_clearance.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Kinoko::Field {class RailInterpolator;}

namespace mkw {
// The course preview ("opening pan") shown before every race: the KMP CAME
// cameras chained from the section's opening index. Ported from the retail
// director (PAL 0x805AD5C8): each shot lasts its `time` in frames, then the
// next camera starts; FOV and the view point step linearly at speed/100 per
// frame (PAL 0x805A9D50, 0x805AA9FC). Type 4 holds its position (0x805ADAE4);
// type 5 follows its POTI route on a stop-at-end rail whose velocity is the
// route points' first setting, interpolated across each segment (0x805ADCB0).
class RaceIntro {
public:
 RaceIntro();~RaceIntro();
 RaceIntro(const RaceIntro&)=delete;RaceIntro& operator=(const RaceIntro&)=delete;
 // Reads course.kmp's CAME section. False when the course has no opening.
 bool load(const void* kmp,std::size_t size);
 // Starts the first shot. Requires Kinoko's RailManager for the current scene.
 void start();
 // One 60 Hz tick, in step with the race simulation.
 void step();
 // Ends the preview early (A or START).
 void skip(){stop();}
 // Drops the rail before Kinoko's scene (and its RailManager) changes.
 void stop();
 bool active()const{return active_;}
 Vec3 eye()const{return eye_;}
 Vec3 target()const{return target_;}
 // FOV as last written to the camera (a switch's fovStart applies next tick).
 float fov()const{return appliedFov_;}
 // QA: total authored length in frames, and the shot index being shown.
 unsigned frames()const;
 unsigned frame()const{return frame_;}
 std::vector<IntroPathFrame> previewFrames()const;
 int shot()const{return current_;}
private:
 struct Camera {
  std::uint8_t type=0,next=0xff,route=0xff;std::uint16_t pathSpeed=0,fovSpeed=0,viewSpeed=0;
  Vec3 position,viewStart,viewEnd;float fovStart=0,fovEnd=0,time=0;
 };
 void begin(int index);
 std::vector<Camera> cameras_;
 int opening_=-1,current_=-1;unsigned shots_=0;
 unsigned frame_=0;
 bool active_=false;
 float elapsed_=0,fov_=60,appliedFov_=60,viewDistance_=0;
 Vec3 eye_,target_,viewOffset_;
 std::unique_ptr<Kinoko::Field::RailInterpolator> rail_;
};
}
