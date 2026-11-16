#pragma once
#include <mkw/menu_assets.hpp>
#include <mkw/menu_name_glyph_atlas.hpp>
#include <iterator>
#include <string_view>
namespace mkw {
// Name glyphs (UiSprites "name_glyph_*") share LA4 atlas pages, which are
// UiSprites rows of their own ("glyph_atlas_*"); the page row is what loads
// and stays resident. A glyph's u/v is its extent in the page.
struct UiGlyphAtlas {unsigned first=0,count=0,pages[std::size(UiGlyphPageKeys)]{};bool valid=false;};
consteval UiGlyphAtlas makeUiGlyphAtlas(){
 UiGlyphAtlas a{};a.first=~0u;
 for(unsigned i=0;i<std::size(UiSprites);++i){const std::string_view key=UiSprites[i].key;
  if(a.first==~0u&&key.starts_with("name_glyph_"))a.first=i;
  for(unsigned p=0;p<std::size(UiGlyphPageKeys);++p)if(key==UiGlyphPageKeys[p])a.pages[p]=i+1;}
 a.count=std::size(UiGlyphPlaces);
 if(a.first==~0u||a.first+a.count>std::size(UiSprites))return a;
 for(unsigned i=0;i<a.count;++i)if(!std::string_view(UiSprites[a.first+i].key).starts_with("name_glyph_"))return a;
 for(auto& p:a.pages){if(!p)return a;--p;}
 a.valid=true;return a;
}
inline constexpr UiGlyphAtlas UiGlyphAtlasRows=makeUiGlyphAtlas();
static_assert(UiGlyphAtlasRows.valid,"UiSprites needs every glyph_atlas_* page row and the name_glyph_* rows together, as cook_menu_names.py writes them");
// The UiSprites row whose texture draws this sprite.
constexpr unsigned uiTextureSprite(unsigned sprite){
 const unsigned n=sprite-UiGlyphAtlasRows.first;
 return n<UiGlyphAtlasRows.count?UiGlyphAtlasRows.pages[UiGlyphPlaces[n].page]:sprite;
}
// A name glyph's texel origin in its page; {0,0} for every other sprite.
struct UiTexelOrigin {unsigned x,y;};
constexpr UiTexelOrigin uiTexelOrigin(unsigned sprite){
 const unsigned n=sprite-UiGlyphAtlasRows.first;
 return n<UiGlyphAtlasRows.count?UiTexelOrigin{UiGlyphPlaces[n].x,UiGlyphPlaces[n].y}:UiTexelOrigin{0,0};
}
}
