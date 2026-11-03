#pragma once
#include <mkw/math.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace mkw {
inline float battleSearchRadius(unsigned course){
    // AI Battle director table USA808c6928, native course ids32..41.
    constexpr float Radius[]={24000,15000,15000,30000,12000,10000,12000,20000,20000,20000};
    return course>=32&&course<42?Radius[course-32]:0.f;
}

// Shared original Battle graph, USA808767b8..80876e88. Directed ENPH
// successors retain their order; opposite directions refer to one edge.
class BattleSearchGraph {
public:
    static constexpr uint16_t Invalid=0xffff;
    struct Link {uint16_t point=Invalid,edge=Invalid;};
    struct Node {Vec3 position;float width=0;std::vector<Link> links;};
    struct Edge {uint16_t a=0,b=0;Vec3 direction;float length=0;bool degenerate=false;std::vector<uint16_t> adjacent;};
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    void clear(){nodes=std::vector<Node>{};edges=std::vector<Edge>{};}
    template<class Route> bool build(const Route& route){
        clear();if(!route.size()||route.size()>255)return false;
        nodes.resize(route.size());
        for(unsigned i=0;i<route.size();++i){const auto& p=route.point(i);auto& n=nodes[i];
            n.position=p.position;n.width=p.width*50.f;n.links.resize(p.nextCount);
            for(unsigned k=0;k<p.nextCount;++k){
                const unsigned next=p.next[k];if(next>=route.size()){clear();return false;}
                uint16_t edge=Invalid;
                // The retail constructor searches earlier nodes, preserving
                // parallel links authored on the same node.
                for(unsigned j=0;j<i&&edge==Invalid;++j)for(const auto& old:nodes[j].links){
                    if((j==i&&old.point==next)||(j==next&&old.point==i)){edge=old.edge;break;}
                }
                if(edge==Invalid){
                    if(edges.size()>=Invalid){clear();return false;}
                    edge=uint16_t(edges.size());Edge e;e.a=uint16_t(i);e.b=uint16_t(next);edges.push_back(std::move(e));
                }
                n.links[k]={uint16_t(next),edge};
            }
        }
        for(unsigned i=0;i<edges.size();++i){auto& e=edges[i];const auto delta=nodes[e.b].position-nodes[e.a].position;
            e.degenerate=dot(delta,delta)<=std::numeric_limits<float>::epsilon();e.length=e.degenerate?0.f:length(delta);
            e.direction=e.degenerate?Vec3{0,1,0}:delta/e.length;
            for(unsigned point:{unsigned(e.a),unsigned(e.b)})for(const auto& l:nodes[point].links)
                if(l.edge!=i)e.adjacent.push_back(l.edge);
        }
        return !edges.empty();
    }
    unsigned edge(unsigned a,unsigned b)const{
        for(unsigned i=0;i<edges.size();++i)if((edges[i].a==a&&edges[i].b==b)||(edges[i].a==b&&edges[i].b==a))return i;
        return Invalid;
    }
};

