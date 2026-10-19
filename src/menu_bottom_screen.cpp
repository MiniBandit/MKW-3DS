#include <mkw/menu_bottom_screen.hpp>
#include <mkw/renderer.hpp>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace mkw {
namespace {
constexpr float ScreenW=320,ScreenH=240;
constexpr std::uint32_t Background=0x0b0f17ffu,Selection=0xffd040ffu,Text=0xffffffffu,TextDisabled=0x7a7f8cffu;

// The renderer's built-in 5 x 7 font draws one quad per lit pixel, which cannot
// carry a page of labels. This copy of the same glyphs is rendered once into a
// small texture, so a letter costs one quad. Columns, bit 0 = top row.
struct Glyph {char c;std::uint8_t column[5];};
constexpr Glyph Glyphs[]={
    {'A',{0x7e,0x11,0x11,0x11,0x7e}},{'B',{0x7f,0x49,0x49,0x49,0x36}},{'C',{0x3e,0x41,0x41,0x41,0x22}},
    {'D',{0x7f,0x41,0x41,0x22,0x1c}},{'E',{0x7f,0x49,0x49,0x49,0x41}},{'F',{0x7f,0x09,0x09,0x09,0x01}},
    {'G',{0x3e,0x41,0x49,0x49,0x7a}},{'H',{0x7f,0x08,0x08,0x08,0x7f}},{'I',{0,0x41,0x7f,0x41,0}},
    {'J',{0x20,0x40,0x41,0x3f,0x01}},{'K',{0x7f,0x08,0x14,0x22,0x41}},{'L',{0x7f,0x40,0x40,0x40,0x40}},
    {'M',{0x7f,0x02,0x0c,0x02,0x7f}},{'N',{0x7f,0x04,0x08,0x10,0x7f}},{'O',{0x3e,0x41,0x41,0x41,0x3e}},
    {'P',{0x7f,0x09,0x09,0x09,0x06}},{'Q',{0x3e,0x41,0x51,0x21,0x5e}},{'R',{0x7f,0x09,0x19,0x29,0x46}},
    {'S',{0x46,0x49,0x49,0x49,0x31}},{'T',{0x01,0x01,0x7f,0x01,0x01}},{'U',{0x3f,0x40,0x40,0x40,0x3f}},
    {'V',{0x1f,0x20,0x40,0x20,0x1f}},{'W',{0x3f,0x40,0x38,0x40,0x3f}},{'X',{0x63,0x14,0x08,0x14,0x63}},
    {'Y',{0x07,0x08,0x70,0x08,0x07}},{'Z',{0x61,0x51,0x49,0x45,0x43}},
    {'0',{0x3e,0x51,0x49,0x45,0x3e}},{'1',{0,0x42,0x7f,0x40,0}},{'2',{0x42,0x61,0x51,0x49,0x46}},
    {'3',{0x21,0x41,0x45,0x4b,0x31}},{'4',{0x18,0x14,0x12,0x7f,0x10}},{'5',{0x27,0x45,0x45,0x45,0x39}},
    {'6',{0x3c,0x4a,0x49,0x49,0x30}},{'7',{0x01,0x71,0x09,0x05,0x03}},{'8',{0x36,0x49,0x49,0x49,0x36}},
    {'9',{0x06,0x49,0x49,0x29,0x1e}},
    {'/',{0x60,0x18,0x04,0x03,0}},{':',{0,0x36,0x36,0,0}},{'!',{0,0,0x5f,0,0}},{'-',{8,8,8,8,8}},{'.',{0,0x60,0x60,0,0}},
    {'\'',{0,0x05,0x03,0,0}},{'(',{0,0x1c,0x22,0x41,0}},{')',{0,0x41,0x22,0x1c,0}},{'+',{8,8,0x3e,8,8}},
    {'?',{0x02,0x01,0x51,0x09,0x06}},{',',{0,0x50,0x30,0,0}}
};
constexpr unsigned GlyphCount=sizeof Glyphs/sizeof Glyphs[0];
constexpr unsigned AtlasSize=64,Cell=8,CellsPerRow=AtlasSize/Cell;
static_assert(GlyphCount<=CellsPerRow*CellsPerRow,"glyph atlas is full");

int glyphIndex(unsigned char c){
    if(c>='a'&&c<='z')c=char(c-32);
    for(unsigned i=0;i<GlyphCount;++i)if(Glyphs[i].c==char(c))return int(i);
    return -1;
}
// 8 x 8 tiles in rows, pixels inside a tile in Morton order.
unsigned morton(unsigned x,unsigned y){
    return (x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
}
void rect(Renderer::MaterialQuad& q,float x,float y,float w,float h,std::uint32_t color){
    q.x[0]=x;q.x[1]=x+w;q.x[2]=x;q.x[3]=x+w;
    q.y[0]=y;q.y[1]=y;q.y[2]=y+h;q.y[3]=y+h;
    for(unsigned i=0;i<4;++i){q.u[i]=0;q.v[i]=0;q.color[i]=color;}
}
}

// The glyph atlas, and portrait and icon textures named by widgets (a few small
// files): icons load the first time a widget asks (a missing file is remembered,
// not retried); everything is kept until the lower screen is released.
struct MenuBottomScreen::Icons {
    struct Slot {char path[64]{};Texture texture;};
    static constexpr unsigned Capacity=8;
    std::array<Slot,Capacity> slots;
    unsigned used=0;
    Texture font;
    // Per-frame quad buffers live here (heap): 80 bytes a quad is too much for the main thread's stack.
    std::array<Renderer::MaterialQuad,Renderer::BottomQuadCapacity> quads;
    bool makeFont(){
        if(font.ready)return true;
        if(!C3D_TexInit(&font.gpu,AtlasSize,AtlasSize,GPU_RGBA8))return false;
        std::vector<std::uint32_t> pixels(AtlasSize*AtlasSize,0u); // heap: the menu loop runs on a small stack
        for(unsigned g=0;g<GlyphCount;++g){
            const unsigned ox=(g%CellsPerRow)*Cell,oy=(g/CellsPerRow)*Cell;
            for(unsigned col=0;col<5;++col)for(unsigned row=0;row<7;++row)
                if(Glyphs[g].column[col]&(1u<<row)){
                    const unsigned x=ox+col,yTop=oy+row;
                    // Top row first (checked on screen: the sprite and quad paths read it upright).
                    const unsigned tileX=x/8,tileY=yTop/8,inX=x%8,inY=yTop%8;
                    pixels[(tileY*(AtlasSize/8)+tileX)*64+morton(inX,inY)]=0xffffffffu;
                }
        }
        C3D_TexLoadImage(&font.gpu,pixels.data(),GPU_TEXFACE_2D,0);
        C3D_TexFlush(&font.gpu);
        C3D_TexSetFilter(&font.gpu,GPU_NEAREST,GPU_NEAREST);
        C3D_TexSetWrap(&font.gpu,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        font.ready=true;
        return true;
    }
    const Texture* get(const char* path){
        for(unsigned i=0;i<used;++i)if(!std::strcmp(slots[i].path,path))return slots[i].texture.ready?&slots[i].texture:nullptr;
        if(used==Capacity)return nullptr;
        Slot& slot=slots[used++];
        std::snprintf(slot.path,sizeof slot.path,"%s",path);
        slot.texture.load(slot.path);
        return slot.texture.ready?&slot.texture:nullptr;
    }
    void clear(){for(auto& slot:slots){slot.texture.reset();slot.path[0]=0;}used=0;font.reset();}
};
MenuBottomScreen::MenuBottomScreen():icons_(new Icons){}
MenuBottomScreen::~MenuBottomScreen()=default;

bool MenuBottomScreen::enable(Renderer& renderer){
    if(on_)return true;
    on_=renderer.enableBottom();
    owned_=on_;
    if(on_)icons_->makeFont();
    return on_;
}

bool MenuBottomScreen::borrow(Renderer& renderer){
    if(on_)return true;
    if(!renderer.bottomEnabled())return false;
    on_=true;owned_=false;
    icons_->makeFont();
    return true;
}

void MenuBottomScreen::disable(Renderer& renderer){
    if(!on_)return;
    renderer.waitForGpu(); // nothing recorded may still sample the icon or font textures
    icons_->clear();
    if(owned_)renderer.releaseBottom();
    on_=false;owned_=false;
}

void MenuBottomScreen::drawTopText(Renderer& renderer,const MenuTopTexts& texts){
    if(!on_||!texts.count||!icons_->font.ready)return;
    auto& quads=icons_->quads;
    unsigned count=0;
    auto glyphs=[&](const char* s,float x,float y,float size,std::uint32_t color){
        x=float(int(x+.5f));y=float(int(y+.5f));
        for(;*s;++s,x+=6*size){
            const int g=glyphIndex(static_cast<unsigned char>(*s));
            if(g<0||count>=quads.size())continue;
            auto& q=quads[count++];
            const float u0=(float((unsigned(g)%CellsPerRow)*Cell)+.03f)/AtlasSize,v0=(float((unsigned(g)/CellsPerRow)*Cell)+.03f)/AtlasSize;
            const float u1=u0+4.94f/AtlasSize,v1=v0+6.94f/AtlasSize;
            q.x[0]=x;q.x[1]=x+5*size;q.x[2]=x;q.x[3]=x+5*size;
            q.y[0]=y;q.y[1]=y;q.y[2]=y+7*size;q.y[3]=y+7*size;
            q.u[0]=u0;q.u[1]=u1;q.u[2]=u0;q.u[3]=u1;
            q.v[0]=v0;q.v[1]=v0;q.v[2]=v1;q.v[3]=v1;
            for(unsigned k=0;k<4;++k)q.color[k]=color;
        }
    };
    for(unsigned i=0;i<texts.count;++i){
        const auto& t=texts.lines[i];
        glyphs(t.text,t.x+1,t.y+1,t.size,0x000000c0u); // shadow
        glyphs(t.text,t.x,t.y,t.size,t.rgba);
    }
    renderer.drawScreenMaterialQuads(&icons_->font,quads.data(),count,0x00000000u,0xffffffffu);
}

void MenuBottomScreen::draw(Renderer& renderer,const MenuBottomLayout& layout,bool beginFrame,const std::function<void()>& between){
    if(!on_)return;
    if(beginFrame)renderer.beginBottom();
    // The lower screen's quads (fills, then letters) share one capacity.
    constexpr unsigned Capacity=Renderer::BottomQuadCapacity-Icons::Capacity-2;
    auto& quads=icons_->quads;
    unsigned count=0;
    // Flat fills: the screen, then each tile's selection frame and body.
    rect(quads[count++],0,0,ScreenW,ScreenH,Background);
    for(unsigned i=0;i<layout.count&&count+2<Capacity;++i){
        const auto& w=layout.widgets[i];
        if(w.kind==BottomWidget::Label||w.hidden)continue;
        if(w.selected&&w.enabled)rect(quads[count++],w.x-2,w.y-2,w.w+4,w.h+4,Selection);
        rect(quads[count++],w.x,w.y,w.w,w.h,w.color);
    }
    renderer.drawScreenMaterialQuads(nullptr,quads.data(),count,0,0xffffffffu);
    if(between)between();
    // Icons (Mii portraits) at the left of their tiles.
    for(unsigned i=0;i<layout.count;++i){
        const auto& w=layout.widgets[i];
        if(w.kind==BottomWidget::Label||w.hidden||!w.icon[0])continue;
        if(const Texture* icon=icons_->get(w.icon))renderer.drawScreenSprite(*icon,w.x+1,w.y+1,w.h-2,w.h-2,1.f,1.f,w.enabled?1.f:.5f);
    }
    // Letters: one textured quad each, in the widget's colour.
    count=0;
    auto text=[&](const char* s,float x,float y,float size,std::uint32_t color){
        x=float(int(x+.5f));y=float(int(y+.5f)); // whole pixels: nearest sampling of a 5 x 7 font needs the pixel grid
        for(;*s;++s,x+=6*size){
            const int g=glyphIndex(static_cast<unsigned char>(*s));
            if(g<0||count>=Capacity)continue;
            auto& q=quads[count++];
            // Inset a hair from the texel edges so nearest sampling never picks a neighbour.
            const float u0=(float((unsigned(g)%CellsPerRow)*Cell)+.03f)/AtlasSize,v0=(float((unsigned(g)/CellsPerRow)*Cell)+.03f)/AtlasSize;
            const float u1=u0+4.94f/AtlasSize,v1=v0+6.94f/AtlasSize;
            q.x[0]=x;q.x[1]=x+5*size;q.x[2]=x;q.x[3]=x+5*size;
            q.y[0]=y;q.y[1]=y;q.y[2]=y+7*size;q.y[3]=y+7*size;
            q.u[0]=u0;q.u[1]=u1;q.u[2]=u0;q.u[3]=u1;
            q.v[0]=v0;q.v[1]=v0;q.v[2]=v1;q.v[3]=v1;
            for(unsigned k=0;k<4;++k)q.color[k]=color;
        }
    };
    for(unsigned i=0;i<layout.count;++i){
        const auto& w=layout.widgets[i];
        if(w.hidden)continue;
        const float size=w.textSize>0?w.textSize:1;
        if(w.kind==BottomWidget::Label){text(w.text,w.x,w.y,size,w.color?w.color:Text);continue;}
        const float width=float(std::strlen(w.text))*6*size-size,height=7*size;
        const std::uint32_t color=w.enabled?Text:TextDisabled;
        if(w.icon[0]){text(w.text,w.x+w.h+2,w.y+(w.h-height)*.5f,size,color);continue;}
        if(*w.sub){
            text(w.text,w.x+(w.w-width)*.5f,w.y+(w.h-height)*.5f-6,size,color);
            const float subWidth=float(std::strlen(w.sub))*6-1;
            text(w.sub,w.x+(w.w-subWidth)*.5f,w.y+(w.h-height)*.5f+height,1,TextDisabled);
        }else text(w.text,w.x+(w.w-width)*.5f,w.y+(w.h-height)*.5f,size,color);
    }
    if(count&&icons_->font.ready)renderer.drawScreenMaterialQuads(&icons_->font,quads.data(),count,0x00000000u,0xffffffffu);
}
}
