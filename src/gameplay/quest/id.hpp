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
inline constexpr size_t questIndex(QuestId quest) { return size_t(quest); }
inline constexpr size_t questActCount = 5;

} // namespace d2x
