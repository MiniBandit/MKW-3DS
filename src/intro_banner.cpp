#include <mkw/intro_banner.hpp>
#include <mkw/menu_assets.hpp>
#include <mkw/renderer.hpp>
#include <algorithm>
#include <cstring>

namespace mkw {
namespace {
// The plain (style 0) rendering of a menu string.
const UiSpriteInfo* menuText(const char* text){
    if(!text||!*text)return nullptr;
    for(const auto& s:UiSprites)if(s.style==0&&!std::strncmp(s.key,"text_",5)&&!std::strcmp(s.text,text))return &s;
    return nullptr;
}
constexpr unsigned SlideFrames=18;
}

struct IntroBanner::Impl {
    struct Word {const UiSpriteInfo* info=nullptr;Texture texture;};
    std::array<Word,4> first;Word course;
    unsigned shown=0,hiding=0;bool active=false,leaving=false;
};

IntroBanner::IntroBanner():impl_(std::make_unique<Impl>()){}
IntroBanner::~IntroBanner()=default;

bool IntroBanner::load(const std::array<const char*,4>& first,const char* course){
    release();
    auto take=[](Impl::Word& w,const char* text){
        w.info=menuText(text);
        if(w.info&&!w.texture.load(w.info->path))w.info=nullptr;
    };
    for(unsigned i=0;i<first.size();++i)take(impl_->first[i],first[i]);
    take(impl_->course,course);
    return impl_->course.info!=nullptr;
}
void IntroBanner::release(){
    for(auto& w:impl_->first){w.info=nullptr;w.texture.reset();}
    impl_->course.info=nullptr;impl_->course.texture.reset();
    impl_->shown=impl_->hiding=0;impl_->active=impl_->leaving=false;
}
void IntroBanner::step(bool previewActive){
    auto& m=*impl_;
    if(previewActive){m.active=true;m.leaving=false;if(m.shown<SlideFrames)++m.shown;}
    else if(m.active){m.leaving=true;if(++m.hiding>=SlideFrames){m.active=false;}}
}
void IntroBanner::draw(Renderer& renderer) const {
    const auto& m=*impl_;
    if(!m.active||!m.course.info)return;
    // Ease in from the left; out the same way.
    float t=float(m.shown)/SlideFrames;
    if(m.leaving)t=std::min(t,1.f-float(m.hiding)/SlideFrames);
    t=std::clamp(t,0.f,1.f);
    const float ease=1-(1-t)*(1-t)*(1-t),slide=(1-ease)*-160.f,alpha=ease;
    // A word: its rendered text at `height` px, with a dark drop shadow.
    auto word=[&](const Impl::Word& w,float x,float y,float height,uint32_t rgba)->float{
        if(!w.info)return 0;
        const float scale=height/w.info->h,width=w.info->w*scale;
        renderer.drawScreenSpriteRegion(w.texture,x+1.5f,y+1.5f,width,height,0,0,w.info->u,w.info->v,alpha*.8f,0x000000ffu);
        renderer.drawScreenSpriteRegion(w.texture,x,y,width,height,0,0,w.info->u,w.info->v,alpha,rgba);
        return width;
    };
    constexpr float Left=12,FirstY=168,FirstH=20,CourseY=186,CourseH=34;
    float x=Left+slide;
    // Words apart; single characters (the race number "1 / 4") closer.
    for(const auto& w:m.first)if(w.info)x+=word(w,x,FirstY,FirstH,0xffd860ffu)+(w.info->text[1]?6.f:1.f);
    word(m.course,Left+slide,CourseY,CourseH,0xffffffffu);
}
}
