#include "relation.hpp"

namespace d2x {
CombatRelation harmfulRelation(const CombatRelationInput &input) {
    if (!input.source || !input.target) return CombatRelation::Unknown;
    if (input.source == input.target) return CombatRelation::Self;
    if (input.bothControlled) {
        if (input.sameController) return CombatRelation::Allied;
        if (input.directedHostility) return CombatRelation::Hostile;
        return input.sameParty ? CombatRelation::Allied : CombatRelation::Neutral;
    }
    if (input.forcedHostility) return CombatRelation::Hostile;
    return sameAlignment(input.sourceAlignment, input.targetAlignment) ? CombatRelation::Allied : CombatRelation::Hostile;
}
bool beneficialRelation(const CombatRelationInput &input) {
    if (!input.source || !input.target) return false;
    if (input.source == input.target) return true;
    if (input.bothControlled) return input.sameController || input.sameParty;
    return sameAlignment(input.sourceAlignment, input.targetAlignment);
}
}