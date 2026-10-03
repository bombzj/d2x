#include "damage_curve.hpp"
#include <limits>
#include <stdexcept>

namespace d2x {
int64_t skillLevelBonus(int rank, const std::array<int, 5> &steps) {
    int64_t result = 0;
    for (int level = 2; level <= rank; ++level)
        result += steps[level <= 8 ? 0 : level <= 16 ? 1 : level <= 22 ? 2 : level <= 28 ? 3 : 4];
    return result;
}
float evaluateSkillDamage(const SkillDamageCurve &curve, int rank, int hitShift,
                          int64_t bonusPercent, int masteryPercent) {
    if (rank < 1 || rank > 255 || hitShift < 0 || hitShift > 15)
        throw std::runtime_error("Unsupported original skill rank or shift");
    const int64_t value = (int64_t(curve.base) + skillLevelBonus(rank, curve.perLevel)) << hitShift;
    const int64_t scaled = value * bonusPercent / 100;
    const int64_t mastered = scaled + scaled * masteryPercent / 100;
    if (mastered < 0 || mastered > std::numeric_limits<int32_t>::max())
        throw std::runtime_error("Original skill damage exceeds supported range");
    return float(mastered) / 256.f;
}
} // namespace d2x
