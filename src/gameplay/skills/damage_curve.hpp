#pragma once
#include <array>
#include <cstdint>

namespace d2x {
struct SkillDamageCurve {
    int base = 0;
    std::array<int, 5> perLevel{};
};
int64_t skillLevelBonus(int rank, const std::array<int, 5> &steps);
// D2Common SKILLS_GetManaCosts / GetElementalLength. Values are native 8-bit fixed mana and 25 Hz frames.
int64_t skillManaCostFixed(int base, int perLevel, int shift, int rank);
int64_t skillElementalLength(int base, const std::array<int, 3> &steps, int rank, int64_t bonusPercent = 0);
int32_t evaluateSkillDamageFixed(const SkillDamageCurve &curve, int rank, int hitShift,
                                int64_t bonusPercent = 100, int masteryPercent = 0);
// MISSILE_GetMin/MaxElemDamage applies missile synergy before HitShift; mastery follows the shift.
int32_t evaluateMissileDamageFixed(const SkillDamageCurve &curve, int rank, int hitShift,
                                  int64_t bonusPercent = 100, int masteryPercent = 0);
float evaluateSkillDamage(const SkillDamageCurve &curve, int rank, int hitShift,
                          int64_t bonusPercent = 100, int masteryPercent = 0);
// SKILLS_GetMinElemDamage skips synergy for a fixed <=1 damage minimum with no first-tier growth.
float evaluateSkillMinimumDamage(const SkillDamageCurve &curve, int rank, int hitShift,
                                 int64_t bonusPercent = 100, int masteryPercent = 0);
} // namespace d2x
