#pragma once
#include <mkw/award_results.hpp>
#include <memory>

namespace mkw {
class Renderer;
class AwardUi {
public:
 AwardUi();~AwardUi();
 // Render-thread ownership; caller drains previous GPU work before unloading.
 bool load();
 void unload();
 void configure(const AwardResults&,bool winner,bool draw);
 void showCongratulations(bool show);
 void enterBoard();
 void leaveBoard();
 bool boardTransitionFinished()const;
 unsigned step(); // original 60 Hz tick; returns a source sound id or zero
 void draw(Renderer&);
private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
