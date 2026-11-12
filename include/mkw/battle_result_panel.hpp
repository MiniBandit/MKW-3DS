#pragma once
#include <mkw/race_flow_draw.hpp>
namespace mkw {
class BattleResultPanel {
public:
 void reset(){started_=titleStarted_=false;title_={};}
 void step(const RaceResultsFlow&);
 void draw(const RaceResultsFlow&,FlowDrawer&,FlowSink,void*);
private:
 bool started_=false,titleStarted_=false;FlowPlay title_;
};
}
