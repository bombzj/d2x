#include "summon_resolve.hpp"
#include "damage_curve.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace d2x {
SummonCastSpec resolveSummon(const SummonSkillSpec &spec, int rank, int mastery, int resist,
                            int ownerLevel, int difficulty) {
    if (rank <= 0 || rank > 255 || difficulty < 0 || difficulty > 2 || ownerLevel < 1)
        throw std::runtime_error("Invalid summon level or difficulty");
    SummonCastSpec result;
    result.monster = spec.monster; result.kind = spec.kind;
    result.limit = rank < 4 ? rank : 2 + rank / 3;
    result.shieldChance = rank > 2 ? spec.shieldChance : 0;
    result.shieldVariants = spec.shieldVariants;
    result.stats = spec.base[difficulty];
    auto &stats = result.stats;
    auto &attributes = stats.attributes;
    stats.level = std::clamp(rank + 3 * ownerLevel / 4, 1, ownerLevel);
    const auto level = std::min(size_t(stats.level), spec.levelDefense.size() - 1);
    attributes.maxLife = int((int64_t(attributes.maxLife) + int64_t(mastery) * spec.masteryLife) *
        (100 + int64_t(std::max(0, rank - 3)) * spec.lifePerRank) / 100);
    attributes.attackRating += spec.levelAttack.at(level)[difficulty] + (rank + mastery) * spec.attackPerRank;
    attributes.defense += spec.levelDefense.at(level)[difficulty] + (rank + mastery) * spec.defensePerRank;
    const int64_t damage = int64_t(mastery) * spec.masteryDamage + skillLevelBonus(rank, spec.damageSteps);
    const int percent = 100 + std::max(0, rank - 3) * spec.damagePerRank;
    stats.minimumDamage = float((int64_t(stats.minimumDamage * 256.f) + damage * 256) * percent / 100) / 256.f;
    stats.maximumDamage = float((int64_t(stats.maximumDamage * 256.f) + damage * 256) * percent / 100) / 256.f;
    if (resist > 0) {
        const int bonus = std::min(spec.resistMaximum, spec.resistMinimum +
            (spec.resistMaximum - spec.resistMinimum) * (110 * resist / (resist + 6)) / 100);
        attributes.fireResist += bonus; attributes.coldResist += bonus;
        attributes.lightningResist += bonus; attributes.poisonResist += bonus;
    }
    return result;
}
} // namespace d2x
