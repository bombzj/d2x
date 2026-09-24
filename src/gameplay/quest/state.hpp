#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace d2x {
enum class ActOneQuest : uint8_t {
    DenOfEvil,
    SistersBurialGrounds,
    SearchForCain,
    ForgottenTower,
    ToolsOfTheTrade,
    SistersToTheSlaughter,
    Count
};

// Stage meanings belong to each quest's rule, not to the presentation layer.
// The compact, difficulty-local record also leaves room for multi-step quests.
struct QuestRecord {
    uint32_t stage = 0;
    uint32_t flags = 0;
};
using ActOneQuestBook = std::array<std::array<QuestRecord, size_t(ActOneQuest::Count)>, 3>;

inline constexpr size_t questIndex(ActOneQuest quest) { return size_t(quest); }
} // namespace d2x
