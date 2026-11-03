#pragma once
#include <mkw/license_identity.hpp>
#include <mkw/driver_controller.hpp>
#include <mkw/renderer.hpp>
namespace mkw {
// A single 3DS CFL head, shared by menu/race/photo callers. Geometry and
// expression masks are built once outside a frame, never during a race tick.
class NativeMiiHead {
public:
 enum Expression : unsigned {Normal,Blink,Smile,Anger,Sorrow,Surprise,Count};
 NativeMiiHead();~NativeMiiHead();
 NativeMiiHead(const NativeMiiHead&)=delete;
 NativeMiiHead& operator=(const NativeMiiHead&)=delete;
 // Main thread, GPU drained. Resolution 64 for racing, 128 for still photos.
 // Failure preserves the previous head. No Wii appearance conversion.
 bool load(const NativeMii&,unsigned resolution=64);
 bool ready()const;
 const Model* shadowModel()const;
 std::uint32_t skinConstant()const;
 static Expression expression(const DriverPose&);
 void draw(Renderer&,const C3D_Mtx&,Expression=Normal)const;
 void drawAtScreen(Renderer&,const C3D_Mtx&,float x,float y,float sx,float sy,Expression=Normal)const;
private:
 struct Impl;std::unique_ptr<Impl> impl_;
 static void bind(void*,std::size_t);
};
}
