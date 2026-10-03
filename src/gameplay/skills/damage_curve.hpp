#pragma once
#include <array>
#include <cstdint>

namespace d2x {
struct SkillDamageCurve {
    int base = 0;
    std::array<int, 5> perLevel{};
};
int64_t skillLevelBonus(int rank, const std::array<int, 5> &steps);
float evaluateSkillDamage(const SkillDamageCurve &curve, int rank, int hitShift,
                          int64_t bonusPercent = 100, int masteryPercent = 0);
} // namespace d2x
