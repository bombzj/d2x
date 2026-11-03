#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace d2x {
// Q0 arrival messages have native quest flags but no quest-log slot. They are
// separate from PlrIntro's ordinary NPC introduction flags and journal quests.
enum class QuestPreludeId : uint8_t { LutGholeinArrival, KurastArrival, EncampmentArrival, FortressArrival, Count };
using QuestPreludeBook = std::array<std::array<bool, std::size_t(QuestPreludeId::Count)>, 3>;
} // namespace d2x
