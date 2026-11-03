#include <mkw/driver_look_target.hpp>
#include "mkw/animation.hpp"
#include <mkw/render_metrics.hpp>
#include <mkw/animation_budget.hpp>
#include <mkw/skin_draw_reuse.hpp>
#include <mkw/asset_pack.hpp>
#include "mkw/pose_blend.hpp"
#include <3ds.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>
namespace mkw {
// Animation banks are CPU-only. Prefer LINEAR while it has spare capacity,
// but reserve room for GPU resources and allow banks to use the ordinary heap.
void DriverAnimation::reset(){resetLook();bodyLook_={};lookCount_=0;lookYaw_=lookPitch_=0;beforeLookPalette_.clear();hasBraids_=braidsRacing_=false;braids_.reset();braidWorld_={1,0,0,0,0,1,0,0,0,0,1,0,1,0,0,0,1,0,0,0,1};effectRootSlot_=-2;effectRootReady_=false;effectRootTracks_={};effectRoot_=previousEffectRoot_={};if(data_){if(dataLinear_)linearFree(data_);else std::free(data_);}data_=nullptr;dataLinear_=true;ready=false;rigidParts_.clear();count_=faceCount_=hairNodeCount_=hairClipCount_=uvClipCount_=0;baseUV_.clear();lastClip_=-1;hasPreviousPose_=false;cachedVertices_=nullptr;cachedSettled_=false;poseBoundsValid_=false;usedMatrices_.fill(false);neutralUv_=nullptr;uvBuffer_=nullptr;uvNeutral_=true;shared_={};handleDrive_=dash_=shake_=-1;gpu_=false;shapeHidden_.fill(false);}
bool DriverAnimation::gpuSkin(Model& model){
    if(!ready||gpu_||matrices_>Renderer::MaxSkinBones||vertices_!=model.vertexCount)return false;
    if(skinNormals_){
        if(!model.skinBoneIndices)model.skinBoneIndices=static_cast<float*>(linearAlloc(vertices_*sizeof(float)));
        if(!model.skinBoneIndices)return false;
    }
    for(unsigned i=0;i<vertices_;++i){
        const auto& v=source_[i];
        model.vertices[i].position={v.p[0],v.p[1],v.p[2]};
        if(skinNormals_){model.vertices[i].normal={v.n[0],v.n[1],v.n[2]};model.skinBoneIndices[i]=float(v.matrix*3);}
        else model.vertices[i].normal={float(v.matrix*3),0,0};
    }
    if(skinNormals_)flushGpuData(model.skinBoneIndices,vertices_*sizeof(float));
    flushGpuData(model.vertices,vertices_*sizeof(Vertex));
    shapeHidden_.fill(false);cachedVertices_=nullptr;cachedSettled_=false;uvBuffer_=nullptr;uvNeutral_=true;
    uvDisjoint_=true;
    for(unsigned i=0;i<shapes_;++i)for(unsigned j=i+1;j<shapes_;++j)
        if(ranges_[i].count&&ranges_[j].count&&ranges_[i].first<ranges_[j].first+ranges_[j].count&&ranges_[j].first<ranges_[i].first+ranges_[i].count)uvDisjoint_=false;
    // A draw before the first apply() still has a pose (the first clip's first frame).
    evaluate(clips_[0],0,0,0.f,0,1.f);poseBoundsValid_=false;
    return gpu_=true;
}
unsigned DriverAnimation::shareDrawVertices(Model& model){
    if(!ready||!gpu_||vertices_!=model.vertexCount)return 0;
    std::vector<uint64_t> domains(vertices_,0);
    for(unsigned i=0;i<shapes_;++i)for(unsigned j=ranges_[i].first;j<ranges_[i].first+ranges_[i].count;++j)domains[j]|=uint64_t(1)<<i;
    const unsigned reused=shareSkinDrawIndices(model.vertices,model.skinBoneIndices,domains.data(),vertices_,model.indices,model.indexCount);
    if(reused)flushGpuData(model.indices,model.indexCount*sizeof(uint16_t));
    return reused;
}
void DriverAnimation::skinNormals(bool on){
    skinNormals_=on;
    if(on||!ready)shared_={};else shared_.build(source_,vertices_);
}
bool DriverAnimation::prepareUnlitSkinning(Model& model){
    skinNormals_=false;
    // reset() can leave source_ pointing into the released bank allocation.
    if(!ready){shared_={};return false;}
    // gpuSkin rejects an already-converted bank; it needs no CPU fallback.
    if(gpu_||gpuSkin(model)){shared_={};return true;}
    skinNormals(false);
    return false;
}
bool DriverAnimation::prepareLitSkinning(Model& model){
    if(model.staticLightingBaked)return prepareUnlitSkinning(model);
    skinNormals(true);
    if(!ready)return false;
    if(gpu_&&model.skinBoneIndices)return true;
    gpu_=false; // An earlier unlit setup must retain normals before lit draws.
    return gpuSkin(model);
}
int DriverAnimation::find(const char* name)const{if(name)for(unsigned i=0;i<count_;++i)if(!std::strcmp(name,clips_[i].name))return i;return -1;}
int DriverAnimation::face(const char* name)const{if(name)for(unsigned i=0;i<faceCount_;++i)if(!std::strcmp(name,faces_[i].name))return i;return -1;}
bool DriverAnimation::load(const char* path,const Model& model){
    reset();
    // These banks are read by the CPU, never directly by PICA or DSP. Leave
    // LINEAR room for meshes/textures and the race sound cache.
    // Prefer ordinary heap even when early loading has spare LINEAR.
    auto allocateBank=[&](size_t bytes){
        data_=static_cast<uint8_t*>(std::malloc(bytes));
        if(data_){dataLinear_=false;return true;}
        data_=static_cast<uint8_t*>(linearAlloc(bytes));dataLinear_=true;
        return data_!=nullptr;
    };
    // Menu banks come from the open menu.pak; other banks are loose files.
    bool ok=false;size_t size=0;
    if(const long packed=pack::size(path);packed>=0){
        size=size_t(packed);if(size<28||size>12*1024*1024)return false;
        if(!allocateBank(size))return false;
        ok=pack::readInto(path,data_,size);
    }else{
        FILE* f=std::fopen(path,"rb");if(!f)return false;std::fseek(f,0,SEEK_END);long length=std::ftell(f);std::rewind(f);
        if(length<0){std::fclose(f);return false;}size=size_t(length);
        if(size<28||size>12*1024*1024){std::fclose(f);return false;}
        if(!allocateBank(size)){std::fclose(f);return false;}
        ok=std::fread(data_,1,size,f)==size;std::fclose(f);
    }
    auto u32=[](const uint8_t* p){uint32_t v;std::memcpy(&v,p,4);return v;};
    if(!ok||std::memcmp(data_,"DSKM",4)||u32(data_+4)<2||u32(data_+4)>8){reset();return false;}
    vertices_=u32(data_+8);matrices_=u32(data_+12);count_=u32(data_+16);shapes_=u32(data_+20);faceCount_=u32(data_+24);
    if(vertices_!=model.vertexCount||!matrices_||matrices_>128||!count_||count_>64||shapes_>64||faceCount_>64||model.submeshes.size()>64){reset();return false;}
    size_t cursor=28;
    if(u32(data_+4)>=5){
        if(size<32){reset();return false;}
        effectRootSlot_=int32_t(u32(data_+28));cursor=32;
        if(effectRootSlot_<-1||(effectRootSlot_>=0&&unsigned(effectRootSlot_)>=matrices_)){reset();return false;}
    }
    source_=reinterpret_cast<const SourceVertex*>(data_+cursor);cursor+=vertices_*sizeof(SourceVertex);
    ranges_=reinterpret_cast<const Range*>(data_+cursor);cursor+=shapes_*sizeof(Range);
    if(cursor>size){reset();return false;}
    for(unsigned i=0;i<vertices_;++i)if(source_[i].matrix>=matrices_){reset();return false;}
    for(unsigned i=0;i<vertices_;++i){
        const auto& v=source_[i];Vec3 p{v.p[0],v.p[1],v.p[2]};
        if(!usedMatrices_[v.matrix]){sourceBounds_[v.matrix]={p,p};usedMatrices_[v.matrix]=true;}
        else sourceBounds_[v.matrix].include(p);
    }
    for(unsigned i=0;i<shapes_;++i)if(ranges_[i].first>vertices_||ranges_[i].count>vertices_-ranges_[i].first){reset();return false;}
    for(unsigned bank=0;bank<2;++bank)for(unsigned i=0;i<(bank?faceCount_:count_);++i){
        if(cursor+44>size){reset();return false;}auto& c=(bank?faces_:clips_)[i];std::memcpy(c.name,data_+cursor,32);c.name[31]=0;
        c.duration=u32(data_+cursor+32);c.loop=u32(data_+cursor+36);c.count=u32(data_+cursor+40);cursor+=44;c.data=data_+cursor;
        if(!c.count||c.count>2000||c.duration!=c.count){reset();return false;}
        cursor+=size_t(c.count)*(bank?shapes_*4:matrices_*84+shapes_*4);if(cursor>size){reset();return false;}
    }
    if(u32(data_+4)>=3){
        if(cursor+12>size||std::memcmp(data_+cursor,"HLOC",4)){reset();return false;}
        hairNodeCount_=u32(data_+cursor+4);hairClipCount_=u32(data_+cursor+8);cursor+=12;
        if((!hairNodeCount_!=!hairClipCount_)||hairNodeCount_>hairNodes_.size()||hairClipCount_>hairClips_.size()||cursor+hairNodeCount_*8>size){reset();return false;}
        for(unsigned i=0;i<hairNodeCount_;++i){
            auto& n=hairNodes_[i];n.matrix=u32(data_+cursor);n.parent=u32(data_+cursor+4);cursor+=8;
            if(n.matrix>=matrices_||n.parent>=matrices_||n.matrix==n.parent){reset();return false;}
            for(unsigned j=0;j<i;++j)if(hairNodes_[j].matrix==n.matrix||hairNodes_[j].parent==n.matrix){reset();return false;}
        }
        for(unsigned i=0;i<hairClipCount_;++i){
            if(cursor+44>size){reset();return false;}auto& h=hairClips_[i];auto& c=h.clip;
            std::memcpy(c.name,data_+cursor,32);c.name[31]=0;c.duration=u32(data_+cursor+32);c.loop=u32(data_+cursor+36);c.count=u32(data_+cursor+40);cursor+=44;
            if(!c.count||c.count>2000||c.duration!=c.count){reset();return false;}
            h.inverseRest=reinterpret_cast<const float*>(data_+cursor);cursor+=hairNodeCount_*84;
            c.data=data_+cursor;cursor+=size_t(c.count)*hairNodeCount_*84;if(cursor>size){reset();return false;}
        }
    }
    if(u32(data_+4)>=4){
        if(cursor+8>size||std::memcmp(data_+cursor,"UVAN",4)){reset();return false;}
        uvClipCount_=u32(data_+cursor+4);cursor+=8;
        if(uvClipCount_>uvClips_.size()){reset();return false;}
        for(unsigned i=0;i<uvClipCount_;++i){
            if(cursor+44>size){reset();return false;}auto& c=uvClips_[i];
            std::memcpy(c.name,data_+cursor,32);c.name[31]=0;
            c.duration=u32(data_+cursor+32);c.loop=u32(data_+cursor+36);c.count=u32(data_+cursor+40);cursor+=44;c.data=data_+cursor;
            if(!c.count||c.count>2000||c.duration!=c.count){reset();return false;}
            cursor+=size_t(c.count)*shapes_*24;if(cursor>size){reset();return false;}
        }
        baseUV_.resize(vertices_);
        for(unsigned i=0;i<vertices_;++i)baseUV_[i]={model.vertices[i].uv[0],model.vertices[i].uv[1]};
    }
    if(u32(data_+4)>=5&&effectRootSlot_==-1){
        if(cursor+8>size||std::memcmp(data_+cursor,"ROOT",4)||u32(data_+cursor+4)!=count_){reset();return false;}
        cursor+=8;
        for(unsigned i=0;i<count_;++i){
            const size_t bytes=size_t(clips_[i].count)*12;
            if(cursor>size||bytes>size-cursor){reset();return false;}
            effectRootTracks_[i]=reinterpret_cast<const float*>(data_+cursor);
            for(unsigned j=0;j<clips_[i].count*3;++j)if(!std::isfinite(effectRootTracks_[i][j])){reset();return false;}
            cursor+=bytes;
        }
    }
    if(u32(data_+4)==6||(u32(data_+4)>=7&&cursor+4<=size&&!std::memcmp(data_+cursor,"BRAD",4))){
        if(cursor+32>size||std::memcmp(data_+cursor,"BRAD",4)){reset();return false;}
        braidHead_=u32(data_+cursor+4);
        if(braidHead_>=matrices_){reset();return false;}
        for(unsigned i=0;i<6;++i){
            braidSlots_[i]=u32(data_+cursor+8+i*4);
            if(braidSlots_[i]>=matrices_||braidSlots_[i]==braidHead_){reset();return false;}
            for(unsigned j=0;j<i;++j)if(braidSlots_[j]==braidSlots_[i]){reset();return false;}
        }
        cursor+=32;hasBraids_=true;
    }
    if(u32(data_+4)>=7){
        if(cursor+12>size||std::memcmp(data_+cursor,"LOOK",4)){reset();return false;}
        lookHead_=u32(data_+cursor+4);lookCount_=u32(data_+cursor+8);cursor+=12;
        if(lookHead_>=matrices_||!lookCount_||lookCount_>matrices_||cursor+lookCount_*4>size){reset();return false;}
        bool hasHead=false;
        for(unsigned i=0;i<lookCount_;++i){
            const unsigned slot=u32(data_+cursor);cursor+=4;
            if(slot>=matrices_){reset();return false;}
            for(unsigned j=0;j<i;++j)if(lookSlots_[j]==slot){reset();return false;}
            lookSlots_[i]=slot;hasHead|=slot==lookHead_;
        }
        if(!hasHead){reset();return false;}
        beforeLookPalette_.resize(matrices_*21);
    }
    if(u32(data_+4)>=8){
        if(cursor+4>size||std::memcmp(data_+cursor,"BODY",4)){reset();return false;}cursor+=4;
        for(auto& body:bodyLook_){
            if(cursor+8>size){reset();return false;}
            body.joint=u32(data_+cursor);body.count=u32(data_+cursor+4);cursor+=8;
            if(body.joint>=matrices_||!body.count||body.count>matrices_||cursor+body.count*4>size){reset();return false;}
            bool hasJoint=false;
            for(unsigned i=0;i<body.count;++i){
                const unsigned slot=u32(data_+cursor);cursor+=4;
                if(slot>=matrices_){reset();return false;}
                for(unsigned j=0;j<i;++j)if(body.slots[j]==slot){reset();return false;}
                body.slots[i]=slot;hasJoint|=slot==body.joint;
            }
            if(!hasJoint){reset();return false;}
        }
    }
    if(cursor!=size){reset();return false;}
    for(unsigned i=0;i<model.submeshes.size();++i)baseTextures_[i]=model.submeshes[i].texture;
    handleDrive_=find("handle_drive");dash_=find("dash");shake_=find("shake");
    return ready=true;
}
namespace {
// Row-major affine/normal pair. Keeping the normal transform separate also
// preserves interpolation behavior of existing DSKM2 palettes.
void multiplyPair(float* out,const float* a,const float* b){
    float r[21];
    for(unsigned row=0;row<3;++row){
        for(unsigned col=0;col<3;++col)r[row*4+col]=a[row*4]*b[col]+a[row*4+1]*b[4+col]+a[row*4+2]*b[8+col];
        r[row*4+3]=a[row*4]*b[3]+a[row*4+1]*b[7]+a[row*4+2]*b[11]+a[row*4+3];
        for(unsigned col=0;col<3;++col)r[12+row*3+col]=a[12+row*3]*b[12+col]+a[13+row*3]*b[15+col]+a[14+row*3]*b[18+col];
    }
    std::memcpy(out,r,sizeof r);
}
bool inverse3(float* out,const float* m,unsigned stride){
    const float a=m[0],b=m[1],c=m[2],d=m[stride],e=m[stride+1],f=m[stride+2],g=m[2*stride],h=m[2*stride+1],i=m[2*stride+2];
    const float determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!std::isfinite(determinant)||std::fabs(determinant)<1e-7f)return false;
    const float scale=1.f/determinant;
    const float r[]={e*i-f*h,c*h-b*i,b*f-c*e,f*g-d*i,a*i-c*g,c*d-a*f,d*h-e*g,b*g-a*h,a*e-b*d};
    for(unsigned j=0;j<9;++j)out[j]=r[j]*scale;
    return true;
}
bool inversePair(float* out,const float* m){
    float linear[9];if(!inverse3(linear,m,4)||!inverse3(out+12,m+12,3))return false;
    for(unsigned row=0;row<3;++row){
        for(unsigned col=0;col<3;++col)out[row*4+col]=linear[row*3+col];
        out[row*4+3]=-linear[row*3]*m[3]-linear[row*3+1]*m[7]-linear[row*3+2]*m[11];
    }
    return true;
}
}
void DriverAnimation::evaluate(const Clip& c,unsigned a,unsigned b,float t,unsigned tick,float blend,bool simulateBraids){
    const unsigned stride=matrices_*84+shapes_*4;
    auto x=reinterpret_cast<const float*>(c.data+a*stride);auto y=reinterpret_cast<const float*>(c.data+b*stride);
    if(gpu_&&!lookCount_&&!hairClipCount_&&!hasBraids_&&!rigid()&&blend>=1.f){
        // GPU skinning consumes only the affine rows. Hair composition needs
        // the normal pair, so keep its full path (and all transition math).
        for(unsigned bone=0;bone<matrices_;++bone)for(unsigned i=0;i<12;++i){
            const unsigned at=bone*21+i;
            palette_[at]=x[at]+(y[at]-x[at])*t;
        }
    }else for(unsigned i=0;i<matrices_*21;++i)palette_[i]=x[i]+(y[i]-x[i])*t;
    applyHair(c,tick);
    if(blend<1)for(unsigned i=0;i<matrices_;++i){float result[21];blendPosePair(result,previousPalette_.data()+i*21,palette_.data()+i*21,blend);std::memcpy(palette_.data()+i*21,result,sizeof result);}
    if(lookCount_){
        std::copy_n(palette_.data(),matrices_*21,beforeLookPalette_.data());
        const auto angles=lookMotion_.angles();
        const auto& response=vehicleResponse_.angles();
        applyDriverBodyLookPalette(palette_.data(),bodyLook_[0].joint,bodyLook_[0].slots.data(),bodyLook_[0].count,angles.root,response.pitch[0],response.roll[0]);
        applyDriverBodyLookPalette(palette_.data(),bodyLook_[1].joint,bodyLook_[1].slots.data(),bodyLook_[1].count,angles.spine,response.pitch[1],response.roll[1]);
        applyDriverLookPalette(palette_.data(),lookHead_,lookSlots_.data(),lookCount_,lookYaw_,lookPitch_+response.pitch[2],response.roll[2]);
    }
    if(simulateBraids)applyBraids(tick);
    if(effectRootSlot_>=0){
        const float* m=palette_.data()+unsigned(effectRootSlot_)*21;
        effectRoot_={m[3],m[7],m[11]};effectRootReady_=true;
    }else if(effectRootSlot_==-1){
        const float* p=effectRootTracks_[size_t(&c-clips_.data())];
        if(p){
            const auto mix=[&](unsigned axis){return p[a*3+axis]+(p[b*3+axis]-p[a*3+axis])*t;};
            effectRoot_={mix(0),mix(1),mix(2)};
            if(blend<1)effectRoot_=previousEffectRoot_+(effectRoot_-previousEffectRoot_)*blend;
            effectRootReady_=true;
        }
    }
}
void DriverAnimation::setBraidWorld(const C3D_Mtx& world){
    if(!hasBraids_)return;
    if(!braidsRacing_)braids_.reset();
    for(unsigned r=0;r<3;++r){
        braidWorld_[r*4]=world.r[r].x;braidWorld_[r*4+1]=world.r[r].y;
        braidWorld_[r*4+2]=world.r[r].z;braidWorld_[r*4+3]=world.r[r].w;
    }
    float inverse[9];
    if(inverse3(inverse,braidWorld_.data(),4))for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)braidWorld_[12+r*3+c]=inverse[c*3+r];
    braidsRacing_=true;
}
void DriverAnimation::applyBraids(unsigned tick){
    if(!hasBraids_)return;
    float inverse[21];if(!inversePair(inverse,braidWorld_.data()))return;
    ToadetteBraids::Frames rest;
    for(unsigned i=0;i<6;++i){
        float m[21];multiplyPair(m,braidWorld_.data(),palette_.data()+braidSlots_[i]*21);
        rest[i]={{m[3],m[7],m[11]},{m[0],m[4],m[8]},{m[1],m[5],m[9]},{m[2],m[6],m[10]}};
    }
    float head[21];multiplyPair(head,braidWorld_.data(),palette_.data()+braidHead_*21);
    const auto& posed=braids_.step(rest,{head[3],head[7],head[11]},tick,braidsRacing_);
    for(unsigned i=0;i<6;++i){
        const auto& b=posed[i];float m[21]={b.x.x,b.y.x,b.z.x,b.p.x,b.x.y,b.y.y,b.z.y,b.p.y,b.x.z,b.y.z,b.z.z,b.p.z};
        float inv[9];if(!inverse3(inv,m,4))continue;
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)m[12+r*3+c]=inv[c*3+r];
        multiplyPair(palette_.data()+braidSlots_[i]*21,inverse,m);
    }
}
void DriverAnimation::applyHair(const Clip& primary,unsigned tick){
    if(!hairClipCount_)return;
    const bool waiting=!std::strcmp(primary.name,"wait");
    const bool driving=!std::strcmp(primary.name,"drive")||!std::strcmp(primary.name,"back")||!std::strcmp(primary.name,"dash")||!std::strcmp(primary.name,"drift_l")||!std::strcmp(primary.name,"drift_r");
    if(!waiting&&!driving)return; // Keep authored trick/reaction/finish hair.
    const char* name=waiting?"pc_hair_wait":"pc_hair_drive";const HairClip* selected=nullptr;
    for(unsigned i=0;i<hairClipCount_;++i)if(!std::strcmp(hairClips_[i].clip.name,name)){selected=&hairClips_[i];break;}
    if(!selected)return;
    const auto& clip=selected->clip;const unsigned frame=clip.loop?tick%clip.count:std::min(tick,clip.count-1);
    const float* secondary=reinterpret_cast<const float*>(clip.data+frame*hairNodeCount_*84);
    std::array<float,8*21> corrected{};
    // Wait uses the authored local hair loop. Driving retains primary steering
    // and adds secondary local motion relative to its frame-zero rest pose.
    // The latter is an explicit native composition policy, not recovered Wii
    // hair blend logic. Both chains remain attached to the animated head.
    for(unsigned i=0;i<hairNodeCount_;++i){
        const auto& node=hairNodes_[i];const float* parent=palette_.data()+node.parent*21;
        const float* correctedParent=parent;
        for(unsigned j=0;j<i;++j)if(hairNodes_[j].matrix==node.parent)correctedParent=corrected.data()+j*21;
        float* output=corrected.data()+i*21;
        if(waiting)multiplyPair(output,correctedParent,secondary+i*21);
        else{
            float inverse[21],local[21],delta[21];if(!inversePair(inverse,parent))return;
            multiplyPair(local,inverse,palette_.data()+node.matrix*21);
            multiplyPair(delta,selected->inverseRest+i*21,secondary+i*21);
            multiplyPair(local,local,delta);multiplyPair(output,correctedParent,local);
        }
        for(unsigned j=0;j<21;++j)if(!std::isfinite(output[j]))return;
    }
    for(unsigned i=0;i<hairNodeCount_;++i)std::memcpy(palette_.data()+hairNodes_[i].matrix*21,corrected.data()+i*21,84);
}
bool DriverAnimation::apply(Model& model,int clip,float frame,unsigned tick,int expression,float expressionFrame,const ClipFrustum* frustum){
    if(!ready||clip<0||unsigned(clip)>=count_)return true;
    auto sample=[](const Clip& c,float t){return c.loop?std::fmod(std::max(t,0.f),float(c.count)):std::clamp(t,0.f,float(c.count-1));};
    const auto& c=clips_[clip];frame=sample(c,frame);unsigned a=unsigned(frame),b=a+1<c.count?a+1:(c.loop?0:a);float t=frame-a;
    if(lastClip_!=clip){hasPreviousPose_=lastClip_>=0;previousPalette_=palette_;if(lookCount_)std::copy_n(beforeLookPalette_.data(),matrices_*21,previousPalette_.data());previousEffectRoot_=effectRoot_;transitionTick_=tick;lastClip_=clip;}
    float blend=motionBlendValid_?motionBlendWeight_:(hasPreviousPose_?std::clamp(float(tick-transitionTick_+1)/6.f,0.f,1.f):1.f);
    const float faceFrame=expression>=0&&unsigned(expression)<faceCount_?sample(faces_[expression],expressionFrame):0.f;
    auto poseVisible=[&](){
        if(!frustum)return true;
        if(!poseBoundsValid_){
            // Matrix-local vertex AABBs conservatively contain every skinned
            // vertex, including fractional samples, transition blends and hair.
            poseBounds_={{0,0,0},{0,0,0}};
            for(unsigned i=0;i<matrices_;++i)if(usedMatrices_[i]){
                auto b=transformBoundsPadded(sourceBounds_[i],palette_.data()+i*21);
                poseBounds_.include(b.minimum);poseBounds_.include(b.maximum);
            }
            poseBoundsValid_=true;
        }
        return frustum->intersects(poseBounds_);
    };
    const unsigned step=fullRate?1:animationStep();
    if(step>1&&cachedSettled_&&blend>=1&&cachedVertices_==model.vertices&&cachedClip_==clip&&
       cachedExpression_==expression&&tick>=cachedTick_&&tick/step==cachedTick_/step){
        ++renderMetrics.animationHolds;return poseVisible();
    }
    if(cachedSettled_&&blend>=1&&cachedVertices_==model.vertices&&cachedClip_==clip&&
       cachedFrame_==frame&&cachedExpression_==expression&&cachedExpressionFrame_==faceFrame&&
       ((!hairClipCount_&&!hasBraids_)||cachedTick_==tick)){++renderMetrics.poseReuses;return poseVisible();}
    unsigned stride=matrices_*84+shapes_*4;
    evaluate(c,a,b,t,tick,blend);
    poseBoundsValid_=false;
    if(!poseVisible()){cachedSettled_=false;return false;}
    ++renderMetrics.poseEvaluations;renderMetrics.skinnedVertices+=vertices_;
    cachedVertices_=model.vertices;cachedClip_=clip;cachedFrame_=frame;
    cachedExpression_=expression;cachedExpressionFrame_=faceFrame;cachedTick_=tick;cachedSettled_=blend>=1;
    const auto shown=reinterpret_cast<const uint32_t*>(c.data+a*stride+matrices_*84);
    const int32_t* textures=nullptr;
    if(expression>=0&&unsigned(expression)<faceCount_){const auto& f=faces_[expression];textures=reinterpret_cast<const int32_t*>(f.data+unsigned(sample(f,expressionFrame))*shapes_*4);}
    const float* uv=nullptr;
    if(expression>=0&&unsigned(expression)<faceCount_)for(unsigned i=0;i<uvClipCount_;++i){
        const auto& uc=uvClips_[i];if(!std::strcmp(uc.name,faces_[expression].name)){
            uv=reinterpret_cast<const float*>(uc.data+unsigned(sample(uc,expressionFrame))*shapes_*24);break;
        }
    }
    if(auto* deferred=deferredWrites()){
        // Bank data is immutable; the CPU path's pose is copied.
        struct Write {DriverAnimation* bank;Model* model;const uint32_t* shown;const int32_t* textures;const float* uv;float* palette;};
        const std::size_t paletteBytes=gpu_?0:std::size_t(matrices_)*84;
        auto* w=static_cast<Write*>(deferred->allocate(sizeof(Write)+paletteBytes));
        *w={this,&model,shown,textures,uv,gpu_?nullptr:reinterpret_cast<float*>(w+1)};
        if(paletteBytes)std::memcpy(w->palette,palette_.data(),paletteBytes);
        deferred->queue([](void* p){auto& w=*static_cast<Write*>(p);w.bank->write(*w.model,w.shown,w.textures,w.uv,w.palette);},w);
    }else write(model,shown,textures,uv,palette_.data());
    return true;
}
void DriverAnimation::write(Model& model,const uint32_t* shown,const int32_t* textures,const float* uv,const float* palette){
    if(gpu_){
        // The GPU applies palette_. Hidden shapes collapse to one point (no
        // fragments, as the CPU path's zeroed positions); writes only on change.
        unsigned lo=vertices_,hi=0;
        auto touch=[&](unsigned first,unsigned count){if(count){lo=std::min(lo,first);hi=std::max(hi,first+count);}};
        for(unsigned i=0;i<shapes_;++i){
            const bool hide=!shown[i];if(hide==shapeHidden_[i])continue;
            for(unsigned j=ranges_[i].first;j<ranges_[i].first+ranges_[i].count;++j){
                const auto& v=source_[j];model.vertices[j].position=hide?Vec3{0,0,0}:Vec3{v.p[0],v.p[1],v.p[2]};
            }
            shapeHidden_[i]=hide;touch(ranges_[i].first,ranges_[i].count);
        }
        // UV0 = baseUV_ outside the shape ranges and each shape's transform of
        // baseUV_ inside its range (later shapes win). A buffer whose state
        // is known rewrites only shapes whose 2x3 transform bits changed.
        auto transform=[&](unsigned j){const auto* m=uv+j*6;
            for(unsigned i=ranges_[j].first;i<ranges_[j].first+ranges_[j].count;++i){
                const auto& p=baseUV_[i];model.vertices[i].uv[0]=m[0]*p[0]+m[1]*p[1]+m[2];model.vertices[i].uv[1]=m[3]*p[0]+m[4]*p[1]+m[5];
            }
            touch(ranges_[j].first,ranges_[j].count);
        };
        const bool known=uvBuffer_==model.vertices;
        if(!known||(!uv&&!uvNeutral_)){
            for(unsigned i=0;i<baseUV_.size();++i){model.vertices[i].uv[0]=baseUV_[i][0];model.vertices[i].uv[1]=baseUV_[i][1];}
            touch(0,unsigned(baseUV_.size()));
            if(uv)for(unsigned j=0;j<shapes_;++j)transform(j);
        }else if(uv&&uvNeutral_){
            for(unsigned j=0;j<shapes_;++j)transform(j);
        }else if(uv){
            if(uvDisjoint_){for(unsigned j=0;j<shapes_;++j)if(std::memcmp(appliedUv_.data()+j*6,uv+j*6,24))transform(j);}
            else if(std::memcmp(appliedUv_.data(),uv,shapes_*24))for(unsigned j=0;j<shapes_;++j)transform(j);
        }
        if(uv)std::memcpy(appliedUv_.data(),uv,shapes_*24);
        uvBuffer_=model.vertices;uvNeutral_=!uv;
        for(unsigned i=0;i<model.submeshes.size();++i){auto& sub=model.submeshes[i];sub.texture=baseTextures_[i];if(textures)for(unsigned j=0;j<shapes_;++j)if(sub.first>=ranges_[j].first&&sub.first<ranges_[j].first+ranges_[j].count&&textures[j]>=0&&unsigned(textures[j])<model.textures.size()){sub.texture=textures[j];break;}}
        if(hi>lo)flushGpuData(model.vertices+lo,(hi-lo)*sizeof(Vertex));
        return;
    }
    if(!skinNormals_&&shared_.ready()){
        auto& unique=sharedPositionScratch();
        for(unsigned u=0;u<shared_.first.size();++u){
            const auto& v=source_[shared_.first[u]];auto m=palette+v.matrix*21;
            unique[u]={m[0]*v.p[0]+m[1]*v.p[1]+m[2]*v.p[2]+m[3],m[4]*v.p[0]+m[5]*v.p[1]+m[6]*v.p[2]+m[7],m[8]*v.p[0]+m[9]*v.p[1]+m[10]*v.p[2]+m[11]};
        }
        for(unsigned i=0;i<vertices_;++i)model.vertices[i].position=unique[shared_.of[i]];
    }else for(unsigned i=0;i<vertices_;++i){
        const auto& v=source_[i];auto m=palette+v.matrix*21;
        Vec3 p{m[0]*v.p[0]+m[1]*v.p[1]+m[2]*v.p[2]+m[3],m[4]*v.p[0]+m[5]*v.p[1]+m[6]*v.p[2]+m[7],m[8]*v.p[0]+m[9]*v.p[1]+m[10]*v.p[2]+m[11]};
        model.vertices[i].position=p;
        if(skinNormals_&&!model.staticLightingBaked){
            Vec3 n{m[12]*v.n[0]+m[13]*v.n[1]+m[14]*v.n[2],m[15]*v.n[0]+m[16]*v.n[1]+m[17]*v.n[2],m[18]*v.n[0]+m[19]*v.n[1]+m[20]*v.n[2]};
            model.vertices[i].normal=normalized(n);
        }
    }
    for(unsigned i=0;i<shapes_;++i)if(!shown[i])for(unsigned j=ranges_[i].first;j<ranges_[i].first+ranges_[i].count;++j)model.vertices[j].position={0,0,0};
    // Always restore neutral UVs first: blinking or leaving reverse must not
    // inherit the preceding expression's atlas offset or accumulate transforms.
    // A buffer this bank left neutral (and nothing else writes UV0) already is.
    if(uv||neutralUv_!=model.vertices)
        for(unsigned i=0;i<baseUV_.size();++i){model.vertices[i].uv[0]=baseUV_[i][0];model.vertices[i].uv[1]=baseUV_[i][1];}
    if(uv)for(unsigned j=0;j<shapes_;++j){const auto* m=uv+j*6;
        for(unsigned i=ranges_[j].first;i<ranges_[j].first+ranges_[j].count;++i){
            const auto& p=baseUV_[i];model.vertices[i].uv[0]=m[0]*p[0]+m[1]*p[1]+m[2];model.vertices[i].uv[1]=m[3]*p[0]+m[4]*p[1]+m[5];
        }
    }
    neutralUv_=uv?nullptr:model.vertices;
    for(unsigned i=0;i<model.submeshes.size();++i){auto& sub=model.submeshes[i];sub.texture=baseTextures_[i];if(textures)for(unsigned j=0;j<shapes_;++j)if(sub.first>=ranges_[j].first&&sub.first<ranges_[j].first+ranges_[j].count&&textures[j]>=0&&unsigned(textures[j])<model.textures.size()){sub.texture=textures[j];break;}}
    flushGpuData(model.vertices,vertices_*sizeof(Vertex));
}
void DriverAnimation::stepLook(int clip,float frame,unsigned tick,bool active,Vec3 target,
        const C3D_Mtx& seated,bool body,const DriverLookParameters& parameters,
        const DriverPose& motion,bool handleRelativeBike,float xAxisY,float zAxisY){
    if(lookTickValid_&&lookTick_==tick)return;
    lookTick_=tick;lookTickValid_=true;
    if(!lookCount_||!ready){resetLook();return;}
    motionBlendValid_=true;motionBlendWeight_=motion.blendWeight;
    vehicleResponse_.step(motion.state,motion.previousState,motion.transitioning,
        motion.blendWeight,handleRelativeBike,xAxisY,zAxisY,parameters);
    // Evaluate the same transition as the rendered pose, without skin uploads.
    // beforeLookPalette_ excludes last tick's procedural rotations.
    // This query computes head targeting before the render transform is set.
    // Advance world-space braid physics only with the final rendered pose.
    const unsigned step=fullRate?1:animationStep();
    const bool hold=step>1&&cachedSettled_&&cachedClip_==clip&&tick>=cachedTick_&&tick/step==cachedTick_/step;
    if(hold)return;
    if(!pose(clip,frame,tick,false)){resetLook();return;}
    float world[12];for(unsigned row=0;row<3;++row){world[row*4]=seated.r[row].x;world[row*4+1]=seated.r[row].y;world[row*4+2]=seated.r[row].z;world[row*4+3]=seated.r[row].w;}
    auto a=driverLookTargetAngles(world,palette_.data()+lookHead_*21,target);
    const auto angles=lookMotion_.step(active&&a.valid,body,a.yaw,a.pitch,a.kartYaw,
        0,vehicleResponse_.angles().pitchSum(),parameters);
    cachedSettled_=false;setLookAngles(angles.yaw,angles.pitch);
}

