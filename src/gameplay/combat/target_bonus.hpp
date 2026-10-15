#pragma once
#include "gameplay/stats/stat_list.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <span>

namespace d2x {
// SUNITDMG copies at most 128 parameterized ATTACK/DAMAGE_VS_MONTYPE
// stats, then checks each layer against the prepared monster ancestry.
inline int32_t nativeMonsterTypeBonus(const StatSnapshot &stats,uint16_t stat,std::span<const uint16_t> types) {
    int32_t result{};size_t count{};
    for(const auto &[key,value]:stats.values) if(key.id==stat && count++<128 && key.layer &&
        std::binary_search(types.begin(),types.end(),key.layer)) result=nativeAdd(result,value);
    return result;
}
}
