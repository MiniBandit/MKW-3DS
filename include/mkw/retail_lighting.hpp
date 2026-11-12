#pragma once
#include <cstdint>
#include <array>
#include <cstring>
#include <mkw/math.hpp>

namespace mkw {
// Retail Mario Kart Wii course lighting (EGG posteffect), reduced to what the
// 3DS renderer evaluates. Addresses are USA main.dol / StaticR (relocated at
// 0x8050BF60); the full derivation is in docs/COURSE_LIGHTING.md.
//
// Colours are 0xRRGGBBAA.

// EGG::LightObject after LightObject::SetBinaryInner (0x8022BF30) and Calc
// (0x8022B54C): flags (1 enable, 0x20 light-texture light, 0x40 G3D/GX light,
// 0x200 colour, 0x400 alpha), coordinate space (0 world, 1 world half-angle,
// 2 view, 3 view -Z, 4 view +Z; CalcView 0x8022B658), GX type (1 directional),
// the unit aim-pos direction, colour*intensity (saturated) and dark colour.
struct RetailLight { uint16_t flags; uint8_t space,type; float direction[3]; uint32_t color,dark; };
// One LTEX entry: BLIGHT light, gradient texture (LightTexture::initialize
// 0x8022D1F4) and the LTEX intensity (LightObject::CalcFinalDirColor 0x8022B87C).
struct RetailTextureLight { uint8_t light,gradient; float intensity; };
struct RetailLightTexture { uint8_t type,count; RetailTextureLight entries[4]; };
// One BFG fog state (StaticR 0x805A30C8): GXFogType, eye-depth start/end,
// colour, enabled and the per-frame fade toward a newly selected state.
struct RetailFog { uint8_t type,enabled; float start,end; uint32_t color; float fade; };
struct CourseLighting {
    const char* archive;
    RetailLight lights[16];
    uint32_t ambient[16];
    // EGG::LightManager colour (LGHT 0x14): light textures clear to it first.
    uint32_t clear;
    RetailLightTexture textures[2]; // lm_0, lm_1
    uint8_t fogCount;
    RetailFog fog[4];
};

// GMDL v4 per-submesh lighting word (tools/gx_material.py retail_lighting_word).
namespace retail {
enum : uint32_t {
    StaticLighting=1u<<28, // immutable per-course vertex light; material animation remains live
    FixedEnvironment=1u<<29, // load-time baked EnvCamera coordinates (bit27 belongs to road opacity)
    Retail=1u<<0,          // submesh uses retail course lighting
    Menu=1u<<21,           // racer material alpha/reflection path (menu or race)
    PrimaryEnvironment=1u<<22, // GX EnvCamera feeds texture 0; v5 stores its authored SRT
    CoplanarLayer=1u<<23, // source translucent course decal sharing an earlier surface
    RaisedRoadLayer=1u<<25, // authored raised road detail; PICA slope/depth precision guard
    AuthoredOpaqueBlend=1u<<26, // source DrawOpa blend layer; order only, no depth bias
    Fog=1u<<1,             // material fog index 0 (G3D fog 0)
    ChannelShift=2,        // 0 unlit (raster = vertex/register colour), 1 GX lit, 2 GX lit + light textures
    ChannelMask=3u<<2,
    LightSetShift=4,       // G3D light set: light k with ambient 0 (StaticR 0x8055CD68)
    LightSetMask=7u<<4,
    AlphaLit=1u<<7,        // GX alpha channel lighting enabled
    DiffuseShift=8,        // GXDiffuseFn: 0 none, 1 sign, 2 clamp
    DiffuseMask=3u<<8,
    AmbientVertex=1u<<10,  // ambient colour source is the vertex colour
    // Light-texture combiners (see Renderer::bindRetailLightTexture)
    PrimaryShift=11,       // 0 none, 1 raster+lm_0*rasA, 2 rasA+lm_0*rasA
    PrimaryMask=3u<<11,
    PrimaryScaleShift=13,  // GX colour scale of that stage
    PrimaryScaleMask=3u<<13,
    SecondaryShift=15,     // lm_1 mask: 0 none, 1 previous alpha, 2 raster colour
    SecondaryMask=3u<<15,
    AlbedoLerp=1u<<17,     // albedo stage is lerp(C2 = submesh constant, albedo, P)
    AlbedoScaleShift=18,   // GX colour scale of the albedo stage
    AlbedoScaleMask=3u<<18,
    AlbedoTint=1u<<24,     // Mii C0 multiplies lit albedo before the lm_1 specular add
    AlbedoAdd=1u<<20,      // albedo stage adds Submesh::add (GX C1, Thwomps)
};
}

// 64x4 gradient textures of LightTexture::initialize (0x8022D1F4): fill
// operation (CpuTexture gradient 0x80217A3C) and the [start,end] texel range.
struct LightGradient { uint8_t op,start,end; };
inline constexpr LightGradient LightGradients[10]={
    {0,33,63},{0,1,63},{0,17,63},{1,33,63},{2,33,63},{3,33,63},{4,33,63},{0,5,35},{5,11,41},{5,14,44}};
// Texel i of gradient texture `index` after fillGradient's half-width rotation
// (0x802184CC): value in [0,255].
uint8_t lightGradientTexel(unsigned index,unsigned texel);
// EGG::LightObject::CalcView (0x8022B658): the light's travel direction in
// view space. view holds the world-to-view rotation rows.
Vec3 retailLightViewDirection(const RetailLight& light,const float view[3][3]);
// EGG::LightTexture (0x8022D964..0x8022DF38): renders light texture `which`
// (0 = lm_0, 1 = lm_1) into rgba (32*32*4 bytes, GX row order, 0xRRGGBBAA
// bytes R,G,B,A per texel) from each light's view-space direction.
void retailLightTexture(const CourseLighting& course,unsigned which,const Vec3 viewDirections[16],uint8_t* rgba);
// Exact camera-dependent inputs after GX S1.10 quantization. With an immutable
// CourseLighting table, equal keys guarantee byte-identical generated textures.
void retailLightTextureKey(const CourseLighting& course,unsigned which,const Vec3 viewDirections[16],int (&key)[12]);
// True when light texture `which` depends on the camera (world-space lights).
bool retailLightTextureDynamic(const CourseLighting& course,unsigned which);
// Same GX ambient/diffuse equation evaluated at a fixed, model-space reference.
std::array<float,4> viewLightFactor(const CourseLighting* course,uint32_t lighting,uint32_t ambient,Vec3 normal,const float view[3][3]);
std::array<float,4> staticLightFactor(const CourseLighting* course,uint32_t lighting,uint32_t ambient,Vec3 normal);
bool normalEnvironment(uint32_t lighting,uint32_t mode,uint32_t constant);
// GX fog factor for a fog type at normalized eye depth ze ((z-start)/(end-start)).
float retailFogFactor(unsigned type,float ze);
// Fog object state (StaticR 0x805A31D0): fades from the current to a target
// BFG state by each state's per-frame fade; DK Summit fades on the race timer.
struct RetailFogState {
    uint8_t current=0,target=0;
    bool started=false;
    float progress=1.f;
    RetailFog from{},value{};
    void reset(const CourseLighting& course);
    // targetState: KMP AREA type 2 parameter (or -1 outside); timed: course 6.
    void step(const CourseLighting& course,int targetState,bool timed,uint32_t raceFrame);
};
}
