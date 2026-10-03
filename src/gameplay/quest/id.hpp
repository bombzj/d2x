#pragma once
#include <cstddef>
#include <cstdint>

namespace d2x {
enum class QuestId : uint8_t {
    DenOfEvil,
    SistersBurialGrounds,
    SearchForCain,
    ForgottenTower,
    ToolsOfTheTrade,
    SistersToTheSlaughter,
    RadamentsLair,
    HoradricStaff,
    TaintedSun,
    ArcaneSanctuary,
    Summoner,
    SevenTombs,
    Count
};
using ActOneQuest = QuestId;
inline constexpr size_t actOneQuestCount = 6;

inline constexpr size_t questIndex(ActOneQuest quest) { return size_t(quest); }
inline constexpr unsigned questAct(QuestId quest) { return questIndex(quest) < 6 ? 1 : 2; }

} // namespace d2x
