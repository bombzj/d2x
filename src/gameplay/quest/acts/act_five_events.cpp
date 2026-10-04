#include "act_five_state.hpp"
#include "rules.hpp"
namespace d2x {
void actFiveEntry(DifficultyQuests &records, const QuestEntryFacts &facts, std::vector<QuestEntryStep> &result) {
    auto &baal = records[questIndex(QuestId::EveOfDestruction)];
    const uint32_t baalStage = facts.level == 132 ? 4 : facts.level == 131 ? 3 : facts.level >= 128 && facts.level <= 130 ? 2 : 0;
    if (baalStage && baal.stage < baalStage) { baal.stage = baalStage; result.emplace_back(QuestTransition{QuestId::EveOfDestruction, baal}); }
    auto &ancients = records[questIndex(QuestId::RiteOfPassage)];
    if (facts.level == 120 && ancients.stage < 3) {
        ancients.stage = 3; result.emplace_back(QuestTransition{QuestId::RiteOfPassage, ancients});
    } else if (facts.act == 4 && facts.level != 109 && ancients.stage == 1) {
        ancients.stage = 2; result.emplace_back(QuestTransition{QuestId::RiteOfPassage, ancients});
    }
    auto &betrayal = records[questIndex(QuestId::BetrayalOfHarrogath)];
    if (facts.level >= 121 && facts.level <= 124 && betrayal.stage < 2) {
        betrayal.stage = 2; result.emplace_back(QuestTransition{QuestId::BetrayalOfHarrogath, betrayal});
    }
    auto &ice = records[questIndex(QuestId::PrisonOfIce)];
    if (facts.act == 4 && facts.level != 109 && ice.stage == 1) { ice.stage = 2; result.emplace_back(QuestTransition{QuestId::PrisonOfIce, ice}); }
    auto &rescue = records[questIndex(QuestId::RescueOnMountArreat)];
    if (facts.level == 111 && rescue.stage < 2) {
        rescue.stage = 2; result.emplace_back(QuestTransition{QuestId::RescueOnMountArreat, rescue});
    }
    auto &siege = records[questIndex(QuestId::SiegeOnHarrogath)];
    if (facts.act == 4 && facts.level >= 110 && facts.level <= 112 && siege.stage == 1) {
        siege.stage = 2; result.emplace_back(QuestTransition{QuestId::SiegeOnHarrogath, siege});
    }
}
void actFiveDeath(const EnemyDied &death, const QuestDeathContext &context, QuestDeathPlan &plan) {
    if (death.identity.monster == "baalcrab" && int(death.region) == 132 && death.identity.origin != SpawnOrigin::Summoned &&
        context.records[questIndex(QuestId::EveOfDestruction)].stage < 5) {
        plan.steps.emplace_back(QuestDeathWorldEffect::BaalTyrael);
        plan.steps.emplace_back(QuestDeathTransition{QuestId::EveOfDestruction, {5, 0}});
        plan.questFirstKill = true;
    }
    if (context.ancientsCleared) {
        plan.steps.emplace_back(QuestDeathWorldEffect::AncientsDefeated);
        if (context.ancientsRewardEligible && context.records[questIndex(QuestId::RiteOfPassage)].stage < 4) {
            plan.steps.emplace_back(QuestDeathWorldEffect::AncientsExperience);
            plan.steps.emplace_back(QuestDeathTransition{QuestId::RiteOfPassage, {4, 0}});
            if (!context.records[questIndex(QuestId::EveOfDestruction)].stage)
                plan.steps.emplace_back(QuestDeathTransition{QuestId::EveOfDestruction, {1, 0}});
        }
    }
    if (death.identity.superUnique == "Nihlathak Boss" && int(death.region) == 124 && death.identity.origin != SpawnOrigin::Summoned &&
        context.records[questIndex(QuestId::BetrayalOfHarrogath)].stage < 3)
        plan.steps.emplace_back(QuestDeathTransition{QuestId::BetrayalOfHarrogath, {3, 0}});
    if (death.identity.superUnique == "Siege Boss" && int(death.region) == 110 && death.identity.origin != SpawnOrigin::Summoned &&
        context.records[questIndex(QuestId::SiegeOnHarrogath)].stage < 3)
        plan.steps.emplace_back(QuestDeathTransition{QuestId::SiegeOnHarrogath, {3, 0}});
}
} // namespace d2x
