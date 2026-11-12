#pragma once
#include <mkw/award_environment_draw.hpp>
#include <mkw/award_trophy.hpp>
#include <mkw/award_parade.hpp>
#include <mkw/award_drive.hpp>

namespace mkw {
// One worker-owned transaction. The render thread may inspect published
// resources only after done(); destruction of published draws requires a GPU drain.
class AwardSceneAssets final:public AssetJob {
public:
 AwardSceneAssets(const AwardResults& results,unsigned environment,RaceTeam winner,unsigned trophyRank);
 void run()override;
 bool loadPodium();
 const char* detail()const override{return failure_.c_str();}
 bool loaded=false;
 std::unique_ptr<AwardEnvironmentJob> environment;
 std::unique_ptr<AwardCastJob> cast;
 std::unique_ptr<AwardDrive> drive;
 std::unique_ptr<AwardParadeJob> parade;
 std::unique_ptr<AwardTrophyJob> trophy;
private:
 AwardResults results_;
 unsigned environment_,trophyRank_;
 RaceTeam winner_;
 std::string failure_;
};
}
