#pragma once
#include <mkw/staff_ui_data.hpp>
#include <algorithm>
namespace mkw {
// USA StaffRoll::OnInit 806259b0..80625cac: the table's three pairs
// bind Center, Top, Bottom, in that order (not screen reading order).
inline const FlowLayoutDef* const StaffCreditLayouts[]={
 &FlowStaffRollCenterTitleLayout,&FlowStaffRollCenterNameLayout,
 &FlowStaffRollTopTitleLayout,&FlowStaffRollTopNameLayout,
 &FlowStaffRollBottomTitleLayout,&FlowStaffRollBottomNameLayout};
inline const FlowControl* const StaffCreditControls[]={
 FlowStaffRollCenterTitleVariants,FlowStaffRollCenterNameVariants,
 FlowStaffRollTopTitleVariants,FlowStaffRollTopNameVariants,
 FlowStaffRollBottomTitleVariants,FlowStaffRollBottomNameVariants};
struct StaffMovieWindow {unsigned start,end,slot;};
// USA EndingMovie::AfterControlUpdate 80626788..806267dc. Windows
// change in four authored sections, independently of credit-page changes.
inline constexpr StaffMovieWindow StaffMovieWindows[]={
 {60,1860,0},{1860,3660,1},{3660,5340,0},{5340,7260,1}};
struct StaffMoviePlacement {unsigned slot;float alpha;};
inline StaffMoviePlacement staffMoviePlacement(unsigned slot,unsigned tick){
 for(const auto& w:StaffMovieWindows)if(w.slot==slot&&tick>=w.start&&tick<w.end+30){
  const float alpha=tick<w.end?std::min(float(tick-w.start)/29.f,1.f):
   std::max(1.f-float(tick-w.end)/29.f,0.f);
  return {slot,alpha};
 }
 return {slot,0};
}
struct StaffPhotoPlacement {float x,y,scale;};
inline constexpr StaffPhotoPlacement staffPhotoPlacement(bool variant){
 // The true photo has a raised podium between Luigi and Mario. Keep the
 // same full-body scale; y now explicitly denotes the feet/podium top.
 // A 3DS projection adaptation, pending target visual confirmation.
 return variant?StaffPhotoPlacement{202.f,175.f,1.44f}:StaffPhotoPlacement{205.f,225.f,2.15f};
}
}
