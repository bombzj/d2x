#pragma once
#include "gameplay/items/state.hpp"
#include <string>

namespace d2x {
inline std::string itemArtKey(const ItemInstance &item) {
    std::string key = item.definition;
    if (item.nativeHasGraphic) key += ":gfx:" + std::to_string(item.nativeGraphic);
    if (item.specialRow >= 0)
        key += "#" + std::to_string(int(item.quality)) + ":" + std::to_string(item.specialRow) +
               (item.identified ? ":identified" : ":unidentified");
    return key;
}
} // namespace d2x
