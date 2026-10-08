#include "damage_curve.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
int64_t skillLevelBonus(int rank, const std::array<int, 5> &steps) {
    int64_t result = 0;
    for (int level = 2; level <= rank; ++level)
        result += steps[level <= 8 ? 0 : level <= 16 ? 1 : level <= 22 ? 2 : level <= 28 ? 3 : 4];
    return result;
}
int64_t skillManaCostFixed(int base, int perLevel, int shift, int rank) {
    if (rank < 1 || rank > 255 || shift < 0 || shift > 15)
        throw std::invalid_argument("Unsupported original mana curve");
    return (int64_t(base) + int64_t(rank - 1) * perLevel) * (int64_t(1) << shift);
}
int64_t skillElementalLength(int base, const std::array<int, 3> &steps, int rank, int64_t bonusPercent) {
    if (rank < 1 || rank > 255) throw std::invalid_argument("Unsupported original duration rank");
    const int64_t frames = int64_t(base) + int64_t(std::min(rank - 1, 7)) * steps[0] +
        int64_t(std::clamp(rank - 8, 0, 8)) * steps[1] + int64_t(std::max(rank - 16, 0)) * steps[2];
    return frames + frames * bonusPercent / 100;
}
int32_t evaluateSkillDamageFixed(const SkillDamageCurve &curve, int rank, int hitShift,
                                int64_t bonusPercent, int masteryPercent) {
    if (rank < 1 || rank > 255 || hitShift < 0 || hitShift > 15)
        throw std::runtime_error("Unsupported original skill rank or shift");
    const int64_t value = (int64_t(curve.base) + skillLevelBonus(rank, curve.perLevel)) * (int64_t(1) << hitShift);
    const int64_t scaled = value + value * (bonusPercent-100) / 100;
    const int64_t mastered = scaled + scaled * masteryPercent / 100;
    if (mastered < 0 || mastered > std::numeric_limits<int32_t>::max())
        throw std::runtime_error("Original skill damage exceeds supported range");
    return int32_t(mastered);
}
int32_t evaluateMissileDamageFixed(const SkillDamageCurve &curve, int rank, int hitShift,
                                  int64_t bonusPercent, int masteryPercent) {
    if (rank<1 || rank>255) throw std::invalid_argument("Unsupported original missile damage rank");
    const int64_t base=int64_t(curve.base)+skillLevelBonus(rank,curve.perLevel);
    const int64_t scaled=base+base*(bonusPercent-100)/100;
    if (scaled<0 || scaled>INT32_MAX) throw std::runtime_error("Original missile damage exceeds supported range");
    return evaluateSkillDamageFixed({int(scaled),{}},1,hitShift,100,masteryPercent);
}
float evaluateSkillDamage(const SkillDamageCurve &curve, int rank, int hitShift,
                          int64_t bonusPercent, int masteryPercent) {
    return float(evaluateSkillDamageFixed(curve, rank, hitShift, bonusPercent, masteryPercent)) / 256.f;
}
float evaluateSkillMinimumDamage(const SkillDamageCurve &curve, int rank, int hitShift,
                                 int64_t bonusPercent, int masteryPercent) {
    const auto unmodified=evaluateSkillDamageFixed(curve,rank,hitShift);
    if (unmodified<=256 && !curve.perLevel[0]) bonusPercent=100;
    return evaluateSkillDamage(curve,rank,hitShift,bonusPercent,masteryPercent);
}
} // namespace d2x
