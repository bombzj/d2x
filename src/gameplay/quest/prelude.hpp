#pragma once
#include "id.hpp"
#include <array>
#include <string_view>

namespace d2x {
// Q0 arrival messages have native quest flags but no quest-log slot. They are
// separate from PlrIntro's ordinary NPC introduction flags and journal quests.
enum class QuestPreludeId : uint8_t { LutGholeinArrival, Count };
struct QuestPreludeDefinition {
    QuestPreludeId id;
    int act;
    unsigned nativeSlot;
    std::string_view npcClass; // Reference rule participant; speech is MPQ content.
};
inline constexpr std::array<QuestPreludeDefinition, size_t(QuestPreludeId::Count)> questPreludes{{
    {QuestPreludeId::LutGholeinArrival, 1, 8, "jerhyn"},
}};
using QuestPreludeBook = std::array<std::array<bool, size_t(QuestPreludeId::Count)>, 3>;
} // namespace d2x
