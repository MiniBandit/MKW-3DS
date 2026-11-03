#include "mkw/object_bank.hpp"
#include <mkw/asset_pack.hpp>
#include <mkw/render_metrics.hpp>
#include <mkw/normal_matrix.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace mkw {
ClipSample sampleClip(unsigned frames,unsigned step,bool loop,float frame){
    if(frames<=1||!std::isfinite(frame))return {0,0,0};
    step=std::max(1u,step);
    const unsigned last=(frames-1+step-1)/step; // index of the final sample (frame frames-1)
    if(loop){frame=std::fmod(frame,float(frames));if(frame<0)frame+=float(frames);}
    else frame=std::clamp(frame,0.f,float(frames-1));
    const float lastFrame=float(frames-1);
    if(frame>=lastFrame){
        // The final interval of a loop blends the last sample back to frame 0.
        return loop?ClipSample{last,0,frame-lastFrame}:ClipSample{last,last,0};
    }
    const unsigned a=unsigned(frame)/step;
    const float fa=float(a*step),fb=std::min(float((a+1)*step),lastFrame);
    return {a,std::min(a+1,last),fb>fa?(frame-fa)/(fb-fa):0.f};
}
namespace {
uint32_t u32(const uint8_t* p){uint32_t v;std::memcpy(&v,p,4);return v;}
unsigned samples(unsigned frames,unsigned step){return frames<=1?1:(frames-1+step-1)/step+1;}
}
void ObjectBank::reset(){
    std::free(data_);data_=nullptr;vertices_=matrices_=shapes_=0;weighted_=false;
    source_=nullptr;ranges_=nullptr;bind_=nullptr;shared_={};sharedVertices_.reset();nodes_.clear();chr_.clear();pat_.clear();srt_.clear();clr_.clear();morph_.clear();rigidParts_.clear();registerAlpha_.clear();
    callbackSteps_.clear();callbackRecipes_.clear();callbackClips_.clear();callbackBind_=nullptr;
}
bool ObjectBank::load(const char* path){
    reset();
    size_t size=0;bool ok=false;
    if(const long packed=pack::size(path);packed>=0){
        size=size_t(packed);if(size<40||size>8*1024*1024)return false;
        data_=static_cast<uint8_t*>(std::malloc(size));if(!data_)return false;
        ok=pack::readInto(path,data_,size);
    }else{
        FILE* f=std::fopen(path,"rb");if(!f)return false;
        std::setvbuf(f,nullptr,_IONBF,0);
        std::fseek(f,0,SEEK_END);const long length=std::ftell(f);std::rewind(f);
        if(length<40||length>8*1024*1024){std::fclose(f);return false;}
        size=size_t(length);data_=static_cast<uint8_t*>(std::malloc(size));
        ok=data_&&std::fread(data_,1,size,f)==size;std::fclose(f);
    }
    if(!ok||std::memcmp(data_,"DOBJ",4)||u32(data_+4)!=1){reset();return false;}
    vertices_=u32(data_+8);matrices_=u32(data_+12);shapes_=u32(data_+16);
    const unsigned nodeCount=u32(data_+20),chrCount=u32(data_+24),patCount=u32(data_+28),srtCount=u32(data_+32),clrCount=u32(data_+36);
    if(!matrices_||matrices_>128||vertices_>65535||shapes_>256||nodeCount>256||chrCount>64||patCount>64||srtCount>64||clrCount>64){reset();return false;}
    size_t at=40;
    auto take=[&](size_t bytes)->const uint8_t*{if(bytes>size-std::min(at,size)||at>size)return nullptr;const uint8_t* p=data_+at;at+=bytes;return p;};
    source_=reinterpret_cast<const SourceVertex*>(take(size_t(vertices_)*sizeof(SourceVertex)));
    ranges_=reinterpret_cast<const Range*>(take(size_t(shapes_)*sizeof(Range)));
    bind_=reinterpret_cast<const float*>(take(size_t(matrices_)*48));
    if(!source_||!ranges_||!bind_){reset();return false;}
    for(unsigned i=0;i<vertices_;++i)if(source_[i].matrix>=matrices_){reset();return false;}
    for(unsigned i=0;i<shapes_;++i)if(vertices_&&(ranges_[i].first>vertices_||ranges_[i].count>vertices_-ranges_[i].first)){reset();return false;}
    nodes_.resize(nodeCount);
    for(auto& n:nodes_){
        const uint8_t* p=take(sizeof(Node));if(!p){reset();return false;}
        std::memcpy(&n,p,sizeof(Node));n.name[31]=0;
        if(n.parent>=int32_t(nodeCount)||n.slot>=int32_t(matrices_)){reset();return false;}
    }
    for(unsigned i=0;i<chrCount;++i){
        const uint8_t* p=take(48);if(!p){reset();return false;}
        Chr c{};std::memcpy(c.name,p,32);c.name[31]=0;c.frames=u32(p+32);c.loop=u32(p+36);c.step=u32(p+40);c.animatedCount=u32(p+44);
        if(!c.frames||c.frames>20000||!c.step||c.step>64||c.animatedCount>matrices_){reset();return false;}
        c.animated=reinterpret_cast<const uint32_t*>(take(size_t(c.animatedCount)*4));
        c.base=reinterpret_cast<const float*>(take(size_t(matrices_)*48));
        c.samples=reinterpret_cast<const float*>(take(size_t(samples(c.frames,c.step))*c.animatedCount*48));
        if(!c.animated||!c.base||!c.samples){reset();return false;}
        for(unsigned j=0;j<c.animatedCount;++j)if(c.animated[j]>=matrices_){reset();return false;}
        chr_.push_back(c);
    }
    for(unsigned i=0;i<patCount;++i){
        const uint8_t* p=take(40);if(!p){reset();return false;}
        Pat c{};std::memcpy(c.name,p,32);c.name[31]=0;c.frames=u32(p+32);c.loop=u32(p+36);
        if(!c.frames||c.frames>20000){reset();return false;}
        c.data=reinterpret_cast<const int32_t*>(take(size_t(c.frames)*shapes_*8));if(!c.data){reset();return false;}
        pat_.push_back(c);
    }
    for(unsigned i=0;i<srtCount;++i){
        const uint8_t* p=take(48);if(!p){reset();return false;}
        Srt c{};std::memcpy(c.name,p,32);c.name[31]=0;c.frames=u32(p+32);c.loop=u32(p+36);c.step=u32(p+40);c.count=u32(p+44);
        // Native BRRES SRT0 duration is u16; Delfino uses24000 frames.
        // The existing8MiB bank bound and payload checks still limit memory.
        if(!c.frames||c.frames>65535||!c.step||c.step>64||c.count>shapes_){reset();return false;}
        c.shapes=reinterpret_cast<const uint32_t*>(take(size_t(c.count)*4));
        c.data=reinterpret_cast<const float*>(take(size_t(samples(c.frames,c.step))*c.count*48));
        if(!c.shapes||!c.data){reset();return false;}
        for(unsigned j=0;j<c.count;++j)if(c.shapes[j]>=shapes_){reset();return false;}
        srt_.push_back(c);
    }
    for(unsigned i=0;i<clrCount;++i){
        const uint8_t* p=take(44);if(!p){reset();return false;}
        Clr c{};std::memcpy(c.name,p,32);c.name[31]=0;c.frames=u32(p+32);c.loop=u32(p+36);c.count=u32(p+40);
        if(!c.frames||c.frames>20000||c.count>shapes_){reset();return false;}
        c.shapes=reinterpret_cast<const uint32_t*>(take(size_t(c.count)*4));
        c.flags=reinterpret_cast<const uint32_t*>(take(size_t(c.count)*4));
        c.data=reinterpret_cast<const uint32_t*>(take(size_t(c.frames)*c.count*12));
        if(!c.shapes||!c.flags||!c.data){reset();return false;}
        for(unsigned j=0;j<c.count;++j)if(c.shapes[j]>=shapes_){reset();return false;}
        clr_.push_back(c);
    }
    while(at<size){
        const auto* header=take(8);
        if(!header){reset();return false;}
        if(!std::memcmp(header,"JCB1",4)){
            const unsigned count=u32(header+4);const auto* dimensions=take(8);
            if(!callbackSteps_.empty()||!count||count>256||!dimensions||u32(dimensions)!=matrices_||u32(dimensions+4)!=chr_.size()){reset();return false;}
            callbackSteps_.resize(count);
            for(unsigned i=0;i<count;++i){
                const auto* p=take(sizeof(CallbackStep));if(!p){reset();return false;}
                std::memcpy(&callbackSteps_[i],p,sizeof(CallbackStep));auto& s=callbackSteps_[i];s.name[31]=0;
                if(s.parent < -1||s.parent>=static_cast<int32_t>(i)){reset();return false;}
            }
            auto floats=[&](size_t n)->const float*{
                const auto* p=reinterpret_cast<const float*>(take(n*4));if(!p)return nullptr;
                for(size_t i=0;i<n;++i)if(!std::isfinite(p[i]))return nullptr;
                return p;
            };
            callbackBind_=floats(size_t(count)*18);if(!callbackBind_){reset();return false;}
            for(unsigned i=0;i<matrices_;++i){
                const auto* n=take(4);if(!n||!u32(n)||u32(n)>64){reset();return false;}
                CallbackRecipe recipe{u32(n),reinterpret_cast<const CallbackTerm*>(take(size_t(u32(n))*sizeof(CallbackTerm)))};
                if(!recipe.terms){reset();return false;}
                for(unsigned j=0;j<recipe.count;++j){const auto& t=recipe.terms[j];
                    if(t.step>=count||!std::isfinite(t.weight)){reset();return false;}
                    for(float v:t.inverse)if(!std::isfinite(v)){reset();return false;}
                }
                callbackRecipes_.push_back(recipe);
            }
            for(const auto& chr:chr_){
                const auto* p=take(12);if(!p||u32(p)!=chr.frames||u32(p+4)!=chr.loop||!u32(p+8)||u32(p+8)>64){reset();return false;}
                CallbackClip c{u32(p),u32(p+4),u32(p+8),nullptr};
                c.data=floats(size_t(samples(c.frames,c.step))*count*18);if(!c.data){reset();return false;}
                callbackClips_.push_back(c);
            }
            continue;
        }
        if(std::memcmp(header,"MORF",4)||!morph_.empty()||u32(header+4)>chr_.size()){reset();return false;}
        for(unsigned i=0;i<u32(header+4);++i){
            const auto* p=take(12);if(!p){reset();return false;}
            Morph m{};m.clip=u32(p);m.count=u32(p+4);m.frames=u32(p+8);
            if(m.clip>=chr_.size()||m.count>vertices_||m.frames!=chr_[m.clip].frames){reset();return false;}
            m.vertices=reinterpret_cast<const uint32_t*>(take(size_t(m.count)*4));
            m.deltas=reinterpret_cast<const float*>(take(size_t(m.count)*24));
            m.weights=reinterpret_cast<const float*>(take(size_t(m.frames)*4));
            if(!m.vertices||!m.deltas||!m.weights){reset();return false;}
            for(unsigned v=0;v<m.count;++v)if(m.vertices[v]>=vertices_||(v&&m.vertices[v]<=m.vertices[v-1])){reset();return false;}
            for(unsigned v=0;v<m.count*6;++v)if(!std::isfinite(m.deltas[v])){reset();return false;}
            for(unsigned f=0;f<m.frames;++f)if(!std::isfinite(m.weights[f])){reset();return false;}
            morph_.push_back(m);
        }
    }
    if(at!=size){reset();return false;}
    return true;
}
namespace {
template<class T> int findName(const std::vector<T>& list,const char* name){
    if(name)for(size_t i=0;i<list.size();++i)if(!std::strcmp(list[i].name,name))return int(i);
    return -1;
}
}
int ObjectBank::clip(const char* name) const {return findName(chr_,name);}
int ObjectBank::pattern(const char* name) const {return findName(pat_,name);}
int ObjectBank::scroll(const char* name) const {return findName(srt_,name);}
int ObjectBank::color(const char* name) const {return findName(clr_,name);}
int ObjectBank::node(const char* name) const {return findName(nodes_,name);}
void ObjectBank::pose(int clip,float frame,float* out) const {
    if(clip<0||unsigned(clip)>=chr_.size()){std::memcpy(out,bind_,size_t(matrices_)*48);return;}
    const auto& c=chr_[clip];
    std::memcpy(out,c.base,size_t(matrices_)*48);
    const auto s=sampleClip(c.frames,c.step,c.loop,frame);
    const float* a=c.samples+size_t(s.a)*c.animatedCount*12;
    const float* b=c.samples+size_t(s.b)*c.animatedCount*12;
    for(unsigned i=0;i<c.animatedCount;++i){
        float* m=out+c.animated[i]*12;const float* x=a+i*12;const float* y=b+i*12;
        for(unsigned k=0;k<12;++k)m[k]=x[k]+(y[k]-x[k])*s.t;
    }
}
namespace {
void callbackMultiply(const float* a,const float* b,float* out){
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c){
        float v=a[r*4]*b[c]+a[r*4+1]*b[4+c]+a[r*4+2]*b[8+c];
        out[r*4+c]=c==3?v+a[r*4+3]:v;
    }
}
}
bool ObjectBank::poseCallbacks(int clip,float frame,const JointCallback* callbacks,unsigned count,
        float* out,std::vector<float>& scratch) const {
    if(!hasJointCallbacks()||!callbacks||!count||!out)return false;
    constexpr float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};bool unchanged=true;
    for(unsigned i=0;i<count;++i){
        if(!callbacks[i].node||!callbacks[i].matrix)return false;
        bool found=false;for(const auto& s:callbackSteps_)if(!std::strcmp(s.name,callbacks[i].node)){found=true;break;}
        if(!found)return false;
        for(unsigned c=0;c<12;++c){if(!std::isfinite(callbacks[i].matrix[c]))return false;unchanged&=callbacks[i].matrix[c]==identity[c];}
    }
    if(unchanged){pose(clip,frame,out);return true;}
    const size_t nodes=callbackSteps_.size(),stride=nodes*18;
    const float* a=callbackBind_;const float* b=a;float t=0;
    if(clip>=0&&unsigned(clip)<callbackClips_.size()){
        const auto& c=callbackClips_[clip];const auto sample=sampleClip(c.frames,c.step,c.loop,frame);
        a=c.data+size_t(sample.a)*stride;b=c.data+size_t(sample.b)*stride;t=sample.t;
    }
    scratch.resize(nodes*15);float* world=scratch.data();float* scale=world+nodes*12;
    for(size_t i=0;i<nodes;++i){
        float local[12],parent[12];const auto* x=a+i*18;const auto* y=b+i*18;
        auto value=[&](unsigned k){return x[k]+(y[k]-x[k])*t;};
        for(unsigned k=0;k<12;++k)local[k]=value(k);
        const int p=callbackSteps_[i].parent;const auto* base=p<0?identity:world+size_t(p)*12;
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)parent[r*4+c]=base[r*4+c]*(c<3?value(12+c):1.f);
        auto* m=world+i*12;callbackMultiply(parent,local,m);
        for(unsigned k=0;k<count;++k)if(!std::strcmp(callbackSteps_[i].name,callbacks[k].node)){
            float turned[12];callbackMultiply(m,callbacks[k].matrix,turned);std::copy_n(turned,12,m);
        }
        for(unsigned k=0;k<3;++k)scale[i*3+k]=value(15+k);
    }
    // CalcWorld applies each node's own scale only after all descendants and
    // callbacks. Matrix recipes then reproduce NodeMix's inverse-bind blends.
    for(size_t i=0;i<nodes;++i)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)world[i*12+r*4+c]*=scale[i*3+c];
    for(unsigned i=0;i<matrices_;++i){
        auto* m=out+i*12;std::fill_n(m,12,0.f);
        for(unsigned j=0;j<callbackRecipes_[i].count;++j){
            const auto& term=callbackRecipes_[i].terms[j];float part[12];callbackMultiply(world+size_t(term.step)*12,term.inverse,part);
            for(unsigned k=0;k<12;++k)m[k]+=part[k]*term.weight;
        }
    }
    return true;
}
bool ObjectBank::material(int pat,float patFrame,int srt,float srtFrame,int clr,float clrFrame,SubmeshOverride* out,unsigned count) const {
    // Submesh i is shape i: the cooker preserves vertices and starts one
    // submesh per shape.
    if(count!=shapes_)return false;
    bool any=false;
    for(unsigned i=0;i<count;++i){auto& o=out[i];o={};o.uv[0]=o.uv[4]=o.uv[6]=o.uv[10]=1;o.texture[0]=o.texture[1]=-1;o.tint[0]=o.tint[1]=o.tint[2]=o.tint[3]=1;}
    if(pat>=0&&unsigned(pat)<pat_.size()){
        const auto& c=pat_[pat];
        unsigned f=c.loop?unsigned(std::fmod(std::max(patFrame,0.f),float(c.frames))):unsigned(std::clamp(patFrame,0.f,float(c.frames-1)));
        if(f>=c.frames)f=c.frames-1;
        const int32_t* row=c.data+size_t(f)*shapes_*2;
        for(unsigned i=0;i<count;++i)if(row[i*2]>=0||row[i*2+1]>=0){out[i].texture[0]=row[i*2];out[i].texture[1]=row[i*2+1];out[i].flags|=SubmeshOverride::Texture;any=true;}
    }
    if(srt>=0&&unsigned(srt)<srt_.size()){
        const auto& c=srt_[srt];const auto s=sampleClip(c.frames,c.step,c.loop,srtFrame);
        const float* a=c.data+size_t(s.a)*c.count*12;const float* b=c.data+size_t(s.b)*c.count*12;
        for(unsigned j=0;j<c.count;++j){auto& o=out[c.shapes[j]];for(unsigned k=0;k<12;++k)o.uv[k]=a[j*12+k]+(b[j*12+k]-a[j*12+k])*s.t;o.flags|=SubmeshOverride::Uv;any=true;}
    }
    if(clr>=0&&unsigned(clr)<clr_.size()){
        const auto& c=clr_[clr];
        unsigned f=c.loop?unsigned(std::fmod(std::max(clrFrame,0.f),float(c.frames))):unsigned(std::clamp(clrFrame,0.f,float(c.frames-1)));
        if(f>=c.frames)f=c.frames-1;
        const uint32_t* row=c.data+size_t(f)*c.count*3;
        for(unsigned j=0;j<c.count;++j){
            auto& o=out[c.shapes[j]];const uint32_t flags=c.flags[j];
            if(flags&1){o.constant=row[j*3];o.flags|=SubmeshOverride::Constant;}
            if(flags&(2|4)){
                const uint32_t t=row[j*3+1];
                // Tint ratios were stored halved (255 is 2.0).
                for(unsigned k=0;k<4;++k)o.tint[k]=float((t>>(8*k))&255)*(2.f/255.f);
                if(!(flags&2))o.tint[0]=o.tint[1]=o.tint[2]=1;
                if(!(flags&4)&&!(flags&2))o.tint[3]=1;
                o.flags|=SubmeshOverride::Tint;
            }
            if((flags&4)&&c.shapes[j]<registerAlpha_.size()&&registerAlpha_[c.shapes[j]])o.flags|=SubmeshOverride::Alpha;
            if(flags&8){o.add=row[j*3+2];o.flags|=SubmeshOverride::Add;}
            any=true;
        }
    }
    return any;
}
void ObjectBank::markRegisterAlpha(const Model& model){
    registerAlpha_.assign(shapes_,0);
    if(model.submeshes.size()!=shapes_||!model.vertices||!model.indices)return;
    for(unsigned i=0;i<shapes_;++i){
        const auto& s=model.submeshes[i];
        // Source GX state (bit 31) with alpha mode 4: alpha is the vertex alpha alone.
        if(!(s.flags&(1u<<31))||((s.flags>>20)&7)!=4||!s.count||s.first+s.count>model.indexCount)continue;
        bool zero=true;
        for(unsigned k=s.first;k<s.first+s.count&&zero;++k){const unsigned v=model.indices[k];zero=v<model.vertexCount&&model.vertices[v].color[3]==0;}
        registerAlpha_[i]=zero;
    }
}
bool ObjectBank::makeRigid(Model& model){
    if(morphing())return false;
    if(!ready()||rigid()||!vertices_||vertices_!=model.vertexCount)return false;
    for(const auto& s:model.submeshes)for(unsigned i=s.first;i+2<s.first+s.count;i+=3){
        const auto m=source_[model.indices[i]].matrix;
        if(source_[model.indices[i+1]].matrix!=m||source_[model.indices[i+2]].matrix!=m){weighted_=true;return false;}
    }
    std::vector<std::vector<RigidPart>> parts(model.submeshes.size());
    std::vector<std::array<uint16_t,3>> triangles;
    for(size_t n=0;n<model.submeshes.size();++n){
        const auto& s=model.submeshes[n];
        triangles.clear();
        for(unsigned i=s.first;i+2<s.first+s.count;i+=3)triangles.push_back({model.indices[i],model.indices[i+1],model.indices[i+2]});
        std::stable_sort(triangles.begin(),triangles.end(),[&](const auto& a,const auto& b){return source_[a[0]].matrix<source_[b[0]].matrix;});
        for(size_t k=0;k<triangles.size();++k){
            const unsigned at=s.first+unsigned(k)*3;std::memcpy(model.indices+at,triangles[k].data(),6);
            const unsigned matrix=source_[triangles[k][0]].matrix;
            if(parts[n].empty()||parts[n].back().matrix!=matrix)parts[n].push_back({at,0,matrix});
            parts[n].back().count+=3;
        }
    }
    for(unsigned i=0;i<vertices_;++i){
        const auto& v=source_[i];
        model.vertices[i].position={v.p[0],v.p[1],v.p[2]};model.vertices[i].normal={v.n[0],v.n[1],v.n[2]};
    }
    // Each part's bone-local bounds, for culling parts where they are drawn.
    for(auto& list:parts)for(auto& part:list){
        part.bounds.minimum=part.bounds.maximum=model.vertices[model.indices[part.first]].position;
        for(unsigned k=part.first;k<part.first+part.count;++k)part.bounds.include(model.vertices[model.indices[k]].position);
    }
    flushGpuData(model.vertices,model.vertexCount*sizeof(Vertex));
    flushGpuData(model.indices,model.indexCount*sizeof(uint16_t));
    rigidParts_=std::move(parts);
    return true;
}
bool ObjectBank::gpuSkin(Model& model) const {
    if(morphing()||!vertices_||vertices_!=model.vertexCount||matrices_>Renderer::MaxSkinBones)return false;
    for(unsigned i=0;i<vertices_;++i){
        const auto& v=source_[i];
        model.vertices[i].position={v.p[0],v.p[1],v.p[2]};model.vertices[i].normal={float(v.matrix*3),0,0};
    }
    flushGpuData(model.vertices,vertices_*sizeof(Vertex));
    return true;
}
bool ObjectBank::shareSkinnedVertices(){
    if(!sharedVertices_)sharedVertices_=buildSkinRepresentatives(source_,vertices_);
    return bool(sharedVertices_);
}
void ObjectBank::skin(const float* palette,Model& model,bool normals,int clip,float frame) const {
    if(!vertices_||model.vertexCount!=vertices_)return;
    const Morph* morph=nullptr;float weight=0;
    for(const auto& m:morph_)if(int(m.clip)==clip){
        morph=&m;const auto s=sampleClip(m.frames,1,chr_[m.clip].loop,frame);
        weight=m.weights[s.a]+(m.weights[s.b]-m.weights[s.a])*s.t;break;
    }
    if(!morph&&!normals&&shared_.ready()){
        auto& unique=sharedPositionScratch();
        for(unsigned u=0;u<shared_.first.size();++u){
            const auto& v=source_[shared_.first[u]];const float* m=palette+v.matrix*12;
            unique[u]={m[0]*v.p[0]+m[1]*v.p[1]+m[2]*v.p[2]+m[3],m[4]*v.p[0]+m[5]*v.p[1]+m[6]*v.p[2]+m[7],m[8]*v.p[0]+m[9]*v.p[1]+m[10]*v.p[2]+m[11]};
        }
        for(unsigned i=0;i<vertices_;++i)model.vertices[i].position=unique[shared_.of[i]];
        flushGpuData(model.vertices,vertices_*sizeof(Vertex));
        return;
    }
    const uint16_t* representative=normals&&!morph?sharedVertices_.get():nullptr;
    std::array<std::array<Vec3,3>,128> normalRows;
    if(normals)for(unsigned matrix=0;matrix<matrices_;++matrix){
        const float* m=palette+matrix*12;
        const Vec3 rows[3]={{m[0],m[1],m[2]},{m[4],m[5],m[6]},{m[8],m[9],m[10]}};
        Vec3 inverse[3];normalMatrix(rows,inverse);
        std::copy(std::begin(inverse),std::end(inverse),normalRows[matrix].begin());
    }
    unsigned morphVertex=0;
    for(unsigned i=0;i<vertices_;++i){
        if(representative&&representative[i]!=i){
            // The first matching source index is always earlier in this loop.
            // Copy only the transformed fields: UV/color seams stay intact.
            const auto& first=model.vertices[representative[i]];
            model.vertices[i].position=first.position;model.vertices[i].normal=first.normal;
            continue;
        }
        auto v=source_[i];
        if(morph&&morphVertex<morph->count&&morph->vertices[morphVertex]==i){
            const auto* delta=morph->deltas+morphVertex++*6;
            for(unsigned k=0;k<3;++k){v.p[k]+=delta[k]*weight;v.n[k]+=delta[k+3]*weight;}
        }
        const float* m=palette+v.matrix*12;
        model.vertices[i].position={m[0]*v.p[0]+m[1]*v.p[1]+m[2]*v.p[2]+m[3],m[4]*v.p[0]+m[5]*v.p[1]+m[6]*v.p[2]+m[7],m[8]*v.p[0]+m[9]*v.p[1]+m[10]*v.p[2]+m[11]};
        if(normals){
            const auto& rows=normalRows[v.matrix];const Vec3 n{v.n[0],v.n[1],v.n[2]};
            model.vertices[i].normal=normalized(Vec3{dot(rows[0],n),dot(rows[1],n),dot(rows[2],n)});
        }
    }
    flushGpuData(model.vertices,vertices_*sizeof(Vertex));
}
bool ObjectBank::slotBounds(std::vector<Bounds>& out,std::vector<bool>& used) const {
    if(!vertices_)return false;
    out.assign(matrices_,{});used.assign(matrices_,false);
    for(unsigned i=0;i<vertices_;++i){
        const auto& v=source_[i];const Vec3 p{v.p[0],v.p[1],v.p[2]};
        if(!used[v.matrix]){out[v.matrix]={p,p};used[v.matrix]=true;}else out[v.matrix].include(p);
    }
    return true;
}
bool ObjectBank::animatedBounds(Bounds& out) const {
    std::vector<Bounds> slots;std::vector<bool> used;
    if(!slotBounds(slots,used))return false;
    bool first=true;
    auto include=[&](const float* palette){
        for(unsigned i=0;i<matrices_;++i)if(used[i]){
            const auto b=transformBounds(slots[i],palette+i*12);
            if(first){out=b;first=false;}else{out.include(b.minimum);out.include(b.maximum);}
        }
    };
    include(bind_);
    for(const auto& m:morph_)for(unsigned j=0;j<m.count;++j){
        const auto& v=source_[m.vertices[j]];const auto* mat=bind_+v.matrix*12;const auto* delta=m.deltas+j*6;
        for(unsigned f=0;f<m.frames;++f){
            const Vec3 p{v.p[0]+delta[0]*m.weights[f],v.p[1]+delta[1]*m.weights[f],v.p[2]+delta[2]*m.weights[f]};
            out.include({mat[0]*p.x+mat[1]*p.y+mat[2]*p.z+mat[3],mat[4]*p.x+mat[5]*p.y+mat[6]*p.z+mat[7],mat[8]*p.x+mat[9]*p.y+mat[10]*p.z+mat[11]});
        }
    }
    std::vector<float> palette(size_t(matrices_)*12);
    for(const auto& c:chr_){
        const unsigned n=samples(c.frames,c.step);
        std::memcpy(palette.data(),c.base,palette.size()*4);
        for(unsigned s=0;s<n;++s){
            for(unsigned i=0;i<c.animatedCount;++i)std::memcpy(&palette[c.animated[i]*12],c.samples+(size_t(s)*c.animatedCount+i)*12,48);
            include(palette.data());
        }
    }
    return !first;
}
}
