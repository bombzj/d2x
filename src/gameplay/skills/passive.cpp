#include "passive.hpp"
#include "amazon_passive_spec.hpp"
#include "barbarian_passive_spec.hpp"
#include "necro_passive_spec.hpp"
#include "rank_bonus.hpp"
#include <algorithm>

namespace d2x {
NecromancerPassiveValues evaluateNecromancerPassive(const NecromancerPassiveSpec &spec, int rank) {
    NecromancerPassiveValues result;
    if (rank <= 0) return result;
    const auto &p = spec.parameters;
    if (spec.kind == NecromancerPassiveSpec::Kind::SkeletonMastery) {
        result.skeletonLife = rank * p[0]; result.skeletonDamage = rank * p[1];
        result.reviveLife = rank * p[2]; result.reviveDamage = rank * p[3];
    } else if (spec.kind == NecromancerPassiveSpec::Kind::GolemMastery) {
        result.golemLife = skillRankBonus({p[0],p[1]},rank);
        result.golemSpeed = skillDiminishingRatioBonus({p[2],p[3]},rank);
        result.golemRating = skillRankBonus({p[4],p[5]},rank);
    } else result.resistance = skillDiminishingRatioBonus({p[0],p[1]},rank);
    return result;
}
BarbarianPassiveValues evaluateBarbarianPassive(const BarbarianPassiveSpec &spec, int rank) {
    BarbarianPassiveValues result;
    if (rank <= 0) return result;
    const auto &p = spec.parameters;
    if (spec.kind == BarbarianPassiveSpec::Kind::Mastery) {
        result.attackRating = skillRankBonus({p[0], p[1]}, rank);
        result.damagePercent = skillRankBonus({p[2], p[3]}, rank);
        result.criticalChance = skillDiminishingRatioBonus({p[4], p[5]}, rank);
    } else if (spec.kind == BarbarianPassiveSpec::Kind::Stamina || spec.kind == BarbarianPassiveSpec::Kind::Defense)
        result.amount = skillRankBonus({p[0], p[1]}, rank);
    else result.amount = skillDiminishingRatioBonus({p[0], p[1]}, rank);
    return result;
}
void applyBarbarianPassive(CharacterModifiers &modifiers, const BarbarianPassiveSpec &spec, int rank) {
    if (rank <= 0) return;
    const auto value = evaluateBarbarianPassive(spec, rank);
    if (spec.kind == BarbarianPassiveSpec::Kind::Mastery) {
        auto &masteries = spec.thrown ? modifiers.combat.throwMasteries : modifiers.combat.meleeMasteries;
        auto &bonus = masteries[spec.weaponType];
        bonus[0] = std::max(bonus[0], value.attackRating);
        bonus[1] = std::max(bonus[1], value.damagePercent);
        bonus[2] = std::max(bonus[2], value.criticalChance);
    } else if(spec.kind==BarbarianPassiveSpec::Kind::Stamina) {
        modifiers.staminaPercent+=value.amount;
    } else if(spec.kind==BarbarianPassiveSpec::Kind::Defense) {
        modifiers.combat.defensePercent+=value.amount;
    } else if(spec.kind==BarbarianPassiveSpec::Kind::Speed) {
        modifiers.velocityPercent+=value.amount;
    } else if(spec.kind==BarbarianPassiveSpec::Kind::Resistance) {
        const int amount=value.amount;
        modifiers.fireResist+=amount;modifiers.lightningResist+=amount;modifiers.coldResist+=amount;modifiers.poisonResist+=amount;
    }
}
int amazonPassiveValue(const AmazonPassiveSpec &spec, int rank) {
    if (rank <= 0) return 0;
    if (!spec.diminishing) return skillRankBonus({spec.minimum, spec.perLevel}, rank);
    return skillDiminishingBonus({spec.minimum, spec.maximum}, rank);
}
void applySkillPassive(CharacterModifiers &modifiers, const SkillPassiveSpec &spec,
                       int baseRank, bool suppressed) {
    if (suppressed) return;
    modifiers.combat.attackRatingPercent += baseRank * spec.attackRatingPerBaseRank;
    const int maximum = baseRank / 2;
    if (spec.maxResistElement == 2) modifiers.combat.fireMaxResist += maximum;
    else if (spec.maxResistElement == 3) modifiers.combat.lightningMaxResist += maximum;
    else if (spec.maxResistElement == 4) modifiers.combat.coldMaxResist += maximum;
}
} // namespace d2x
