#include <mkw/menu_bottom_screen.hpp>
#include <mkw/renderer.hpp>
#include <array>
#include <cstdio>
#include <cstring>

namespace mkw {
// Portrait and icon textures named by widgets (a few small files): loaded the
// first time a widget asks (a missing file is remembered, not retried) and kept
// until the lower screen is released.
struct MenuBottomScreen::Icons {
    struct Slot {char path[64]{};Texture texture;bool tried=false;};
    static constexpr unsigned Capacity=8;
    std::array<Slot,Capacity> slots;
    unsigned used=0;
    const Texture* get(const char* path){
        for(unsigned i=0;i<used;++i)if(!std::strcmp(slots[i].path,path))return slots[i].texture.ready?&slots[i].texture:nullptr;
        if(used==Capacity)return nullptr;
        Slot& slot=slots[used++];
        std::snprintf(slot.path,sizeof slot.path,"%s",path);
        slot.tried=true;
        slot.texture.load(slot.path);
        return slot.texture.ready?&slot.texture:nullptr;
    }
    void clear(){for(auto& slot:slots){slot.texture.reset();slot.path[0]=0;slot.tried=false;}used=0;}
};
MenuBottomScreen::MenuBottomScreen():icons_(new Icons){}
MenuBottomScreen::~MenuBottomScreen()=default;

namespace {
constexpr float ScreenW=320,ScreenH=240;
constexpr std::uint32_t Background=0x0b0f17ffu,Selection=0xffd040ffu,Text=0xffffffffu,TextDisabled=0x7a7f8cffu;
void rect(Renderer::MaterialQuad& q,float x,float y,float w,float h,std::uint32_t color){
    q.x[0]=x;q.x[1]=x+w;q.x[2]=x;q.x[3]=x+w;
    q.y[0]=y;q.y[1]=y;q.y[2]=y+h;q.y[3]=y+h;
    for(unsigned i=0;i<4;++i){q.u[i]=0;q.v[i]=0;q.color[i]=color;}
}
}

bool MenuBottomScreen::enable(Renderer& renderer){
    if(on_)return true;
    on_=renderer.enableBottom();
    owned_=on_;
    return on_;
}

bool MenuBottomScreen::borrow(Renderer& renderer){
    if(on_)return true;
    if(!renderer.bottomEnabled())return false;
    on_=true;owned_=false;
    return true;
}

void MenuBottomScreen::disable(Renderer& renderer){
    if(!on_)return;
    renderer.waitForGpu(); // nothing recorded may still sample the icon textures
    icons_->clear();
    if(owned_)renderer.releaseBottom();
    on_=false;owned_=false;
}

void MenuBottomScreen::draw(Renderer& renderer,const MenuBottomLayout& layout,bool beginFrame){
    if(!on_)return;
    if(beginFrame)renderer.beginBottom();
    // Flat fills: the screen, then each tile's selection frame and body.
    std::array<Renderer::MaterialQuad,1+2*MenuBottomLayout::Capacity> quads;
    unsigned count=0;
    rect(quads[count++],0,0,ScreenW,ScreenH,Background);
    for(unsigned i=0;i<layout.count;++i){
        const auto& w=layout.widgets[i];
        if(w.kind==BottomWidget::Label)continue;
        if(w.selected&&w.enabled)rect(quads[count++],w.x-2,w.y-2,w.w+4,w.h+4,Selection);
        rect(quads[count++],w.x,w.y,w.w,w.h,w.color);
    }
    renderer.drawScreenMaterialQuads(nullptr,quads.data(),count,0,0xffffffffu);
    // Icons (Mii portraits) at the left of their tiles.
    for(unsigned i=0;i<layout.count;++i){
        const auto& w=layout.widgets[i];
        if(w.kind==BottomWidget::Label||!w.icon[0])continue;
        if(const Texture* icon=icons_->get(w.icon))renderer.drawScreenSprite(*icon,w.x+1,w.y+1,w.h-2,w.h-2,1.f,1.f,w.enabled?1.f:.5f);
    }
    // Text over the fills, centred on tiles and buttons.
    for(unsigned i=0;i<layout.count;++i){
        const auto& w=layout.widgets[i];
        const float size=w.textSize>0?w.textSize:1;
        if(w.kind==BottomWidget::Label){renderer.drawScreenText(w.text,w.x,w.y,size,w.color?w.color:Text);continue;}
        const float width=float(std::strlen(w.text))*6*size,height=7*size;
        const std::uint32_t color=w.enabled?Text:TextDisabled;
        if(w.icon[0]){renderer.drawScreenText(w.text,w.x+w.h+2,w.y+(w.h-height)*.5f,size,color);continue;}
        if(*w.sub){
            renderer.drawScreenText(w.text,w.x+(w.w-width)*.5f,w.y+(w.h-height)*.5f-6,size,color);
            const float subWidth=float(std::strlen(w.sub))*6;
            renderer.drawScreenText(w.sub,w.x+(w.w-subWidth)*.5f,w.y+(w.h-height)*.5f+height,1,TextDisabled);
        }else renderer.drawScreenText(w.text,w.x+(w.w-width)*.5f,w.y+(w.h-height)*.5f,size,color);
    }
}
}
