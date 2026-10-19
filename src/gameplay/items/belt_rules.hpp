#pragma once
#include <string_view>

namespace d2x {
// ITEMS_ComparePotionTypes uses these native code families, not ItemTypes
// inheritance: rpot inherits both hpot and mpot but must occupy its own column.
constexpr std::string_view beltPotionFamily(std::string_view code) {
    if (code.size() == 3 && code[2] >= '1' && code[2] <= '5') {
        if (code.starts_with("hp")) return "hpot";
        if (code.starts_with("mp")) return "mpot";
    }
    if (code == "rvs" || code == "rvl") return "rpot";
    return {};
}
}
