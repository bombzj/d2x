#pragma once
#include "exploration_memory.hpp"
#include "contracts/online_world.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::debug_ai {
inline Vec position(OnlinePoint p) {return {float(p.x),float(p.y)};}
inline OnlinePoint point(Vec p) {
    if(!std::isfinite(p.x) || !std::isfinite(p.y) || p.x<0 || p.y<0 || p.x>65535 || p.y>65535)
        throw std::runtime_error("Combat position outside native coordinates");
    const auto feet=DebugExplorationMemory::playerCell(p);
    return {uint16_t(feet.first),uint16_t(feet.second)};
}
inline std::optional<uint8_t> life(const OnlineUnit &unit) {
    if(!unit.lifePercent) return {};
    // Normal ratios allow 128; hit packets use the high bit as a trigger flag.
    return uint8_t(std::min(127,int(unit.lifeCarriesTriggerFlag?*unit.lifePercent&0x7f:*unit.lifePercent)));
}
} // namespace d2x::debug_ai
