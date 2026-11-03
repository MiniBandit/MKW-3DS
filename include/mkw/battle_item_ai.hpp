#pragma once
#include <cstdint>

namespace mkw {
// Battle uses AIItemBase directly (USA80729210, vtable808c5bac), not
// AIItemRace's random delay/holding/mushroom-route overrides.
class BattleItemAi {
public:
    enum class State : uint8_t {Idle,Stock,Throw,Mushroom,ThrowFront,ThrowBack,Hold,Goal,None};
    struct Command {bool use=false,forwards=false,backwards=false;};
    void reset(){*this=BattleItemAi{};}
    State state()const{return state_;}
    bool active()const{return state_==State::Stock||state_==State::Throw||state_==State::Mushroom||state_==State::Hold;}
    bool releasing()const{return state_==State::ThrowFront||state_==State::ThrowBack;}
    template<class View> Command step(const View& view,bool use,unsigned direction,bool finished=false){
        Command out;
        if(finished)next_=State::Goal;
        if(next_!=State::None){state_=next_;next_=State::None;
            if(state_==State::Idle||releasing())counter_=0;}
        auto release=[&](State neutral){if(!direction){out.use=true;next_=neutral;}
            else next_=direction==1?State::ThrowFront:State::ThrowBack;};
        const unsigned kind=view.item;
        switch(state_){
        case State::Idle:
            if(view.held)next_=State::Stock;
            else if(kind>=16&&kind<=18){
                if(counter_==0){out.use=true;counter_=1;}
                else if(counter_==1)counter_=2;else counter_=0;
            }else if(kind!=20)next_=(kind==4||kind==5||kind==10)?State::Mushroom:State::Throw;
            else if(view.roulette){if(counter_==0){out.use=true;counter_=1;}else counter_=0;}
            break;
        case State::Stock:if(!view.held)next_=State::Idle;else if(use)release(State::Stock);break;
        case State::Throw:if(kind==20)next_=State::Idle;else if(use)release(State::Idle);break;
        case State::Mushroom:
            if(kind==20)next_=State::Idle;
            else if(use){out.use=true;if(kind!=5&&kind!=10)next_=State::Idle;}
            break;
        case State::ThrowFront:case State::ThrowBack:
            (state_==State::ThrowFront?out.forwards:out.backwards)=true;
            if(counter_==0){out.use=true;counter_=1;}else if(counter_==1)counter_=2;
            else next_=view.held?State::Stock:State::Idle;
            break;
        // The base's hold-enable flag starts false and Battle never sets it.
        // Hold is retained in state numbering for native diagnostics.
        default:break;
        }
        return out;
    }
private:
    State state_=State::Idle,next_=State::Idle;unsigned counter_=0;
};
}
