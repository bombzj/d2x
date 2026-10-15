#include "damage_curve.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
int64_t skillLevelBonus(int rank, const std::array<int, 5> &steps) {
    int32_t result = 0;
    for (int level = 2; level <= rank; ++level)
        result = nativeAdd(result, steps[level <= 8 ? 0 : level <= 16 ? 1 : level <= 22 ? 2 : level <= 28 ? 3 : 4]);
    return result;
}
int64_t skillManaCostFixed(int base, int perLevel, int shift, int rank) {
    if (rank < 1 || rank > 255 || shift < 0 || shift > 15)
        throw std::invalid_argument("Unsupported original mana curve");
    return nativeShiftLeft(nativeAdd(base, nativeMultiply(rank - 1, perLevel)), unsigned(shift));
}
int64_t skillDurationBonus(int rank, const std::array<int, 3> &steps) {
    if (rank <= 1) return 0;
    return int64_t(std::min(rank - 1, 7)) * steps[0] +
        int64_t(std::clamp(rank - 8, 0, 8)) * steps[1] + int64_t(std::max(rank - 16, 0)) * steps[2];
}
int64_t skillElementalLength(int base, const std::array<int, 3> &steps, int rank, int64_t bonusPercent) {
    if (rank < 1 || rank > 255) throw std::invalid_argument("Unsupported original duration rank");
    const auto frames = nativeAdd(base, nativeNarrow(skillDurationBonus(rank,steps)));
    return nativeAdd(frames, nativeMultiplyDivide(frames, nativeChecked(bonusPercent), 100));
}
int32_t evaluateSkillDamageFixed(const SkillDamageCurve &curve, int rank, int hitShift,
                                int64_t bonusPercent, int masteryPercent) {
    if (rank < 1 || rank > 255 || hitShift < 0 || hitShift > 15)
        throw std::runtime_error("Unsupported original skill rank or shift");
    const auto value = nativeShiftLeft(nativeAdd(curve.base, nativeNarrow(skillLevelBonus(rank, curve.perLevel))), unsigned(hitShift));
    const auto scaled = nativeAdd(value, nativeMultiplyDivide(value, nativeChecked(bonusPercent - 100), 100));
    return nativeAdd(scaled, nativePercentage(scaled, masteryPercent, 100));
}
int32_t evaluateSkillMinimumDamageFixed(const SkillDamageCurve &curve, int rank, int hitShift,
                                       int64_t bonusPercent, int masteryPercent) {
    const auto unmodified = evaluateSkillDamageFixed(curve, rank, hitShift);
    if (unmodified <= 256 && !curve.perLevel[0]) bonusPercent = 100;
    return evaluateSkillDamageFixed(curve, rank, hitShift, bonusPercent, masteryPercent);
}
int32_t evaluatePhysicalSkillDamageFixed(const SkillDamageCurve &curve, int rank, int hitShift,
                                        int64_t bonusPercent) {
    if (rank < 1 || rank > 255 || hitShift < 0 || hitShift > 15)
        throw std::invalid_argument("Unsupported original physical skill curve");
    const auto base = nativeAdd(curve.base, nativeNarrow(skillLevelBonus(rank, curve.perLevel)));
    const auto scaled = nativeAdd(base, nativeMultiplyDivide(base, nativeChecked(bonusPercent - 100), 100));
    return nativeShiftLeft(scaled, unsigned(hitShift));
}
int32_t evaluateMissileDamageFixed(const SkillDamageCurve &curve, int rank, int hitShift,
                                  int64_t bonusPercent, int masteryPercent) {
    if (rank<1 || rank>255) throw std::invalid_argument("Unsupported original missile damage rank");
    const auto shifted = evaluatePhysicalSkillDamageFixed(curve, rank, hitShift, bonusPercent);
    return nativeAdd(shifted, nativePercentage(shifted, masteryPercent, 100));
}
float evaluateSkillDamage(const SkillDamageCurve &curve, int rank, int hitShift,
                          int64_t bonusPercent, int masteryPercent) {
    return float(evaluateSkillDamageFixed(curve, rank, hitShift, bonusPercent, masteryPercent)) / 256.f;
}
float evaluateSkillMinimumDamage(const SkillDamageCurve &curve, int rank, int hitShift,
                                 int64_t bonusPercent, int masteryPercent) {
    return float(evaluateSkillMinimumDamageFixed(curve,rank,hitShift,bonusPercent,masteryPercent)) / 256.f;
}
} // namespace d2x
