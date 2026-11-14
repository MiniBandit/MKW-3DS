#pragma once
#include <cstdint>
#include <cstddef>
namespace mkw::stadium {
// Retail EFB 608x456: WLscreenGC copies (152,184,304,228).
// On a 400x240 LCD this is (100,184*10/19,200,120).
inline constexpr unsigned CaptureX=100,CaptureY=96,CaptureWidth=200,CaptureHeight=120;
inline constexpr unsigned TextureWidth=128,TextureHeight=256;
struct Uv {float u,v;};
// The capture is a straight copy of the finished scene's crop (no drawing):
// 8x8 tiles of the top buffer, rows CopyRow.. (x scale) and columns
// CopyColumn..+CopyColumns, into a texture that is 256 x scale tall.
// Measured in Azahar (Waluigi Stadium against the earlier rendered capture):
// the buffer's row for landscape x counts up with x, and the texture samples
// row T from memory row height-1-T, so crop x lands at texel row CopyTop-x.
inline constexpr unsigned CopyRow=96,CopyRows=208,CopyColumn=24,CopyColumns=120;
inline constexpr float CopyTop=251.f;
inline constexpr Uv copyUv(float u,float v,unsigned scale){
 return {v*CaptureHeight/TextureWidth,(CopyTop+.5f*float(scale-1)-u*CaptureWidth)/TextureHeight};
}
inline constexpr Uv displayUv(float u,float v){return {v*CaptureHeight/TextureWidth,(1.f-u)*CaptureWidth/TextureHeight};}
}
