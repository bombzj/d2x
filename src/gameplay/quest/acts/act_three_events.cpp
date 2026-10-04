#include "act_three_state.hpp"
#include "rules.hpp"

namespace d2x {
void actThreeEntry(DifficultyQuests &records, const QuestEntryFacts &facts, std::vector<QuestEntryStep> &result) {
    auto &guardian = records[questIndex(QuestId::Guardian)];
    if (facts.act == 2 && facts.level >= 94 && guardian.stage < 4) {
        const uint32_t next = facts.level == 102 ? 3 : facts.level >= 100 ? 2 : 1;
        if (next > guardian.stage) { guardian.stage = next; result.emplace_back(QuestTransition{QuestId::Guardian, guardian}); }
    }
    auto &temple = records[questIndex(QuestId::BlackenedTemple)];
    if (facts.level == 83 && temple.stage < 2) {
        temple.stage = 2; result.emplace_back(QuestTransition{QuestId::BlackenedTemple, temple});
    }
    auto &tome = records[questIndex(QuestId::LamEsensTome)];
    if (facts.act == 2 && facts.level != 75 && tome.stage == 1) {
        tome.stage = 2; result.emplace_back(QuestTransition{QuestId::LamEsensTome, tome});
    }
    auto &blade = records[questIndex(QuestId::BladeOfTheOldReligion)];
    if (facts.act == 2 && facts.level != 75 && blade.stage == 1) {
        blade.stage = 2; result.emplace_back(QuestTransition{QuestId::BladeOfTheOldReligion, blade});
    }
}
void actThreeDeath(const EnemyDied &death, const QuestDeathContext &context, QuestDeathPlan &plan) {
    if (death.identity.monster == "mephisto" && int(death.region) == 102 && death.identity.origin != SpawnOrigin::Summoned &&
        context.records[questIndex(QuestId::Guardian)].stage < 4) {
        plan.questFirstKill = true;
        plan.steps.emplace_back(QuestDeathWorldEffect::MephistoSoulstone);
        plan.steps.emplace_back(QuestDeathTransition{QuestId::Guardian, {4, guardianSpeechPending}});
    }
    if (context.councilCleared && context.records[questIndex(QuestId::BlackenedTemple)].stage < 3) {
        const uint32_t next = context.records[questIndex(QuestId::KhalimsWill)].stage >= 4 ? 4u : 3u;
        plan.steps.emplace_back(QuestDeathTransition{QuestId::BlackenedTemple, {next, 0}});
    }
    if (context.jadeFigurineBoss) plan.steps.emplace_back(QuestDeathWorldEffect::JadeFigurine);
    if (context.gidbinnBoss) plan.steps.emplace_back(QuestDeathWorldEffect::Gidbinn);
    if (context.khalimFlailDrop) plan.steps.emplace_back(QuestDeathWorldEffect::KhalimFlail);
    else if (context.councilCubeDrop) plan.steps.emplace_back(QuestDeathWorldEffect::CouncilCube);
}
} // namespace d2x
