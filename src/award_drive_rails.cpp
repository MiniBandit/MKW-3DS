#include <mkw/award_drive_rails.hpp>
#include <mkw/award_drive_data.hpp>
namespace mkw {
void AwardCameraRails::prepare(){
 clear();static_assert(std::size(AwardDriveRails)==7);
 for(unsigned i=0;i<paths_.size();++i){
  auto& path=paths_[i];
  Kinoko::System::MapdataPointInfo info(reinterpret_cast<const Kinoko::System::MapdataPointInfo::SData*>(AwardDriveRails[i]));
  if(info.setting(0)){
   path.rail=std::make_unique<Kinoko::Field::RailSpline>(0,&info);
   path.cursor=std::make_unique<Kinoko::Field::RailSmoothInterpolator>(10.f,*path.rail);
  }else{
   path.rail=std::make_unique<Kinoko::Field::RailLine>(0,&info);
   path.cursor=std::make_unique<Kinoko::Field::RailLinearInterpolator>(10.f,*path.rail);
  }
 }
}
void AwardCameraRails::select(unsigned route){
 current_=route<paths_.size()?paths_[route].cursor.get():nullptr;
 if(current_){current_->init(0,0);current_->setStopAtEnd(true);}
}
void AwardCameraRails::step(){
 if(!current_)return;
 const float a=current_->curPoint().setting[0],b=current_->nextPoint().setting[0];
 current_->setCurrVel(a+(b-a)*current_->segmentT());current_->calc();
}
}
