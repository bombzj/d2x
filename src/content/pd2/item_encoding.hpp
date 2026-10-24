#pragma once
#include <string_view>

namespace d2x {
struct ItemSaveEncoding {
    unsigned statBits = 9;
    std::string_view bits = "Save Bits", add = "Save Add", params = "Save Param Bits";
};
// ProjectDiablo complete item reader/writer, RVA 0x382E40 / 0x381AA0
// (catalog build 9c2b4d46). Compact items keep the original current columns.
inline ItemSaveEncoding itemSaveEncoding(bool pd2, unsigned format, bool compact) {
    ItemSaveEncoding result;
    if(!pd2) return result;
    result.statBits=format>=102?10:9;
    if(!compact && format<103)
        return {result.statBits,"Save Bits S12","Save Add S12","Save Param Bits S12"};
    if(!compact && format==103)
        return {result.statBits,"Save Bits S13","Save Adds S13","Save Param Bits S13"};
    return result;
}
}
