#include <mkw/overlay.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace mkw::overlay {
namespace {
constexpr const char* Root="romfs:/overlay";
std::vector<std::string> files; // sorted relative paths
bool enabled=false;
void scan(const std::string& directory,const std::string& relative,unsigned depth){
    if(depth>8)return;
    DIR* d=opendir(directory.c_str());
    if(!d)return;
    while(const dirent* e=readdir(d)){
        const std::string name=e->d_name;
        if(name=="."||name=="..")continue;
        const std::string full=directory+"/"+name,rel=relative.empty()?name:relative+"/"+name;
        bool isDirectory=e->d_type==DT_DIR;
        if(e->d_type==DT_UNKNOWN){struct stat st;isDirectory=stat(full.c_str(),&st)==0&&S_ISDIR(st.st_mode);}
        if(isDirectory)scan(full,rel,depth+1);else files.push_back(rel);
    }
    closedir(d);
}
const char* clean(const char* path){while(path[0]=='.'&&path[1]=='/')path+=2;return path;}
}
void init(){
    files.clear();enabled=false;
    scan(Root,"",0);
    std::sort(files.begin(),files.end());
    enabled=!files.empty();
}
bool active(){return enabled;}
bool has(const char* path){
    if(!enabled||!path)return false;
    // Only paths relative to the asset directory: never SD saves or logs (sdmc:/...).
    if(path[0]=='/'||std::strchr(path,':'))return false;
    path=clean(path);
    const auto it=std::lower_bound(files.begin(),files.end(),path,[](const std::string& a,const char* b){return a.compare(b)<0;});
    return it!=files.end()&&it->compare(path)==0;
}
bool resolve(const char* path,char* out,std::size_t size){
    if(!has(path))return false;
    return std::snprintf(out,size,"%s/%s",Root,clean(path))<int(size);
}
}
