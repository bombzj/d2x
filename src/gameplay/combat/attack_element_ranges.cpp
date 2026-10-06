#include "gameplay/combat/stat_modifiers.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace d2x {
namespace {
AttackDamageRange boundedRange(int low, int high, int ownLow, int ownHigh) {
    if (int64_t(high) + ownHigh <= 0) return {};
    const auto minimum = std::clamp<int64_t>(int64_t(low) + ownLow, 0,
                                              std::numeric_limits<int>::max());
    const auto maximum = std::clamp<int64_t>(int64_t(high) + ownHigh, minimum,
                                              std::numeric_limits<int>::max());
    return {int(minimum), int(maximum)};
}
} // namespace
AttackElementRanges attackElementRanges(const CombatModifiers &combat, EntityId weapon) {
    WeaponModifiers own;
    if (auto found = combat.weapons.find(weapon); found != combat.weapons.end()) own = found->second;
    return {
        boundedRange(combat.fireMinimum, combat.fireMaximum, own.fireMinimum, own.fireMaximum),
        boundedRange(combat.lightningMinimum, combat.lightningMaximum,
                     own.lightningMinimum, own.lightningMaximum),
        boundedRange(combat.coldMinimum, combat.coldMaximum, own.coldMinimum, own.coldMaximum),
        boundedRange(combat.magicMinimum, combat.magicMaximum, own.magicMinimum, own.magicMaximum)
    };
}
} // namespace d2x
