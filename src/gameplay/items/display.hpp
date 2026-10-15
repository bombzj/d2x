#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace d2x {
enum class ItemTextTone { Normal, Name, RunewordName, SocketedName, Property, Error, SetProperty, RuneName };
struct ItemTextLine {
    std::string text;
    ItemTextTone tone = ItemTextTone::Normal;
    // Native base-stat lines keep the label white and color only the improved value.
    std::size_t propertyOffset = std::string::npos;
};
struct ItemDisplay {
    std::string name;
    std::vector<ItemTextLine> tooltip;
    bool requirementsMet = true;
};
} // namespace d2x
