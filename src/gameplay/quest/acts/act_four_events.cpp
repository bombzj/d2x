#include "act_four_state.hpp"
#include "rules.hpp"
namespace d2x {
void actFourEntry(DifficultyQuests &records, const QuestEntryFacts &facts, std::vector<QuestEntryStep> &result) {
    auto &terror = records[questIndex(QuestId::TerrorsEnd)];
    if (facts.act == 3 && facts.level != 103 && terror.stage < 3) {
        const uint32_t next = facts.level == 108 ? 3 : terror.stage ? 2 : 0;
        if (next > terror.stage) { terror.stage = next; result.emplace_back(QuestTransition{QuestId::TerrorsEnd, terror}); }
    }
    auto &forge = records[questIndex(QuestId::HellsForge)];
    if (facts.act == 3 && facts.level != 103 && forge.stage == 1) {
        forge.stage = 2; result.emplace_back(QuestTransition{QuestId::HellsForge, forge});
    }
    auto &record = records[questIndex(QuestId::FallenAngel)];
    if (facts.act == 3 && facts.level != 103 && record.stage == 1) {
        record.stage = 2; result.emplace_back(QuestTransition{QuestId::FallenAngel, record});
    }
}
void actFourDeath(const EnemyDied &death, const QuestDeathContext &context, QuestDeathPlan &plan) {
    if (death.identity.superUnique == "The Feature Creep" && int(death.region) == 107 &&
        death.identity.origin != SpawnOrigin::Summoned) plan.steps.emplace_back(QuestDeathWorldEffect::ForgeHammer);
    if (death.identity.monster == "diablo" && int(death.region) == 108 && death.identity.origin != SpawnOrigin::Summoned &&
        context.records[questIndex(QuestId::TerrorsEnd)].stage < 4) {
        plan.questFirstKill = true;
        plan.steps.emplace_back(QuestDeathTransition{QuestId::TerrorsEnd, {4, terrorTyraelPending | terrorCainPending}});
    }
    if (death.identity.monster == "izual" && int(death.region) == 105 && death.identity.origin != SpawnOrigin::Summoned) {
        plan.steps.emplace_back(QuestDeathWorldEffect::IzualGhost);
        if (context.records[questIndex(QuestId::FallenAngel)].stage < 3)
            plan.steps.emplace_back(QuestDeathTransition{QuestId::FallenAngel, {3, 0}});
    }
}
} // namespace d2x
