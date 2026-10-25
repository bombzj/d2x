#pragma once
#include "core/math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace d2x::pd2 {
inline bool itemCastSafeZone(int level,Vec position) {
    // Current core 0x37B650: unsigned rectangle tests, independent of DT1.
    const auto x=uint32_t(int32_t(std::floor(position.x))),y=uint32_t(int32_t(std::floor(position.y)));
    return (level==157 && x-0x50b0u<=0x2au && y-0x281cu<=0x36u) ||
        (level==159 && x-0x4ff9u<=0x2cu && y-0x2d79u<=0x1fu);
}
inline std::optional<int> streamCastInterval(int unitClass,int fasterCast) {
    // Core 0x12FE80 / 0x1303B0 initialize these algorithm tables, not MPQ
    // animation timings. 0x37BB90 subtracts one frame per passed threshold.
    static constexpr std::array amazon{7,14,22,32,48,68,99,152};
    static constexpr std::array sorceress{9,20,37,63,105,200};
    static constexpr std::array necromancer{9,18,30,48,75,125};
    static constexpr std::array assassin{8,16,27,42,65,102,174};
    static constexpr std::array druid{4,10,19,30,46,68,99,163};
    static constexpr std::array wolf{9,20,42,86,280};
    static constexpr std::array bear{5,10,16,24,37,54,86,152,360};
    static constexpr std::array vampire{7,15,23,35,52,78,117,194};
    std::span<const int> thresholds;int frames{};
    switch(unitClass) {
    case 0:thresholds=amazon;frames=19;break;
    case 1:case 4:thresholds=sorceress;frames=13;break;
    case 2:case 3:thresholds=necromancer;frames=15;break;
    case 5:thresholds=druid;frames=18;break;
    case 6:thresholds=assassin;frames=16;break;
    case 139:thresholds=wolf;frames=16;break;
    case 140:thresholds=bear;frames=16;break;
    case 143:thresholds=vampire;frames=19;break;
    default:return {};
    }
    return frames-int(std::upper_bound(thresholds.begin(),thresholds.end(),fasterCast)-thresholds.begin());
}
}
