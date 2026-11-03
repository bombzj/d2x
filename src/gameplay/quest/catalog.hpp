#pragma once
#include "id.hpp"
#include <array>
#include <span>

namespace d2x {
// Current 1.13c S2C 5E initialization order; distinct from journal/D2S order.
extern const std::array<unsigned,37> nativeQuestInitializationSlots;
struct QuestDefinition {
    QuestId id;
    int act; // Zero-based, like Levels.Act after content decoding.
    unsigned displaySlot, nativeSlot, icon;
    unsigned nativeQuest; // Original quest number within this act, not display order.
    uint32_t completedStage;
};
// Native IDs, display order and resource order are distinct. Only implemented
// quests are registered; Acts III-V acquire descriptors together with rules.
extern const std::array<QuestDefinition, size_t(QuestId::Count)> questDefinitions;
const QuestDefinition &questDefinition(QuestId id);
uint32_t questCompletionStage(QuestId quest);
std::span<const QuestDefinition> questsForAct(int act);
unsigned questAct(QuestId id);

} // namespace d2x
