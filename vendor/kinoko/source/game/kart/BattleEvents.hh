#pragma once
#include <cstddef>

namespace Kinoko::Kart {
// Offline bridge for the race-mode callbacks. Emit at accepted native
// actions / actual contacts, never by reading presentation particle events.
class BattleEvents {
public:
    enum class Kind { Damage, Fall, Steal };
    struct Event {Kind kind;std::size_t victim,attacker;int action=-1;};
    using Hook=bool (*)(void*,const Event&);
    static void SetHook(Hook hook,void* context){s_hook=hook;s_context=context;}
    static bool Emit(Kind kind,std::size_t victim,std::size_t attacker=12,int action=-1){
        return s_hook&&s_hook(s_context,{kind,victim,attacker,action});
    }
private:
    static inline Hook s_hook=nullptr;
    static inline void* s_context=nullptr;
};
} // namespace Kinoko::Kart
