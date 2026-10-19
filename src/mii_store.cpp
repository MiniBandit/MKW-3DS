#include <mkw/mii_store.hpp>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace mkw {
namespace {
constexpr const char* ManifestPath="miis/miis.txt";
std::string_view trim(std::string_view s){
    while(!s.empty()&&(s.front()==' '||s.front()=='\t'||s.front()=='\r'))s.remove_prefix(1);
    while(!s.empty()&&(s.back()==' '||s.back()=='\t'||s.back()=='\r'))s.remove_suffix(1);
    return s;
}
bool validId(std::string_view id){
    if(id.empty()||id.size()>15)return false;
    for(char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    return true;
}
int hexValue(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
template<std::size_t N> void copyText(char (&destination)[N],std::string_view source){
    const std::size_t n=source.size()<N-1?source.size():N-1;
    std::memcpy(destination,source.data(),n);destination[n]=0;
}
}

unsigned parseMiiManifest(const char* text,std::size_t size,MiiEntry* out,unsigned capacity){
    unsigned count=0;
    std::string_view rest(text?text:"",text?size:0);
    while(!rest.empty()&&count<capacity){
        const std::size_t end=rest.find('\n');
        std::string_view line=trim(rest.substr(0,end));
        rest=end==std::string_view::npos?std::string_view():rest.substr(end+1);
        if(line.empty()||line.front()=='#')continue;
        std::string_view field[5];unsigned n=0;
        while(n<5){
            const std::size_t bar=line.find('|');
            field[n++]=trim(line.substr(0,bar));
            if(bar==std::string_view::npos)break;
            line=line.substr(bar+1);
        }
        if(n<3||!validId(field[0])||field[1].empty())continue;
        MiiEntry entry{};
        copyText(entry.id,field[0]);copyText(entry.name,field[1]);
        if(field[2]=="console")entry.source=MiiEntry::Console;
        else if(field[2]=="wii")entry.source=MiiEntry::WiiSave;
        else if(field[2]=="custom"||field[2].empty())entry.source=MiiEntry::Custom;
        else continue;
        if(n>3)copyText(entry.portrait,field[3]);
        if(n>4&&!field[4].empty()){
            const auto hex=field[4];
            if(hex.size()%2||hex.size()/2>entry.data.size())continue;
            bool ok=true;
            for(std::size_t i=0;i<hex.size()/2&&ok;++i){
                const int hi=hexValue(hex[2*i]),lo=hexValue(hex[2*i+1]);
                if(hi<0||lo<0)ok=false;else entry.data[i]=std::uint8_t(hi<<4|lo);
            }
            if(!ok)continue;
            entry.dataBytes=std::uint8_t(hex.size()/2);
        }
        out[count++]=entry;
    }
    return count;
}

bool MiiStore::load(){
    count_=0;
    std::FILE* f=std::fopen(ManifestPath,"rb");
    if(!f)return false;
    std::vector<char> text(16384);
    const std::size_t n=std::fread(text.data(),1,text.size(),f);
    std::fclose(f);
    count_=parseMiiManifest(text.data(),n,entries_.data(),Capacity);
    return count_>0;
}

const MiiEntry* MiiStore::find(const char* id) const {
    for(unsigned i=0;i<count_;++i)if(!std::strcmp(entries_[i].id,id))return &entries_[i];
    return nullptr;
}

MiiStore& miiStore(){static MiiStore store;return store;}
}
