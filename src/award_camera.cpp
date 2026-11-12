#include <mkw/award_camera.hpp>
#include <mkw/renderer.hpp>

namespace mkw {
void beginAwardCamera(Renderer& renderer,const AwardCameraClip& clip,float frame){
 const auto matrices=awardCameraMatrices(sampleAwardCamera(clip,frame));
 renderer.beginSourceCamera(matrices.view.data(),matrices.projection.data());
}
}
