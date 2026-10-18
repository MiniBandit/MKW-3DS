#pragma once
#include "mkw/renderer.hpp"
#include <functional>
#include <memory>
#include <vector>
namespace mkw {
class RaceEffects;
// Retail course-explosion particles, once per simulation step after it: the
// rk_bombHei burst on the first explosion step of each GBA Shy Guy Beach
// cannonball (PAL 0x806D15A8) and Bowser's Castle fireball (PAL 0x807719E8).
// bombHei is RaceEffects::find("rk_bombHei"). Presentation only.
void courseExplosionEffects(RaceEffects& effects,int bombHei);
// Course-object presentation. Original per-MDL0 models and animation banks
// (tools/cook_course_objects.py) are drawn from Kinoko's live object state, or
// for placements Kinoko does not simulate, from their retail presentation
// rules. Owns shared meshes only, never physics. Call reset after a GPU wait.
class CourseObjects {
public:
    CourseObjects();
    ~CourseObjects();
    bool loadCoconut();
    bool loadCourse(int courseId);
    // The race scene was (re)created: re-link placements to new objects.
    void invalidate();
    // Records this frame's sky and object draws (poses, skinning, materials) on
    // the job worker; call after Renderer::begin with the same tick and camera
    // as drawSky/draw, which then replay them in order. Without it they record
    // inline. The race must not step in between.
    void prepare(const Renderer& renderer,unsigned tick,Vec3 camera);
    // tick: simulation frames since the race scene began.
    void draw(Renderer& renderer,unsigned tick,Vec3 camera);
    // Transparent tide layers, after opaque scenery and racers, before effects.
    void drawWater(Renderer& renderer);
    // Sky-pass objects (suns, Rainbow Road's Earth glow): drawn after the sky,
    // before the course, so walls and scenery cover them.
    void drawSky(Renderer& renderer,unsigned tick,Vec3 camera);
    // Draws an object's Bob-omb blast (RaceHud::drawBombBlast); without one the
    // plain bomb_core mesh stands in.
    void setBlastDrawer(std::function<void(Renderer&,const C3D_Mtx&,unsigned)> drawer);
    // GPU-idle pause: keep controllers/physics links while releasing all meshes.
    void suspendGraphics();
    bool resumeGraphics();
    void reset();
    // Renderer::moveToVram for every object model's textures; returns the
    // number moved. GPU idle (Renderer::waitForGpu).
    unsigned moveTexturesToVram(Renderer& renderer,std::size_t reserve);
    unsigned palmCount() const;
    unsigned modelCount() const;
    // QA review: one placement per object type, with its live world bounds.
    struct ReviewTarget {char name[32];unsigned visual;Vec3 center;float radius;};
    std::vector<ReviewTarget> reviewTargets();
    bool reviewBounds(unsigned visual,Vec3& center,float& radius);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
