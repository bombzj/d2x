#include "original.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
int64_t levelBonus(int rank, const std::array<int, 5> &steps) {
    int64_t result = 0;
    for (int level = 2; level <= rank; ++level)
        result += steps[level <= 8 ? 0 : level <= 16 ? 1 : level <= 22 ? 2 : level <= 28 ? 3 : 4];
    return result;
}
} // namespace
OriginalSkillCast resolveOriginalSkill(const OriginalSkillSpec &spec, int rank,
                                       const std::map<int, int> &learned, int fireMasteryPercent,
                                       int lightningMasteryPercent) {
    if (rank < 1 || rank > 255 || spec.manaShift < 0 || spec.manaShift > 15 ||
        spec.hitShift < 0 || spec.hitShift > 15)
        throw std::runtime_error("Unsupported original skill rank or shift");
    OriginalSkillCast result;
    result.effect = spec.effect;
    const int64_t scaledMana = std::max<int64_t>(0,
        int64_t(spec.mana) + int64_t(rank - 1) * spec.manaPerLevel) << spec.manaShift;
    const int64_t fixedMana = std::max<int64_t>(int64_t(spec.minimumMana) * 256, scaledMana);
    result.manaCost = float(fixedMana) / 256.f;
    int64_t synergy = 0;
    for (int id : spec.synergySkills)
        if (auto found = learned.find(id); found != learned.end()) synergy += found->second;
    const int64_t bonus = std::max<int64_t>(0, 100 + synergy * spec.synergyPercent);
    auto damage = [&](int base, const std::array<int, 5> &steps) {
        const int64_t value = (int64_t(base) + levelBonus(rank, steps)) << spec.hitShift;
        const int64_t scaled = value * bonus / 100;
        const int mastery = spec.fireDamage ? fireMasteryPercent :
            spec.lightningDamage ? lightningMasteryPercent : 0;
        const int64_t mastered = scaled + scaled * mastery / 100;
        if (mastered < 0 || mastered > std::numeric_limits<int32_t>::max())
            throw std::runtime_error("Original skill damage exceeds supported range");
        return float(mastered) / 256.f;
    };
    result.minimumDamage = damage(spec.minimumDamage, spec.minimumPerLevel);
    result.maximumDamage = damage(spec.maximumDamage, spec.maximumPerLevel);
    const int64_t coldFrames = int64_t(spec.coldFrames) +
        int64_t(std::min(rank - 1, 7)) * spec.coldFramesPerLevel[0] +
        int64_t(std::clamp(rank - 8, 0, 8)) * spec.coldFramesPerLevel[1] +
        int64_t(std::max(rank - 16, 0)) * spec.coldFramesPerLevel[2];
    result.coldDuration = float(coldFrames) / 25.f;
    result.missileId = spec.missileId;
    result.missileNextDelay = float(spec.missileNextDelay) / 25.f;
    result.staticPercent = float(spec.staticPercent);
    result.staticRadius = float(spec.staticRange + (rank - 1) * spec.staticRangePerLevel);
    result.visualDuration = float(spec.visualFrames) / 25.f;
    result.missileVelocity = spec.missileVelocity;
    result.missileLifetime = spec.missileLifetime;
    result.impactRadius = spec.impactRadius;
    return result;
}
} // namespace d2x
