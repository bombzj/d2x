#include "passive.hpp"

namespace d2x {
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
