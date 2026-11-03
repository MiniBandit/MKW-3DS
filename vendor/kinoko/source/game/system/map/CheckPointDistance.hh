#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Kinoko::System {
// USA8050defc / 8050dbf0: mean of (edge length + remaining length)
// over each checkpoint's successors, terminating at the finish checkpoint.
// An explicit stack avoids growing the 3DS call stack along a long circuit.
template<class Graph>
float checkpointCircuitDistance(const Graph& graph, unsigned finish) {
    const unsigned count=graph.size();
    if(!count||finish>=count)return 0.f;
    struct Frame {unsigned point,edge=0;float sum=0.f;};
    std::vector<Frame> stack{{finish}};
    std::vector<uint8_t> state(count,0);
    std::vector<float> value(count,0.f);
    state[finish]=1;
    while(!stack.empty()) {
        auto& frame=stack.back();
        const unsigned branches=graph.nextCount(frame.point);
        if(!branches)return 0.f; // Malformed graph; never substitute an ENPT circuit.
        if(frame.edge==branches) {
            value[frame.point]=frame.sum/float(branches);
            state[frame.point]=2;
            stack.pop_back();
            continue;
        }
        const unsigned next=graph.next(frame.point,frame.edge);
        if(next>=count)return 0.f;
        if(next!=finish&&state[next]!=2) {
            if(state[next]==1)return 0.f; // A cycle that cannot return to the finish.
            state[next]=1;stack.push_back({next});continue;
        }
        // Preserve the retail add order, including branch-local averaging.
        frame.sum+=graph.distance(frame.point,frame.edge);
        if(next!=finish)frame.sum+=value[next];
        ++frame.edge;
    }
    return value[finish];
}
}
