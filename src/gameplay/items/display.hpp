#pragma once
#include <string>
#include <vector>

namespace d2x {
enum class ItemTextTone { Normal, Name, RunewordName, SocketedName, Property, Error };
struct ItemTextLine {
    std::string text;
    ItemTextTone tone = ItemTextTone::Normal;
};
struct ItemDisplay {
    std::string name;
    std::vector<ItemTextLine> tooltip;
};
} // namespace d2x
