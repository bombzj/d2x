#include "module.hpp"
#include "acts/rules.hpp"
#include <array>

namespace d2x {
std::span<const QuestModule> questModules() {
    // Preserve the old joint-entry ordering (Act II scheduling preceded Act I).
    static constexpr std::array modules{
        QuestModule{1, actTwoNpc, actTwoConversation, actTwoEntry, actTwoDeath, actTwoLog},
        QuestModule{0, actOneNpc, actOneConversation, actOneEntry, actOneDeath, actOneLog},
    };
    return modules;
}
const QuestModule *questModule(int act) {
    for (const auto &module : questModules()) if (module.act == act) return &module;
    return nullptr;
}
QuestNpcQuery queryNpcQuests(const DifficultyQuests &records, const QuestNpcFacts &facts) {
    const auto *module = questModule(facts.act);
    return module ? module->npc(records, facts) : QuestNpcQuery{};
}
std::optional<NpcQuestPlan> planNpcQuest(QuestId id, const QuestRecord &record, const NpcQuestFacts &facts) {
    const auto *module = questModule(questDefinition(id).act);
    return module ? module->conversation(id, record, facts) : std::nullopt;
}
QuestLogSelection selectQuestLog(QuestId id, const QuestRecord &record, const QuestLogFacts &facts) {
    const auto *module = questModule(questDefinition(id).act);
    auto selection = module ? module->log(id, record, facts) : QuestLogSelection{};
    selection.active = record.stage > 0;
    selection.completed = record.stage >= questCompletionStage(id);
    if (selection.completed) selection.descriptionKey = "qstsComplete";
    return selection;
}
} // namespace d2x
