#include "passive.hpp"
#include "amazon_passive_spec.hpp"
#include <algorithm>

namespace d2x {
int amazonPassiveValue(const AmazonPassiveSpec &spec, int rank) {
    if (rank <= 0) return 0;
    if (!spec.diminishing) return spec.minimum + (rank - 1) * spec.perLevel;
    const int ratio = 110 * rank / (rank + 6);
    return std::min(spec.maximum, spec.minimum + (spec.maximum - spec.minimum) * ratio / 100);
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
