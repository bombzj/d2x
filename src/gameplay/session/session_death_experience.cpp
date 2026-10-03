#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/rewards/experience.hpp"
#include "content/monsters/monster_experience.hpp"
#include <iostream>

namespace d2x {
void GameSessionImpl::awardDeathExperience(const EnemyDied &death, EntityId beneficiary) {
    // This local adapter binds one registered player. Future hosts must resolve
    // the explicit beneficiary in their actor repository, never use a global current player.
    if (!beneficiary || beneficiary != state().player.id || death.killer != beneficiary) return;
    grantHirelingExperience(death);
    const auto experience = resolveMonsterExperience(content_, monsterContent_, worldContent_,
        {death.identity, death.region, death.difficulty, state().player.character.level, death.rewardModifiers});
    if (experience.deferred.empty())
        grantExperience(playerExperienceGain(experience.amount, characterStats().combat.experiencePercent));
    else std::cout << "Monster experience deferred: id=" << death.victim.value
                   << " reason=" << experience.deferred << '\n';
}
void GameSessionImpl::grantHirelingExperience(const EnemyDied &death) {
    auto &hireling = simulation_->state_.player.hireling;
    const auto *definition = hirelingDefinition();
    if (!definition || !hirelingCanGainExperience(hireling.active(), hireling.level, state().player.character.level)) return;
    const auto award = resolveMonsterExperience(content_, monsterContent_, worldContent_,
        {death.identity, death.region, death.difficulty, hireling.level, death.rewardModifiers});
    if (!award.deferred.empty()) return;
    const auto stats = deriveHirelingStats(*definition, hireling.level);
    const auto plan = planHirelingExperience(award.amount, {hireling.experience,
        stats.experience, stats.nextExperience, hireling.level, state().player.character.level,
        hirelingStats().combat.experiencePercent, hireling.active(), death.attacker == hireling.id});
    hireling.experience = plan.experience;
    hireling.level = plan.level;
    if (!plan.leveled) return;
    for (const auto &entry : content_.hirelings)
        if (entry.id == definition->id && entry.difficulty == definition->difficulty &&
            entry.level <= hireling.level && entry.level > definition->level) definition = &entry;
    hireling.sourceRow = definition->sourceRow;
    hireling.hp = float(hirelingStats().base.life);
}
} // namespace d2x
