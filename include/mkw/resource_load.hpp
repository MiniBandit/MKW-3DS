#pragma once
namespace mkw {
// A scene becomes playable only after every required resource is ready.
// Recovery must release partial owners, GPU work and old-scene caches first.
template<class Attempt,class Cleanup>
bool loadRequiredResources(Attempt&& attempt,Cleanup&& cleanup,unsigned tries=2){
    for(unsigned i=0;i<tries;++i){
        if(attempt())return true;
        cleanup();
    }
    return false;
}
}
