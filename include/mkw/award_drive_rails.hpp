#pragma once
#include <game/field/RailInterpolator.hh>
#include <memory>
#include <array>
namespace mkw {
// These own EGG allocations, which must be prepared before the physical
// child scene consumes/locks its heap. Switching a CAME shot allocates nothing.
class AwardCameraRails {
public:
 void prepare();
 void select(unsigned route);
 void clear(){current_=nullptr;for(auto& p:paths_){p.cursor.reset();p.rail.reset();}}
 void step();
 const Kinoko::EGG::Vector3f* position()const{return current_?&current_->curPos():nullptr;}
private:
 struct Path {std::unique_ptr<Kinoko::Field::Rail> rail;std::unique_ptr<Kinoko::Field::RailInterpolator> cursor;};
 std::array<Path,7> paths_;
 Kinoko::Field::RailInterpolator* current_=nullptr;
};
}
