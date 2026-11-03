#include <initializer_list>
#include <mkw/flow_text.hpp>
#include <cassert>
int main(){
 const char* p="RANK: \xef\x81\xa3";
 for(char c: {'R','A','N','K',':',' '})assert(mkw::nextFlowGlyph(p)==c);
 assert(mkw::nextFlowGlyph(p)==0xf063&&!*p);
 p="\xe3\x81";assert(mkw::nextFlowGlyph(p)==0xfffd&&!*p);
 p="\xc0X";assert(mkw::nextFlowGlyph(p)==0xfffd&&mkw::nextFlowGlyph(p)=='X');
 p="\xf0\x9f\x98\x80" "A";assert(mkw::nextFlowGlyph(p)==0xfffd&&mkw::nextFlowGlyph(p)=='A');
}
