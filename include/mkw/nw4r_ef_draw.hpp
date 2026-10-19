#pragma once
// NW4R EF drawing for the 3DS: the retail draw strategies (billboard 0x8003e290 /
// 0x8003f680, directional 0x80040a90 / 0x80042310, stripe 0x80045f30 with tube
// 0x80045d40), plus closed smooth ribbons (0x80048f20), produce view-space quads
// and strips, and each GX TEV setup maps to a PICA
// combiner program. Per-particle TEV registers reach the GPU as the vertex colour and a
// colour-lookup texel (texture unit 2); register values a program reads from stage
// constants split batches, like EF's GXSetTevColor per particle.
#include "mkw/nw4r_ef_internal.hpp"
#include <mkw/visibility.hpp>

namespace mkw::ef {
// PICA combiner sources and operations used by the translated programs.
enum class Src : u8 {Vertex,Tex0,Tex1,Lookup,Const,Prev,Buffer};
enum class Operand : u8 {Color,OneMinusColor,Alpha,OneMinusAlpha};
enum class Func : u8 {Replace,Modulate,Add,AddSigned,Interpolate,Subtract,MultiplyAdd};
// A value carried by the vertex colour, the lookup texel or a stage constant: one of the
// TEV registers C0..C2 / K0..K3 (per particle) or a literal.
enum Value : u8 {RegC0,RegC1,RegC2,RegK0,RegK1,RegK2,RegK3,ValZero,ValQuarter,ValHalf,ValOne,ValNone=0xff};
struct Channel { u8 rgb=ValNone,alpha=ValNone; bool rgbFromAlpha=false; };
struct CombinerStage {
    Func rgbFunc=Func::Replace,alphaFunc=Func::Replace;
    Src rgbSrc[3]{Src::Prev,Src::Prev,Src::Prev},alphaSrc[3]{Src::Prev,Src::Prev,Src::Prev};
    Operand rgbOp[3]{},alphaOp[3]{Operand::Alpha,Operand::Alpha,Operand::Alpha};
    u8 rgbScale=1,alphaScale=1;
    Channel constant;
    bool bufferRgb=false,bufferAlpha=false;
};
struct Material {
    u8 stages=0;
    CombinerStage stage[6];
    Channel vertex,lookup;
    // A projected layer (DrawSetting Tex1Proj/Tex2Proj) goes on PICA texture unit 0 in
    // projection mode, which divides by the coordinate's w per pixel like GX does for a
    // 3x4 texture matrix. swapUnits: GX layer 1 on unit 0 and layer 0 on unit 1 (the
    // combiner's Tex0/Tex1 sources are swapped to match).
    bool projected=false,swapUnits=false;
    bool valid=false;
};
// Translates the emitter's GX TEV setup; false if it is not one of the ported programs.
bool translateMaterial(const DrawSetting& d,Material& out);

// Texture units 0..2: unit 0 carries a projective w (1 unless the material is projected).
struct DrawVertex { float x,y,z,s0,t0,q0,s1,t1,s2,t2; u8 color[4]; };
struct DrawBatch {
    const EmitterResource* res;
    u32 first,count;
    u32 texture[2]; // RaceEffects texture index per PICA unit, 0xffffffff none
    u8 wrap[2]; // GX wrap S | T<<2 per PICA unit (0 clamp, 1 repeat, 2 mirror)
    u8 constant[6][4];
    u8 cull; // 0 none, 1 GX front, 2 GX back
    u8 alphaRef0,alphaRef1;
};
// GPU_TRIANGLES draws must contain whole triangles, including a batch clipped at
// the end of a fixed GPU buffer. Shared with the host overflow regression.
constexpr size_t triangleVertexCount(size_t requested,size_t available){
    const size_t count=requested<available?requested:available;
    return count-count%3;
}
constexpr u32 drawBatchVertexCount(const DrawBatch& batch,size_t vertexCount){
    return batch.first<vertexCount?u32(triangleVertexCount(batch.count,vertexCount-batch.first)):0;
}
struct TextureSize { u16 width,height,paddedWidth,paddedHeight; };
struct DrawList {
    static constexpr u32 LookupSize=64,LookupTexels=LookupSize*LookupSize;
    std::vector<DrawVertex> vertices;
    std::vector<DrawBatch> batches;
    std::vector<u32> lookup; // RGBA per texel index (r in the low byte)
    void clear(){vertices.clear();batches.clear();lookup.clear();}
};
// Builds the draw list for every effect in creation order (DrawOrder by weight inside an
// effect). view: world to camera (NW4R DrawInfo view matrix, camera looking down -z).
// Stores only the complete-triangle prefix up to vertexLimit, but still advances
// every particle's draw axis so later frames match an unlimited draw list.
void buildDrawList(System& s,const std::vector<Material>& materials,const TextureSize* textures,u32 textureCount,
                   const Mtx34& view,DrawList& out,u32 vertexLimit=~0u,const mkw::ClipFrustum* frustum=nullptr);
}
