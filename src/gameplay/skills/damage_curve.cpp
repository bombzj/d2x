#include "damage_curve.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
int32_t skillManaBeforeShift(int base,int perLevel,int shift,int rank) {
    if(rank<1 || shift<0 || shift>15) throw std::invalid_argument("Unsupported original mana curve");
    return nativeAdd(base,nativeMultiply(rank-1,perLevel));
}
}
int64_t skillLevelBonus(int rank, const std::array<int, 5> &steps) {
    // D2Common 4DDB0 multiplies each bracket by its number of levels.
    constexpr std::array lower{1,8,16,22,28};
    constexpr std::array upper{8,16,22,28,std::numeric_limits<int>::max()};
    int32_t result = 0;
    for (size_t bracket=0;bracket<steps.size();++bracket) {
        const int levels=std::clamp(rank,lower[bracket],upper[bracket])-lower[bracket];
        result=nativeAdd(result,nativeMultiply(steps[bracket],levels));
    }
    return result;
}
int64_t skillManaCostFixed(int base, int perLevel, int shift, int rank) {
    return nativeShiftLeft(skillManaBeforeShift(base,perLevel,shift,rank),unsigned(shift));
}
int32_t skillManaPerSecond(int base,int perLevel,int shift,int rank) {
    // D2Common 51298 applies 25 / 2 before ManaShift, then SAR 8.
    const auto mana=skillManaBeforeShift(base,perLevel,shift,rank);
    return nativeShiftLeft(nativeDivide(nativeMultiply(mana,25),2),unsigned(shift))>>8;
}
int64_t skillDurationBonus(int rank, const std::array<int, 3> &steps) {
    if (rank <= 1) return 0;
    return int64_t(std::min(rank - 1, 7)) * steps[0] +
        int64_t(std::clamp(rank - 8, 0, 8)) * steps[1] + int64_t(std::max(rank - 16, 0)) * steps[2];
}
int64_t skillElementalLength(int base, const std::array<int, 3> &steps, int rank, int64_t bonusPercent) {
    if (rank < 1 || rank > 255) throw std::invalid_argument("Unsupported original duration rank");
    const auto frames = nativeAdd(base, nativeNarrow(skillDurationBonus(rank,steps)));
    return nativeAdd(frames, nativePercentage(frames, nativeChecked(bonusPercent), 100));
}
int32_t evaluateSkillDamageFixed(const SkillDamageCurve &curve, int rank, int hitShift,
                                int64_t bonusPercent, int masteryPercent) {
    if (rank < 1 || rank > 255 || hitShift < 0 || hitShift > 15)
        throw std::runtime_error("Unsupported original skill rank or shift");
    const auto value = nativeShiftLeft(nativeAdd(curve.base, nativeNarrow(skillLevelBonus(rank, curve.perLevel))), unsigned(hitShift));
    const auto scaled = nativeAdd(value, nativePercentage(value, nativeChecked(bonusPercent - 100), 100));
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
    const auto scaled = nativeAdd(base, nativePercentage(base, nativeChecked(bonusPercent - 100), 100));
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
