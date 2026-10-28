#pragma once
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cstdint>
#include <string_view>

namespace d2x {
struct ItemSaveEncoding {
    unsigned statBits = 9;
    std::string_view bits = "Save Bits", add = "Save Add", params = "Save Param Bits";
};
// 41d49 385D40 adds SaveAdd with x86 arithmetic; 385C60 then saturates
// fields below 32 bits. A full DWORD keeps its original bit pattern.
inline uint32_t pd2ItemSavedValue(int64_t value,int add,unsigned bits) {
    const auto encoded=nativeAdd(nativeNarrow(value),add);
    if(bits==32) return uint32_t(encoded);
    return uint32_t(std::clamp<int64_t>(encoded,0,(uint32_t{1}<<bits)-1));
}
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