const float* DriverAnimation::pose(int clip,float frame,unsigned tick,bool simulateBraids){
    if(!ready||clip<0||unsigned(clip)>=count_)return nullptr;
    auto sample=[](const Clip& c,float t){return c.loop?std::fmod(std::max(t,0.f),float(c.count)):std::clamp(t,0.f,float(c.count-1));};
    const auto& c=clips_[clip];frame=sample(c,frame);unsigned a=unsigned(frame),b=a+1<c.count?a+1:(c.loop?0:a);float t=frame-a;
    if(lastClip_!=clip){hasPreviousPose_=lastClip_>=0;previousPalette_=palette_;if(lookCount_)std::copy_n(beforeLookPalette_.data(),matrices_*21,previousPalette_.data());previousEffectRoot_=effectRoot_;transitionTick_=tick;lastClip_=clip;}
    const float blend=motionBlendValid_?motionBlendWeight_:(hasPreviousPose_?std::clamp(float(tick-transitionTick_+1)/6.f,0.f,1.f):1.f);
    evaluate(c,a,b,t,tick,blend,simulateBraids);
    ++renderMetrics.poseEvaluations;
    return palette_.data();
}
bool DriverAnimation::makeRigid(Model& model){
    if(!ready||rigid()||hairClipCount_||hasBraids_||vertices_!=model.vertexCount)return false;
    const unsigned stride=matrices_*84+shapes_*4;
    // Shape visibility must be constant: a hidden shape is simply not drawn.
    std::vector<uint32_t> visible(shapes_,1);
    for(unsigned clip=0;clip<count_;++clip)for(unsigned frame=0;frame<clips_[clip].count;++frame){
        const auto flags=reinterpret_cast<const uint32_t*>(clips_[clip].data+frame*stride+matrices_*84);
        for(unsigned i=0;i<shapes_;++i){if(clip==0&&frame==0)visible[i]=flags[i];else if(bool(flags[i])!=bool(visible[i]))return false;}
    }
    std::vector<uint8_t> hidden(vertices_,0);
    for(unsigned i=0;i<shapes_;++i)if(!visible[i])for(unsigned j=ranges_[i].first;j<ranges_[i].first+ranges_[i].count;++j)hidden[j]=1;
    for(const auto& s:model.submeshes)for(unsigned i=s.first;i<s.first+s.count;i+=3){
        const auto m=source_[model.indices[i]].matrix;
        if(source_[model.indices[i+1]].matrix!=m||source_[model.indices[i+2]].matrix!=m)return false;
    }
    std::vector<std::vector<RigidPart>> parts(model.submeshes.size());
    std::vector<std::array<uint16_t,3>> triangles;
    for(size_t n=0;n<model.submeshes.size();++n){
        const auto& s=model.submeshes[n];
        triangles.clear();
        for(unsigned i=s.first;i<s.first+s.count;i+=3)triangles.push_back({model.indices[i],model.indices[i+1],model.indices[i+2]});
        // Group by matrix (stable, preserving authored order within a group);
        // triangles of hidden shapes sort last and are never drawn.
        auto key=[&](const std::array<uint16_t,3>& t){return hidden[t[0]]?~0u:source_[t[0]].matrix;};
        std::stable_sort(triangles.begin(),triangles.end(),[&](const auto& a,const auto& b){return key(a)<key(b);});
        for(size_t k=0;k<triangles.size();++k){
            const unsigned at=s.first+unsigned(k)*3;std::memcpy(model.indices+at,triangles[k].data(),6);
            const unsigned matrix=key(triangles[k]);if(matrix==~0u)continue;
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
}
