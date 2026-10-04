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
    GoldenBird,
    BladeOfTheOldReligion,
    KhalimsWill,
    LamEsensTome,
    BlackenedTemple,
    Guardian,
    FallenAngel,
    HellsForge,
    TerrorsEnd,
    SiegeOnHarrogath,
    RescueOnMountArreat,
    PrisonOfIce,
    BetrayalOfHarrogath,
    RiteOfPassage,
    EveOfDestruction,
    Count
};
inline constexpr size_t questIndex(QuestId quest) { return size_t(quest); }
inline constexpr size_t questActCount = 5;

} // namespace d2x
