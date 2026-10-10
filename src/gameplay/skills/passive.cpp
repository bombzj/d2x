#include "passive.hpp"
#include "amazon_passive_spec.hpp"
#include "barbarian_passive_spec.hpp"
#include "rank_bonus.hpp"
#include <algorithm>

namespace d2x {
void applyBarbarianPassive(CharacterModifiers &modifiers, const BarbarianPassiveSpec &spec, int rank) {
    if (rank <= 0) return;
    const auto &p = spec.parameters;
    if (spec.kind == BarbarianPassiveSpec::Kind::Mastery) {
        auto &masteries = spec.thrown ? modifiers.combat.throwMasteries : modifiers.combat.meleeMasteries;
        auto &bonus = masteries[spec.weaponType];
        bonus[0] = std::max(bonus[0], skillRankBonus({p[0], p[1]}, rank));
        bonus[1] = std::max(bonus[1], skillRankBonus({p[2], p[3]}, rank));
        bonus[2] = std::max(bonus[2], barbarianDiminishing(p[4], p[5], rank));
    } else if(spec.kind==BarbarianPassiveSpec::Kind::Stamina) {
        modifiers.staminaPercent+=skillRankBonus({p[0],p[1]},rank);
    } else if(spec.kind==BarbarianPassiveSpec::Kind::Defense) {
        modifiers.combat.defensePercent+=skillRankBonus({p[0],p[1]},rank);
    } else if(spec.kind==BarbarianPassiveSpec::Kind::Speed) {
        modifiers.velocityPercent+=barbarianDiminishing(p[0],p[1],rank);
    } else if(spec.kind==BarbarianPassiveSpec::Kind::Resistance) {
        const int amount=barbarianDiminishing(p[0],p[1],rank);
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
