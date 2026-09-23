#include "brute_ai.hpp"
#include <algorithm>

namespace d2x {
float bruteWalkMultiplier(const Enemy &enemy) {
    if (enemy.maxHp <= 0) return 1.f;
    const int lifePercent = std::clamp(int(enemy.hp * 100.f / enemy.maxHp), 40, 100);
    return 1.f + float(100 - lifePercent) / 100.f;
}
} // namespace d2x
