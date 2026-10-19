#pragma once
#include <mkw/animation.hpp>
#include <memory>
namespace mkw {
class VehicleAnimation;
// Per-racer mutable animation; immutable textures use Model's shared cache.
class LumaVisual {
public:
    bool load();
    void prepare(const ViewFrustum& view,const VehicleAnimation& vehicle,unsigned tick);
    void draw(Renderer& renderer) const;
private:
    std::unique_ptr<Model> model_;
    std::unique_ptr<DriverAnimation> animation_;
    C3D_Mtx matrix_{};
    int wait_=-1;
    bool visible_=false;
};
}
