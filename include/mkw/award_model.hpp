#pragma once
#include <mkw/animation.hpp>
#include <mkw/native_mii_head.hpp>
#include <mkw/mii_body.hpp>
#include <mkw/award_model_assets.hpp>
#include <mkw/award_cast.hpp>
#include <mkw/award_placement.hpp>
#include <mkw/asset_loader.hpp>
#include <mkw/object_bank.hpp>
#include <memory>
#include <string>
#include <vector>

namespace mkw {
// One independently animated ceremony actor. Parts share textures through the
// renderer cache, but never share mutable vertices or an animation palette.
class AwardModel {
public:
 static const AwardModelAsset* find(const char* archive,const char* model);
 // Asset-worker safe. A failure leaves the previous actor intact. Destroy or
 // replace a published actor only after its last GPU frame has completed.
 bool load(const AwardModelAsset& asset,bool menuLighting=false);
 void reset();
 bool play(const char* clip);
 bool setColorFrame(const char* clip,float frame);
 // On the pose/render thread, before recording draws for this frame.
 void update(float frame,unsigned tick);
 void prepareVram(Renderer& renderer);
 bool prepareMii(const NativeMii&);
 void freezeLighting();
 void draw(Renderer& renderer,const C3D_Mtx& transform,bool backdrop=false,bool menuLayer=false) const;
 bool ready()const{return !parts_.empty();}
 bool animated()const{return animation_!=nullptr;}
 float duration()const{return animation_&&clip_>=0?animation_->duration(clip_):0;}
 bool looping()const{return animation_&&clip_>=0&&animation_->loop(clip_);}
private:
 std::string miiStem_;MiiBodySpec mii_{};std::unique_ptr<NativeMiiHead> head_;
 std::vector<std::unique_ptr<Model>> parts_;
 std::unique_ptr<DriverAnimation> animation_;
 std::unique_ptr<ObjectBank> materials_;
 std::vector<SubmeshOverride> overrides_;
 struct Billboard {unsigned slot,mode;};
 std::vector<Billboard> billboards_;
 mutable std::vector<float> billboardPalette_;
 int clip_=-1,face_=-1;
};
// A complete cast is published together. The scene must wait for done() before
// inspecting loaded; a missing actor/clip never produces a partial ceremony.
class AwardCastJob final:public AssetJob {
public:
 explicit AwardCastJob(const AwardCast& cast):cast_(cast){label="award cast";}
 void run()override;
 void updateActors(float frame,unsigned tick);
 // Actor positions use the ceremony's world center, supplied by scene state.
 // The stage/environment has its own original transform and draw order.
 void drawActors(Renderer& renderer,bool teams,Vec3 center)const;
 void drawStand(Renderer& renderer,const AwardStandPlacement& placement)const;
 const char* detail()const override{return failedAsset_.c_str();}
 bool loaded=false;
 std::unique_ptr<AwardModel> stand;
 std::vector<std::unique_ptr<AwardModel>> podium,audience;
private:
 AwardCast cast_;
 std::string failedAsset_;
};
}
