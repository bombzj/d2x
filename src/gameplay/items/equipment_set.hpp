#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <algorithm>

namespace d2x {
// Immutable definition facts; indices identify content property instructions.
struct EquipmentSetBonus {
    int pieces = 0;
    bool perItem = false, fixedValue = false;
    size_t instruction = 0;
};
struct EquipmentSetPiece {
    int32_t row = -1;
    std::string set;
    int addFunction = 0, fullPieces = 0;
    size_t instruction = 0;
    std::vector<EquipmentSetBonus> bonuses;
};
// D2Common Items::sub_6FDA4380: addfunc=1 links a layer to a
// particular other piece; addfunc=2 links it to the distinct-piece count.
inline std::array<bool,5> activeSetItemLayers(int function,int32_t own,
    std::span<const int32_t> members,std::span<const int32_t> worn) {
    std::array<bool,5> result{};
    if(function==2) {
        for(size_t i=0;i<result.size();++i) result[i]=worn.size()>i+1;
    } else if(function==1) {
        size_t layer=0;
        for(const auto member:members) if(member!=own && layer<result.size())
            result[layer++]=std::find(worn.begin(),worn.end(),member)!=worn.end();
    }
    return result;
}
} // namespace d2x
