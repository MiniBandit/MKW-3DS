#pragma once
#include "mkw/math.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace mkw {
// ITPT/ITPH controller, independent of the CPU's ENPT pursuit. Retail landmarks
// and the explicit integration limits are recorded in SHELL_GUIDANCE_SOURCE.md.
class ShellRoute {
public:
    static constexpr uint16_t Invalid=0xffff;
    struct Cursor { uint16_t point=Invalid; bool forward=true; };
    struct Point {
        Vec3 position{};float width=0;uint16_t setting1=0,setting2=0;
        std::array<uint16_t,6> next{},prev{};uint8_t nextCount=0,prevCount=0;
    };
    void clear(){points_.clear();}
    bool valid(Cursor c)const{return c.point<points_.size();}
    const Point& point(Cursor c)const{return points_[c.point];}
    size_t size()const{return points_.size();}
    bool load(const void* bytes,size_t size){
        clear();if(!bytes||size<16)return false;const auto* b=static_cast<const uint8_t*>(bytes);
        auto u16=[](const uint8_t* p){return unsigned(p[0])*256+p[1];};
        auto u32=[](const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
        auto f32=[&](const uint8_t* p){uint32_t u=u32(p);float f;std::memcpy(&f,&u,4);return f;};
        if(std::memcmp(b,"RKMD",4)||u32(b+4)>size||u32(b+4)<16)return false;size=u32(b+4);
        unsigned n=u16(b+8),h=u16(b+10);if(n>64||h<16+4*n||h>size)return false;
        size_t pt=0,ph=0,pe=0,he=0;
        for(unsigned i=0;i<n;++i){size_t off=h+size_t(u32(b+16+4*i));if(off>size||size-off<8)return false;
            size_t end=size;for(unsigned j=0;j<n;++j){size_t z=h+size_t(u32(b+16+4*j));if(z>off)end=std::min(end,z);}
            if(!std::memcmp(b+off,"ITPT",4)){pt=off;pe=end;}
            if(!std::memcmp(b+off,"ITPH",4)){ph=off;he=end;}
        }
        if(!pt||!ph)return false;unsigned count=u16(b+pt+4),groups=u16(b+ph+4);
        if(!count||count>255||!groups||groups>255||pt+8+count*20>pe||ph+8+groups*16>he)return false;
        std::vector<Point> points(count);std::vector<bool> covered(count);
        for(unsigned i=0;i<count;++i){const auto* p=b+pt+8+20*i;auto& q=points[i];
            q.position={f32(p),f32(p+4),f32(p+8)};q.width=f32(p+12);q.setting1=u16(p+16);q.setting2=u16(p+18);
            if(!std::isfinite(q.position.x)||!std::isfinite(q.position.y)||!std::isfinite(q.position.z)||!std::isfinite(q.width)||q.width<0)return false;
        }
        for(unsigned g=0;g<groups;++g){const auto* p=b+ph+8+g*16;unsigned start=p[0],len=p[1];
            if(!len||start+len>count)return false;
            for(unsigned j=start;j<start+len;++j){if(covered[j])return false;covered[j]=true;auto& q=points[j];
                for(unsigned side=0;side<2;++side){auto& edges=side?q.next:q.prev;auto& num=side?q.nextCount:q.prevCount;
                    if(side?j+1<start+len:j>start){edges[num++]=side?j+1:j-1;continue;}
                    for(unsigned k=0;k<6;++k){unsigned group=p[(side?8:2)+k];if(group==255)continue;
                        if(group>=groups)return false;const auto* other=b+ph+8+group*16;
                        if(!other[1]||unsigned(other[0])+other[1]>count)return false;
                        unsigned index=side?other[0]:other[0]+other[1]-1;
                        // Preserve duplicate authored links and their order (retail default is link zero).
                        edges[num++]=uint16_t(index);
                    }
                }
            }
        }
        if(std::find(covered.begin(),covered.end(),false)!=covered.end())return false;
        points_=std::move(points);return true;
    }
    Cursor edge(Cursor c,unsigned link=0)const{
        if(!valid(c))return {};const auto& p=point(c);unsigned n=c.forward?p.nextCount:p.prevCount;
        if(link>=n)return {};return {c.forward?p.next[link]:p.prev[link],c.forward};
    }
    unsigned edges(Cursor c)const{return valid(c)?(c.forward?point(c).nextCount:point(c).prevCount):0;}
    // Seeds a kart's cursor after a respawn (retail maps the respawn checkpoint).
    // Keep height so crossing bridges do not select the lower route.
    Cursor nearest(Vec3 pos)const{
        Cursor best{};float score=std::numeric_limits<float>::infinity();
        for(unsigned i=0;i<size();++i){Vec3 d=points_[i].position-pos;float s=dot(d,d);if(s<score){score=s;best={uint16_t(i),true};}}
        return best;
    }
    // The kart's maintained item point (ItemHolderPlayer +0x44, updated every frame by
    // USA 0x8078f144). 0x807aa028 moves it to the closest point within two links
    // (next, next-next, next-prev, prev, prev-prev, prev-next; squared 3D distance,
    // first strictly closer wins). When the kart is still 12000 or more away,
    // 0x807aa4bc walks forward along the closest next link until a point is closer
    // than the current one. A local walk never jumps to another stretch of road the
    // way a whole-course nearest search does at hairpins and crossings.
    uint16_t track(uint16_t current,Vec3 pos)const{
        if(current>=size())return current;
        auto distance=[&](uint16_t p){Vec3 d=points_[p].position-pos;return dot(d,d);};
        uint16_t best=current;float bestDistance=distance(current);
        auto consider=[&](uint16_t p,uint16_t skip){
            if(p>=size()||p==best||p==skip)return;float d=distance(p);if(d<bestDistance){bestDistance=d;best=p;}
        };
        const Point& c=points_[current];
        for(unsigned i=0;i<c.nextCount;++i){uint16_t n=c.next[i];consider(n,Invalid);if(n>=size())continue;const Point& q=points_[n];
            for(unsigned j=0;j<q.nextCount;++j)consider(q.next[j],Invalid);
            for(unsigned j=0;j<q.prevCount;++j)consider(q.prev[j],current);
        }
        for(unsigned i=0;i<c.prevCount;++i){uint16_t p=c.prev[i];consider(p,Invalid);if(p>=size())continue;const Point& q=points_[p];
            for(unsigned j=0;j<q.prevCount;++j)consider(q.prev[j],Invalid);
            for(unsigned j=0;j<q.nextCount;++j)consider(q.next[j],current);
        }
        if(bestDistance<12000.f*12000.f)return best;
        uint16_t probe=best;
        for(size_t left=size();left>1;--left){
            uint16_t step=Invalid;float stepDistance=std::numeric_limits<float>::max();const Point& q=points_[probe];
            for(unsigned j=0;j<q.nextCount;++j){uint16_t n=q.next[j];if(n>=size()||n==step)continue;float d=distance(n);if(d<stepDistance){stepDistance=d;step=n;}}
            if(step==Invalid)break;
            if(stepDistance<bestDistance)return step;
            probe=step;if(probe==best)break;
        }
        return best;
    }
    Cursor select(Cursor current,const Vec3* target,Cursor targetPoint,bool blue)const{
        Cursor first=edge(current);if(!valid(first))return {};
        unsigned outgoing=edges(current);Cursor reverse{first.point,!current.forward};unsigned incoming=edges(reverse);
        if(outgoing<=1&&incoming<=1)return first;
        // 29e80c..29ea28: Blue consults the target's point and follows its
        // default links for floor(.333 * item-point count), including merge arms.
        if(blue&&target&&valid(targetPoint)){
            Cursor probe=targetPoint;
            for(unsigned step=0;step<unsigned(.333f*size());++step){
                for(unsigned i=0;i<outgoing+incoming;++i){Cursor q=i<outgoing?edge(current,i):edge(reverse,i-outgoing);
                    if(q.point!=probe.point)continue;
                    // A match on an incoming arm (another arm merging into the next
                    // point, or our own point) means the target's route runs through
                    // the next point too: keep heading there. Taking the arm backward
                    // sent shells hundreds of ticks against the track (Coconut Mall
                    // 17+ -> 20-: the leader's default route enters point 24 from
                    // group 5 while the shell is on group 4).
                    if(i>=outgoing)return first;
                    return q;
                }
                probe=edge(probe);if(!valid(probe))break;
            }
        }
        float best=std::numeric_limits<float>::infinity();Cursor selected=first;
        // 29ea80: within 15000, choose a forward-facing arm toward the target,
        // excluding points whose setting2 tens digit is nonzero.
        if(target){Vec3 toward=*target-point(current).position;toward.y=0;
            if(dot(toward,toward)<15000.f*15000.f){
                for(unsigned i=0;i<outgoing;++i){Cursor q=edge(current,i);const auto& p=point(q);
                    Vec3 d=p.position-point(current).position;d.y=0;
                    if((int16_t(p.setting2)%100)/10!=0||dot(toward,d)<=0)continue;
                    Vec3 delta=p.position-*target;float s=dot(delta,delta);if(s<best){best=s;selected=q;}
                }
            }
        }
        // fn_1_2A8E30 0x807aac9c: only a cursor travelling against the route (link list
        // +4, prev) turns at a split: it takes the first other arm (link list +10,
        // next) and travels it forward (0x807aad0c; the distance test after it only
        // runs for forward cursors, which never get there). A forward cursor, Blue or
        // not, keeps the arm chosen above.
        if(incoming<=1||current.forward||!target)return selected;
        for(unsigned i=0;i<incoming;++i){Cursor q=edge(reverse,i);if(q.point!=current.point)return q;}
        return selected;
    }
private:std::vector<Point> points_;
};

// 29d308..29db74: four-point cubic with normalized endpoint tangents, handles
// one third of the middle chord, and length estimated at exactly eight samples.
struct ShellSpline {
    std::array<Vec3,4> knots{},control{};float distance=0,inverseLength=1,t=0;
    Vec3 sample(float u)const{
        float v=1-u;return control[0]*(v*v*v)+control[1]*(3*u*v*v)+control[2]*(3*u*u*v)+control[3]*(u*u*u);
    }
    void rebuild(){
        float chord=length(knots[2]-knots[1]);control[0]=knots[1];control[3]=knots[2];
        control[1]=knots[1]+normalized(knots[2]-knots[0],{})*(chord/3);
        control[2]=knots[2]-normalized(knots[3]-knots[1],{})*(chord/3);
        distance=0;Vec3 last=sample(0);for(unsigned i=1;i<=8;++i){Vec3 p=sample(float(i)/8);distance+=length(p-last);last=p;}
        inverseLength=distance>0?1/distance:1;
    }
    void start(Vec3 before,Vec3 position,Vec3 next,Vec3 after){knots={before,position,next,after};rebuild();t=0;}
    void shift(Vec3 after){float leftover=(t-1)*distance;knots={knots[1],knots[2],knots[3],after};rebuild();t=leftover*inverseLength;}
};
struct ShellGuidance {
    // Red states 0..6 and Blue states 0..6 retain their retail table indices.
    uint8_t phase=0;int8_t pending=-1,routeMatchedTarget=-1;uint16_t ticks=0;
    ShellRoute::Cursor current{},next{},targetPoint{};ShellSpline spline{};
    int16_t launchTicks=0;float blend=1;
    Vec3 up{0,1,0},homing{},targetBack{},lastTarget{},orbitOffset{};
    float height=200,amplitude=50,approachSpeed=260,orbitRadius=300,visualY=0;
    float stretch=1; // Blue dive: model Y scale (retail +0x60); the sides are 2*scale-stretch.
    uint32_t orbitAngle=0;uint16_t orbitStep=1000,bobAngle=0,leaderMask=0;
    uint8_t slopeTicks=0;bool splineReady=false,targetLive=true,hasRoute=false,ownerLed=false,dodge=false;
    // QA trace (RaceItems::qaBlueTrace): route ticks, ticks on a backward
    // cursor, ticks moving against the track, longest such run, own point.
    uint16_t qaTicks=0,qaCursorBack=0,qaMovingBack=0,qaRun=0,qaMaxRun=0,qaPoint=0xffff;bool qaLogged=false;
};
inline float shellMoveToward(float x,float target,float step){return x<target?std::min(x+step,target):std::max(x-step,target);}
inline float shellXZ2(Vec3 d){return d.x*d.x+d.z*d.z;}
inline Vec3 shellLimit(Vec3 v,float max){float n=length(v);return n>max&&n>0?v*(max/n):v;}
}
