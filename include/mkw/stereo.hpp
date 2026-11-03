#pragma once
#include <mkw/math.hpp>
#include <algorithm>

// Stereoscopic 3D on New 3DS (docs/STEREO_3D.md), built the way Mario Kart 7
// builds it. MK7's code (USA rev 1) calls the 3DS SDK's stereo camera,
// nn::ulcd::StereoCamera::CalculateMatrices (0x230244): both eyes keep the
// centre camera's orientation, move along its x axis and skew their frusta to
// meet on a reference plane at a fixed view depth, the depth level. The
// strength comes from the console's stereo display settings, so the port
// reads the same ones. Every constant below was read from MK7's code or
// fitted to its own per-eye framebuffers (tools/mk7_reference.py).
//
// With parallel eyes every view-dependent input (lights, fog, environment
// UVs, billboards, effect draw lists) is the same for both eyes and only
// the projection differs.
//
// Disparity is measured in top-screen pixels as right-eye x minus left-eye x:
// positive behind the screen, negative in front of it.
namespace mkw::stereo {
// Dad requested stronger depth than the MK7 reference. Keep its convergence
// planes and slider range; amplify the resulting binocular disparity only.
inline constexpr float DepthStrength=1.5f;
// The console's stereo display settings (config block 0x50005, which the SDK
// camera reads at start-up), in mm: the viewer's eye separation and distance,
// the top screen's size, and the limit parallax (the disparity of the far
// clip plane at full strength). Defaults: Azahar's values (an original 3DS).
struct Display {
    float eyeSeparation=62.f,viewingDistance=289.f,screenWidth=76.8f,screenHeight=46.08f,limitParallax=10.f;
    // Top-screen pixels per mm (the SDK camera scales by the 240-row height).
    float pixelsPerMm() const {return 240.f/screenHeight;}
    bool valid() const {
        auto ok=[](float v){return v>0.f&&v<1e4f;};
        return ok(eyeSeparation)&&ok(viewingDistance)&&ok(screenWidth)&&ok(screenHeight)&&ok(limitParallax);
    }
};
// One of MK7's 3D-scene stereo cameras. CalculateMatrices puts each eye
// slider*factor*limit*far/(far-level)/2 mm (at the reference plane) from the
// centre, so a point at view depth d has a disparity of
//   slider*factor*limit*pixelsPerMm*far/(far-level)*(1-level/d)
// pixels. MK7 keeps the level at a fixed view depth; levelRatio is that depth
// over the view depth of the shot's subject, which carries it to MKW's camera
// distances.
struct Scene {
    float factor;      // CalculateMatrices' factor argument
    float farFactor;   // far/(far-level) of MK7's projection
    float levelRatio;  // depth level / the subject's view depth
};
// Races: the kart camera's parameters (0x663E98 +0x28 level 45, +0x34 factor
// 0.36, near 3, far 50000), constant on every race camera (the intro
// flyover, replays and the goal included). MK7's kart shows +2.8 px on the
// driver's cap, +3.4 on the back and +1.8 on the rear at full strength
// (level 45 against the kart body's view depth of about 54). The port's
// subject is the kart position's depth along the chase camera, which lies a
// little beyond the visible kart, so its ratio, 0.802, is the one that puts
// the port's Mario kart on the same disparities (within 0.2 px on average).
// (Gliding, which MKW lacks, eases MK7's level to 30 at 2% per frame.)
inline constexpr Scene Race{.36f,50000.f/(50000.f-45.f),.802f};
// Menus with selection models: the garage (character and kart choice): level
// 110, factor 0.6, near 10, far 810. MK7's kart and driver show +7.05 px
// (view depth about 136.6, so the level is 0.805 of it) and its back wall
// +11.3 px. The port's subject is the menu models' origin depth; their
// visible surface lies nearer and the screen placement scales their
// disparity by about 0.96, so the ratio that puts the port's models on
// MK7's +7.05 px is 0.774.
inline constexpr Scene Garage{.6f,810.f/(810.f-110.f),.774f};
// The menu backdrop pane stands where MK7's garage back wall does, +11.3 px
// at full strength: 1.126 times the models' origin depth.
inline constexpr float GarageBackdropRatio=1.126f;
// Screen-space layers through MK7's layout camera, CalculateMatricesReal
// (0x425C98): eyes eyeSeparation apart, viewingDistance from the screen
// plane, one unit per pixel there. A layer `depth` pixels behind the screen
// plane (negative: in front) has a disparity of
//   slider*eyeSeparation*pixelsPerMm*depth/(viewingDistance*pixelsPerMm+depth).
// Race HUD (item window, timer): 4.07 px behind the screen at full strength.
inline constexpr float HudLayer=19.2f;
// The countdown digits come out of the screen: -3.05 px. The port's GO,
// FINISH and position number use it too.
inline constexpr float CountdownLayer=-14.1f;
// Menu and pause panes, labels and buttons: on the screen plane.
inline constexpr float PaneLayer=0.f;

struct Frame {
    // Disparity at infinity in pixels with the slider applied (0: 2D).
    float separation=0;
    // View depth of the reference plane.
    float convergence=1;
    bool active() const {return separation>0;}
    // Disparity of a point at view depth `depth` (> 0 in front of the camera).
    float parallax(float depth) const {return depth>0?separation*(1.f-convergence/depth):0.f;}
};

// Slider 0..1, the scene's camera and its subject's view depth (the player's
// kart along the chase camera, or the menu models) to this frame's stereo.
inline Frame frame(float slider,const Display& display,const Scene& scene,float subjectDepth){
    Frame f;
    if(!(slider>0))return f;
    f.separation=DepthStrength*std::min(slider,1.f)*scene.factor*display.limitParallax*display.pixelsPerMm()*scene.farFactor;
    f.convergence=std::max(scene.levelRatio*subjectDepth,1.f);
    return f;
}
// Disparity in pixels of a screen layer `depth` pixels behind the screen plane.
inline float layerParallax(float slider,const Display& display,float depth){
    if(!(slider>0))return 0.f;
    const float k=display.pixelsPerMm();
    return DepthStrength*std::min(slider,1.f)*display.eyeSeparation*k*depth/(display.viewingDistance*k+depth);
}

// Projection rows m[r][c] (clip = m * view). `horizontal` is the clip row of
// the screen's horizontal axis and `sign` +1 when screen-right is +NDC on it
// (citro3d's tilted matrices use row 1 and -1). eye: -1 left, +1 right.
//
// Perspective: a point at clip w = view depth d moves by eye*parallax(d)/2
// pixels, the off-axis projection of a camera moved eye*e/2 along its x axis
// whose frustum is skewed to meet the centre one at the reference plane.
inline void perspectiveEye(float (&m)[4][4],unsigned horizontal,float sign,float eye,const Frame& f){
    const float a=sign*eye*f.separation/400.f;
    for(unsigned c=0;c<4;++c)m[horizontal][c]+=a*m[3][c];
    m[horizontal][3]-=a*f.convergence;
}
// Screen-space (orthographic, w=1) layer at a fixed disparity in pixels.
inline void screenEye(float (&m)[4][4],unsigned horizontal,float sign,float eye,float parallax){
    const float a=sign*eye*parallax/400.f;
    for(unsigned c=0;c<4;++c)m[horizontal][c]+=a*m[3][c];
}
// Culling projection containing both eyes' frusta: an eye sees
// |x| <= w + k|w-D| (k = separation/400), inside |x| <= (1+k)w + kD. Replacing
// clip w with that bound loosens every plane derived from w, never tightens.
inline void cullingUnion(float (&m)[4][4],const Frame& f){
    const float k=f.separation/400.f;
    for(unsigned c=0;c<4;++c)m[3][c]*=1.f+k;
    m[3][3]+=k*f.convergence;
}
}
