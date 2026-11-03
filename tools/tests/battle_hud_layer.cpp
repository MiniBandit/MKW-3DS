#include <mkw/coin_hud_camera.hpp>
#include <mkw/race_flow_draw.hpp>
#include <mkw/race_results.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
using namespace mkw;
struct Image {
    std::vector<FlowQuad> quads;
    static void collect(const FlowBatch& b,void* p){auto& v=static_cast<Image*>(p)->quads;v.insert(v.end(),b.quads,b.quads+b.count);}
};
int main(int argc,char** argv){
    assert(argc==2);std::ifstream model(argv[1],std::ios::binary);assert(model);
    uint32_t h[6];model.read(reinterpret_cast<char*>(h),sizeof h);assert(!std::memcmp(h,"GMDL",4)&&(h[1]==4||h[1]==5)&&h[2]==240);
    model.seekg(h[4]*(h[1]==5?92:56)+h[5]*64,std::ios::cur);
    struct Vertex {float position[3],other[7];uint32_t color;};static_assert(sizeof(Vertex)==44);
    std::vector<Vertex> vertices(h[2]);model.read(reinterpret_cast<char*>(vertices.data()),vertices.size()*sizeof(Vertex));assert(model);
    C3D_Mtx projection,view;coinHudCamera(projection,view);assert(projection.r[3].w==1&&view.r[2].w==-1020);
    CoinHudStack stack;unsigned projected=0;float minX=400,maxX=0,minY=240,maxY=0;
    for(unsigned held=1;held<=100;++held){
        stack.update(held);
        for(unsigned i=0;i<stack.size();++i){const auto transform=coinHudColumnTransform(stack.column(i));C3D_Mtx mv,clip;
            Mtx_Multiply(&mv,&view,&transform);Mtx_Multiply(&clip,&projection,&mv);
            // Project the actual cooked mesh using the production PICA math.
            // Wrong perspective mode/eye/scale/order would clip this geometry.
            for(const auto& vertex:vertices){const auto& p=vertex.position;
                auto q=Mtx_MultiplyFVec4(&clip,FVec4_New(p[0],p[1],p[2],1));
                assert(q.w==1&&std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z));
                assert(q.x>=-1&&q.x<=1&&q.y>=-1&&q.y<=1&&q.z>=-1&&q.z<=0);
                const float x=(1-q.y)*200,y=(1-q.x)*120;
                minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);++projected;
            }
        }
    }
    assert(minX>0&&maxX<150&&minY>120&&maxY<240);
    RaceResultsFlow flow;FlowDrawer continued;flow.finish();bool switched=false,compared=false;
    for(unsigned tick=0;tick<1500;++tick){
        auto e=flow.step({flow.phase()==RaceFlowPhase::Update&&flow.pageFrame()>100,false});
        if(e&RaceResultsFlow::EventFill){RaceResultEntry rows[]={{0,0,true,7,5,RaceTeam::Red},{1,1,false,3,2,RaceTeam::Blue}};
            flow.fill(rows,2,false);flow.setBattle(1,0,3);}
        continued.step(flow);
        if(flow.phase()==RaceFlowPhase::TotalIn&&flow.pageFrame()==0){
            // Fresh native page controls and the continuing drawer must
            // produce identical Loop poses at the page boundary.
            FlowDrawer fresh;fresh.step(flow);Image a,b;
            continued.draw(flow,Image::collect,&a);fresh.draw(flow,Image::collect,&b);
            assert(!a.quads.empty()&&a.quads.size()==b.quads.size());
            for(unsigned n=0;n<a.quads.size();++n)for(unsigned k=0;k<4;++k){
                assert(a.quads[n].x[k]==b.quads[n].x[k]&&a.quads[n].y[k]==b.quads[n].y[k]&&a.quads[n].color[k]==b.quads[n].color[k]);}
            switched=true;compared=true;break;
        }
    }
    assert(switched&&compared);
    std::printf("PASS production held-coin camera/column transforms with %u cooked vertices projected in clip/LCD bounds x%.2f..%.2f y%.2f..%.2f; separate native Battle page Loop clock; GPU render/stereo acceptance pending\n",projected,minX,maxX,minY,maxY);
}
