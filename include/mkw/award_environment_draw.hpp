#pragma once
#include <mkw/award_environment.hpp>
#include <mkw/award_model.hpp>
#include <mkw/race_effects.hpp>
#include <game/field/RailInterpolator.hh>

namespace mkw {
class AwardEnvironmentJob final:public AssetJob {
public:
 explicit AwardEnvironmentJob(const AwardEnvironmentDef& def):def_(def){label="award environment";}
 void run()override;
 bool loaded=false;
 const char* detail()const override{return failedAsset_.c_str();}
 void updateDecorations(float frame,unsigned tick);
 void prepareVram(Renderer& renderer);
 void drawBackground(Renderer& renderer)const;
 void drawDecorations(Renderer& renderer)const;
 // DemoJugemu's CHR0 includes its world-space flight. Returns whether the
 // authored intro performer is visible, for the matching sound handle.
 bool drawIntroPerformers(Renderer& renderer,float frame,unsigned tick);
 bool stepIntroEffects(float frame,bool active,unsigned sceneTick);
 void drawEffects(Renderer& renderer,const float* camera);
 // Scene-controlled objects (Lakitu, route movers) use the same resident mesh
 // but supply their own live transform and animation time.
 void drawObject(Renderer& renderer,unsigned index,const C3D_Mtx& transform,float frame,unsigned tick);
private:
 struct Entry {unsigned asset;const char* clip;std::unique_ptr<AwardModel> model;bool controlled;};
 const AwardEnvironmentDef& def_;
 std::unique_ptr<AwardModel> course_,sky_;
 std::vector<Entry> entries_;
 std::vector<int> slots_;
 std::vector<C3D_Mtx> transforms_;
 struct Mover {
  unsigned object,tick=0,frames=0;bool started=false;
  std::unique_ptr<Kinoko::Field::Rail> rail;
  std::unique_ptr<Kinoko::Field::RailSmoothInterpolator> interpolator;
 };
 std::vector<Mover> movers_;
 std::unique_ptr<RaceEffects> effects_;
 std::vector<Texture> effectTextures_;
 int tailSpec_=-1;
 bool tailActive_=false;
 int confettiSpec_=-1,confettiLastSpec_=-1;
 int fireworksSpec_=-1;
 bool confettiStarted_=false;
 std::string failedAsset_;
};
}
