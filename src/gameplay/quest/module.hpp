#pragma once
#include "npc_query.hpp"
#include "npc_conversation.hpp"
#include "region_entry.hpp"
#include "death.hpp"
#include "log.hpp"
#include <span>

namespace d2x {
// D2MOO's per-quest callback registration adapted to explicit, pure act rules.
// The local authority alone commits their plans and delivers world/reward effects.
struct QuestModule {
    int act;
    QuestNpcQuery (*npc)(const DifficultyQuests &, const QuestNpcFacts &);
    std::optional<NpcQuestPlan> (*conversation)(QuestId, const QuestRecord &, const NpcQuestFacts &);
    void (*entry)(DifficultyQuests &, const QuestEntryFacts &, std::vector<QuestEntryStep> &);
    void (*death)(const EnemyDied &, const QuestDeathContext &, QuestDeathPlan &);
    QuestLogSelection (*log)(QuestId, const QuestRecord &, const QuestLogFacts &);
};
std::span<const QuestModule> questModules();
const QuestModule *questModule(int act);
} // namespace d2x
