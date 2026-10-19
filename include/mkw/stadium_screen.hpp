#pragma once
#include <cstdint>
#include <cstddef>
namespace mkw::stadium {
// Retail EFB 608x456: WLscreenGC copies (152,184,304,228).
// On a 400x240 LCD this is (100,184*10/19,200,120).
inline constexpr unsigned CaptureX=100,CaptureY=96,CaptureWidth=200,CaptureHeight=120;
inline constexpr unsigned TextureWidth=128,TextureHeight=256;
struct Uv {float u,v;};
inline constexpr Uv displayUv(float u,float v){return {v*CaptureHeight/TextureWidth,(1.f-u)*CaptureWidth/TextureHeight};}
}
