#pragma once
#include <mkw/battle_search.hpp>

namespace mkw {
// AIControlBattle's item-box/coin targeters, USA807263e0..807269fc.
// Targets are selected from the current graph edge, not all world objects.
class BattleCloseTarget {
public:
    enum class State {Idle,Select,Chase};
    void reset(){*this=BattleCloseTarget{};}
    State state()const{return state_;}
    Vec3 position()const{return target_;}
    float travelRemaining()const{return travel_;}
    float timeRemaining()const{return time_;}
    int object()const{return cursor_>=0?int(candidates_[cursor_]):-1;}
    template<class Valid> bool step(const BattleSearchGraph& graph,const BattleSearchField& field,
            unsigned previous,unsigned current,bool switched,Vec3 position,bool inAction,bool wall,
            bool enabled,bool continueNext,Valid valid){
        if(!enabled)return false;
        if(switched&&previous!=current){
            count_=0;cursor_=-1;
            const unsigned edge=graph.edge(previous,current);
            if(field.ready()&&edge<graph.edges.size()){
                const auto& sources=field.sources(edge);
                count_=std::min(unsigned(sources.size()),unsigned(candidates_.size()));
                std::array<float,5> distances{};
                for(unsigned i=0;i<count_;++i){candidates_[i]=sources[i];
                    distances[i]=length(field.object(sources[i])->position-position);}
                // Native qsort compares distance only. Retain authored order
                // for equal distances; no additional id/random tie-break.
                for(unsigned i=1;i<count_;++i){const auto id=candidates_[i];const float distance=distances[i];unsigned j=i;
                    while(j&&distance<distances[j-1]){candidates_[j]=candidates_[j-1];distances[j]=distances[j-1];--j;}
                    candidates_[j]=id;distances[j]=distance;}
            }
            state_=State::Select;
        }
        if(state_==State::Select){
            bool found=false;
            for(int i=cursor_+1;i<int(count_);++i){const unsigned id=candidates_[i];
                const auto* source=field.object(id);
                if(!valid(id)||!source||!source->active)continue;
                cursor_=i;target_=source->position;const float distance=length(target_-position);
                travel_=1.5f*distance;time_=60.f+.15f*distance;last_=position;found=true;break;
            }
            if(!found){state_=State::Idle;return false;}
            state_=State::Chase;
        }
        if(state_!=State::Chase)return false;
        if(!inAction)time_-=1.f;
        if(time_<0){state_=State::Idle;return false;}
        const float moved=length(position-last_);
        if(wall&&moved<5.f)++wallTicks_;else wallTicks_=0;
        if(wallTicks_>30){state_=State::Select;return false;}
        bool done=cursor_<0||!valid(candidates_[cursor_]);
        if(!done){travel_-=moved;last_=position;done=travel_<0;}
        if(done){state_=continueNext?State::Select:State::Idle;return false;}
        return true;
    }
private:
    State state_=State::Idle;
    Vec3 target_{},last_{};float travel_=0,time_=0;
    unsigned count_=0,wallTicks_=0;int cursor_=-1;
    std::array<uint16_t,5> candidates_{};
};
// Battle's DriveInfo yaw (USA80724484). Body axes are transformed by the
// pose matrix, as in the original; this is not ENPT curvature feed-forward.
inline float battleSteering(Vec3 delta,Vec3 right,Vec3 front){
    if(dot(delta,delta)<=std::numeric_limits<float>::epsilon())delta={0,0,1};
    else delta=normalized(delta);
    const float angle=std::atan2(length(cross(front,delta)),dot(front,delta));
    return .5f*angle*(dot(delta,right)>0?-1.f:1.f);
}
}
