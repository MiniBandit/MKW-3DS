#pragma once
#include <string>
#include <vector>

namespace mkw {
struct MenuBottomLayout;
// The staff credits' text: Mario Kart Wii's own staff roll
// (ui-data/menuother_u/message_staffroll.bmg), read at runtime and laid out as
// lines that scroll up the lower screen while the credits movie plays on top.
class StaffRoll {
public:
    bool load(const char* path="ui-data/menuother_u/message_staffroll.bmg");
    unsigned lineCount() const { return unsigned(lines_.size()); }
    // Lines whose positions fall on the 320x240 lower screen, as labels, after
    // `pixels` of scrolling (a heading in yellow, names in white).
    void layout(float pixels,MenuBottomLayout& out) const;
    // Pixels from the first line entering at the bottom to the last leaving at the top.
    float scrollLength() const;
private:
    struct Line { std::string text; bool heading; };
    std::vector<Line> lines_;
};
}
