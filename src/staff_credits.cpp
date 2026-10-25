#include <mkw/staff_credits.hpp>
#include <mkw/menu_bottom.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mkw {
namespace {
constexpr float LineHeight=12,ScreenH=240,ScreenW=320;
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
uint16_t be16(const uint8_t* p){return uint16_t(p[0]<<8|p[1]);}
}

bool StaffRoll::load(const char* path){
    lines_.clear();
    FILE* f=std::fopen(path,"rb");if(!f)return false;
    std::vector<uint8_t> b;
    if(std::fseek(f,0,SEEK_END)==0){const long n=std::ftell(f);if(n>0&&n<1<<20){b.resize(size_t(n));std::rewind(f);if(std::fread(b.data(),1,b.size(),f)!=b.size())b.clear();}}
    std::fclose(f);
    if(b.size()<0x20||std::memcmp(b.data(),"MESGbmg1",8))return false;
    // Sections after the 0x20-byte header: INF1 (offsets into DAT1), DAT1 (UTF-16BE).
    size_t inf=0,dat=0,datSize=0;
    for(size_t off=0x20;off+8<=b.size();){
        const uint32_t size=be32(&b[off+4]);if(size<8||off+size>b.size())break;
        if(!std::memcmp(&b[off],"INF1",4))inf=off;
        if(!std::memcmp(&b[off],"DAT1",4)){dat=off;datSize=size;}
        off+=size;
    }
    if(!inf||!dat)return false;
    const unsigned count=be16(&b[inf+8]),entry=be16(&b[inf+10]);
    if(entry<4)return false;
    for(unsigned i=0;i<count;++i){
        const size_t at=inf+16+size_t(i)*entry;if(at+4>b.size())break;
        size_t p=dat+8+be32(&b[at]);
        std::string text;
        // One message is a heading or names (several lines); escapes (0x1A) are layout codes.
        while(p+1<dat+datSize){
            const uint16_t c=be16(&b[p]);p+=2;
            if(!c)break;
            if(c==0x1A){if(p<b.size())p+=size_t(b[p])-2;continue;}
            if(c=='\n'){
                if(!text.empty()){const bool indented=text[0]==' ';size_t s=text.find_first_not_of(' ');
                    lines_.push_back({s==std::string::npos?"":text.substr(s),!indented});}
                text.clear();continue;
            }
            text.push_back(c<128?char(c):'?');
        }
        if(!text.empty()){const bool indented=text[0]==' ';size_t s=text.find_first_not_of(' ');
            if(s!=std::string::npos)lines_.push_back({text.substr(s),!indented});}
        // A gap after each block of names, before the next heading.
        if(!lines_.empty()&&!lines_.back().heading&&!lines_.back().text.empty())lines_.push_back({"",false});
    }
    return !lines_.empty();
}
float StaffRoll::scrollLength()const{return float(lines_.size())*LineHeight+ScreenH;}
void StaffRoll::layout(float pixels,MenuBottomLayout& out)const{
    out.count=0;out.page=UiPage::Strap;
    for(unsigned i=0;i<lines_.size();++i){
        const float y=ScreenH-pixels+float(i)*LineHeight;
        if(y<-LineHeight||y>ScreenH||lines_[i].text.empty())continue;
        auto* w=out.add();if(!w)break;
        w->kind=BottomWidget::Label;w->textSize=lines_[i].heading?1.f:1.f;
        std::snprintf(w->text,sizeof w->text,"%s",lines_[i].text.c_str());
        const float width=float(std::strlen(w->text))*6-1;
        w->x=(ScreenW-width)*.5f;w->y=y;w->color=lines_[i].heading?0xffd040ffu:0xffffffffu;
    }
}
}
