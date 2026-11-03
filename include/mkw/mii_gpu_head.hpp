#pragma once
#include <mkw/mii_head.hpp>
#include <mkw/renderer.hpp>
namespace mkw {
class MiiGpuHead {
 std::unique_ptr<Model> model_;
 std::array<unsigned,9> kinds_{};
 static void bind(void*,std::size_t);
public:
 // Call outside a frame, after outstanding draws using this instance finish.
 // Scene-specific original lighting/camera is supplied by the caller.
 bool load(const MiiHead&,std::uint32_t lighting,std::uint32_t ambient);
 bool loadIcon(const MiiHead&);
 void draw(Renderer&,const C3D_Mtx&) const;
 void drawAtScreen(Renderer&,const C3D_Mtx&,float x,float y,float scaleX,float scaleY) const;
 std::uint32_t skinConstant() const;
 bool ready() const{return bool(model_);}
};
}
