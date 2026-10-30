#pragma once
#include "gameplay/quest/id.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace d2x {
// Stage meanings belong to each quest's rule, not to the presentation layer.
// The compact, difficulty-local record also leaves room for multi-step quests.
struct QuestRecord {
    uint32_t stage = 0;
    uint32_t flags = 0;
    // Original personal record where the quest has a native implementation.
    // Coarse stage is not sufficient to reconstruct its retained history bits.
    std::optional<uint16_t> nativeRecord{};
};
using QuestBook = std::array<std::array<QuestRecord, size_t(QuestId::Count)>, 3>;
using DifficultyQuests = std::array<QuestRecord, size_t(QuestId::Count)>;

} // namespace d2x