// USA808888d4/808894e4/80889fb8/8088a5e8. Integer influence is accumulated
// per graph edge, from a path-distance falloff, never straight through walls.
class BattleSearchField {
public:
    struct Object {bool active=false;Vec3 position;int importance=0;uint16_t seed=BattleSearchGraph::Invalid;};
    void clear(){*this=BattleSearchField{};}
    bool reset(const BattleSearchGraph& graph,float radius,unsigned objects,unsigned edgeCapacity){
        clear();if(graph.edges.empty()||radius<=0||objects>65535)return false;
        graph_=&graph;radius_=radius;inverse_=1.f/radius;capacity_=edgeCapacity;
        objects_.resize(objects);weights_.resize(graph.edges.size());remaining_.resize(graph.edges.size());hops_.resize(graph.edges.size());
        near_.resize(graph.edges.size());sources_.resize(graph.edges.size());for(auto& list:sources_)list.reserve(edgeCapacity);
        return true;
    }
    bool ready()const{return graph_!=nullptr;}
    unsigned size()const{return unsigned(objects_.size());}
    const Object* object(unsigned index)const{return index<objects_.size()?&objects_[index]:nullptr;}
    int weight(unsigned edge)const{return edge<weights_.size()?weights_[edge]:0;}
    const std::vector<uint16_t>& sources(unsigned edge)const{return sources_[edge];}
    void remove(unsigned index){
        if(!graph_||index>=objects_.size()||!objects_[index].active)return;
        auto& object=objects_[index];addInfluence(object.seed,-object.importance);
        for(auto& list:sources_){const auto p=std::find(list.begin(),list.end(),index);if(p!=list.end())list.erase(p);}
        object.active=false;
    }
    bool update(unsigned index,Vec3 position,int importance){
        if(!graph_||index>=objects_.size()||!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))return false;
        auto& object=objects_[index];
        // Native reuses the cached source if neither its value nor position
        // changed by more than10 units (including its inactive cached entry).
        if(object.importance==importance&&length(object.position-position)<=10.f)return true;
        remove(index);std::fill(near_.begin(),near_.end(),0);
        unsigned seed=BattleSearchGraph::Invalid;float best=0;
        for(unsigned i=0;i<graph_->edges.size();++i){const auto& e=graph_->edges[i];if(e.degenerate)continue;
            const auto& a=graph_->nodes[e.a];const auto& b=graph_->nodes[e.b];const Vec3 delta=position-a.position;
            const float along=dot(delta,e.direction);
            if(along<0||dot(position-b.position,-e.direction)<0)continue;
            const float t=std::clamp(along/e.length,0.f,1.f),width=a.width+t*(b.width-a.width);
            const float distance=length(delta-e.direction*along);if(distance>width)continue;
            near_[i]=1;if(seed==BattleSearchGraph::Invalid||distance<best){seed=i;best=distance;}
        }
        for(const auto& n:graph_->nodes){const float distance=length(position-n.position);if(distance>n.width)continue;
            if(n.links.size()>1&&(seed==BattleSearchGraph::Invalid||distance<best)){seed=n.links[0].edge;best=distance;}
            for(const auto& l:n.links)near_[l.edge]=1;
        }
        if(seed==BattleSearchGraph::Invalid)return false;
        for(unsigned e=0;e<sources_.size();++e)if(near_[e]&&sources_[e].size()<capacity_)sources_[e].push_back(uint16_t(index));
        object={true,position,importance,uint16_t(seed)};addInfluence(seed,importance);return true;
    }
    unsigned choose(unsigned from,unsigned previous,bool highest,bool excludePrevious=true)const{
        if(!graph_||from>=graph_->nodes.size())return 0;
        int best=highest?-1:std::numeric_limits<int>::max();unsigned result=0;
        const auto& links=graph_->nodes[from].links;
        // AIPathPointBattle807477a4/80747858: strict comparison, ties retain
        // the first authored successor. No eligible result falls back to0.
        for(unsigned k=0;k<links.size();++k){if(excludePrevious&&links[k].point==previous)continue;
            const int value=weight(graph_->edge(from,links[k].point));
            if(highest?value>best:value<best){best=value;result=k;}}
        return result;
    }
private:
    void addInfluence(unsigned seed,int importance){
        if(seed>=remaining_.size())return;
        std::fill(remaining_.begin(),remaining_.end(),0.f);std::fill(hops_.begin(),hops_.end(),uint16_t(0xffff));
        remaining_[seed]=radius_;hops_[seed]=0;
        // Native relaxation visits ascending edge ids at each hop; its
        // strictly improving positive distances terminate even on cycles.
        for(unsigned hop=0;hop<65535;++hop){bool changed=false;
            for(unsigned e=0;e<remaining_.size();++e)if(hops_[e]==hop)for(unsigned next:graph_->edges[e].adjacent){
                const float value=remaining_[e]-.5f*(graph_->edges[e].length+graph_->edges[next].length);
                if(remaining_[next]<value){remaining_[next]=value;hops_[next]=uint16_t(hop+1);changed=true;}}
            if(!changed)break;
        }
        for(unsigned e=0;e<weights_.size();++e){const float rate=inverse_*remaining_[e];
            weights_[e]+=int(float(importance)*(rate*rate));}
    }
    const BattleSearchGraph* graph_=nullptr;
    float radius_=0,inverse_=0;unsigned capacity_=0;
    std::vector<Object> objects_;
    std::vector<int> weights_;
    std::vector<float> remaining_;
    std::vector<uint16_t> hops_;
    std::vector<uint8_t> near_;
    std::vector<std::vector<uint16_t>> sources_;
};
}
