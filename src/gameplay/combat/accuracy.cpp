#include "accuracy.hpp"
#include <algorithm>
#include <cstdint>

namespace d2x {
int physicalHitChance(int attackerLevel, int attackRating, int defenderLevel, int defense) {
    if (attackerLevel <= 0 || defenderLevel <= 0) return 5;
    auto rating = std::max<int64_t>(0, attackRating);
    auto divisor = rating + std::max<int64_t>(0, defense);
    auto factor = divisor ? int64_t(100) * rating / divisor : 100;
    return int(std::clamp(int64_t(2) * attackerLevel * factor /
                              (int64_t(attackerLevel) + defenderLevel), int64_t(5), int64_t(95)));
}
} // namespace d2x
