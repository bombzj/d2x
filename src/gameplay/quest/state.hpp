#pragma once
#include "gameplay/quest/id.hpp"
#include "gameplay/quest/catalog.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace d2x {
// Stage meanings belong to each quest's rule, not to the presentation layer.
// The compact, difficulty-local record also leaves room for multi-step quests.
struct QuestRecord {
    uint32_t stage = 0;
    uint32_t flags = 0;
};
using QuestBook = std::array<std::array<QuestRecord, size_t(QuestId::Count)>, 3>;
using DifficultyQuests = std::array<QuestRecord, size_t(QuestId::Count)>;

inline constexpr uint32_t questCompletionStage(QuestId quest) {
    return questDefinition(quest).completedStage;
}
} // namespace d2x
