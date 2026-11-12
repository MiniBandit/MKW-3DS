// Host-only bridge to the existing recovered retail light-texture generator.
#include <mkw/menu_lighting.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
int main(int argc,char** argv){
    if(argc!=11)return 2;
    float view[3][3];
    for(unsigned i=0;i<9;++i)view[i/3][i%3]=std::strtof(argv[2+i],nullptr);
    const auto& source=mkw::MenuLightings[0];
    mkw::Vec3 directions[16];
    for(unsigned i=0;i<16;++i)directions[i]=mkw::retailLightViewDirection(source.lights[i],view);
    std::ofstream out(std::string(argv[1])+"/view_light_directions.f32",std::ios::binary);
    out.write(reinterpret_cast<const char*>(directions),sizeof directions);
    for(unsigned which=0;which<2;++which){
        uint8_t rgba[32*32*4];mkw::retailLightTexture(source,which,directions,rgba);
        std::ofstream tex(std::string(argv[1])+"/lm_"+std::to_string(which)+".rgba",std::ios::binary);
        tex.write(reinterpret_cast<const char*>(rgba),sizeof rgba);
    }
}
