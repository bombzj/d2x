#include "passive_data.hpp"
#include "skill_data.hpp"

namespace d2x {
void applySkillPassives(CharacterModifiers &modifiers, const SkillCatalog &skills,
    const std::map<int, int> &ranks, const CombatEffectSet &effects, EffectFrame frame) {
    for (const auto &[id, skill] : skills.skills) {
        const auto rank = ranks.find(id);
        if (rank == ranks.end()) continue;
        applySkillPassive(modifiers, skill.passiveContribution, rank->second,
            effects.hasState(skill.passiveSuppressedByState, frame));
    }
}
} // namespace d2x
