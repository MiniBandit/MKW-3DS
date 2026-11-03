#pragma once
#include "mkw/renderer.hpp"
#include <memory>
#include <vector>
namespace mkw {
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
    // Simulation event, before prepare: restart this racer's authored lava splash.
    void triggerLavaSplash(unsigned racer,Vec3 position,float scale,unsigned tick);
    // Future race overlays share LINEAR with optional per-instance skin copies.
    void setAllocationReserve(std::size_t bytes);
    // Records this frame's sky and object draws (poses, skinning, materials) on
    // the job worker; call after Renderer::begin with the same tick and camera
    // as drawSky/draw, which then replay them in order. Without it they record
    // inline. The race must not step in between.
    void prepare(const Renderer& renderer,unsigned tick,Vec3 camera);
    // Before Renderer::begin: supply the placed stadium display faces for
    // conservative capture visibility, independently of their frame/stands.
    void requestStadiumScreen(Renderer& renderer);
    // tick: simulation frames since the race scene began.
    void draw(Renderer& renderer,unsigned tick,Vec3 camera);
    // Transparent tide layers, after opaque scenery and racers, before effects.
    void drawWater(Renderer& renderer);
    // Only bobbing/deforming bridge supports; never all moving platforms.
    bool suppressWheelContact(Vec3 center,float radius) const;
    const std::vector<Renderer::ShadowReceiver>& shadowReceivers(Vec3 origin,float radius);
    // Sky-pass objects (suns, Rainbow Road's Earth glow): drawn after the sky,
    // before the course, so walls and scenery cover them.
    void drawSky(Renderer& renderer,unsigned tick,Vec3 camera);
    void reset();
    void suspendGraphics();
    bool resumeGraphics();
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
