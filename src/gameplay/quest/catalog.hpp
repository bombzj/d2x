#pragma once
#include "id.hpp"
#include <array>
#include <span>

namespace d2x {
struct QuestDefinition {
    QuestId id;
    int act; // Zero-based, like Levels.Act after content decoding.
    unsigned displaySlot, nativeSlot, icon;
    unsigned nativeQuest; // Original quest number within this act, not display order.
    uint32_t completedStage;
};
// Native IDs, display order and resource order are distinct. Only implemented
// quests are registered; Acts III-V acquire descriptors together with rules.
inline constexpr std::array<QuestDefinition, size_t(QuestId::Count)> questDefinitions{{
    {QuestId::DenOfEvil, 0, 0, 1, 0, 1, 4},
    {QuestId::SistersBurialGrounds, 0, 1, 2, 1, 2, 4},
    {QuestId::SearchForCain, 0, 2, 4, 3, 4, 8},
    {QuestId::ForgottenTower, 0, 3, 5, 4, 5, 4},
    {QuestId::ToolsOfTheTrade, 0, 4, 3, 2, 3, 6},
    {QuestId::SistersToTheSlaughter, 0, 5, 6, 5, 6, 5},
    {QuestId::RadamentsLair, 1, 0, 9, 6, 1, 4},
    {QuestId::HoradricStaff, 1, 1, 10, 7, 2, 6},
    {QuestId::TaintedSun, 1, 2, 11, 8, 3, 4},
    {QuestId::ArcaneSanctuary, 1, 3, 12, 9, 4, 4},
    {QuestId::Summoner, 1, 4, 13, 10, 5, 3},
    {QuestId::SevenTombs, 1, 5, 14, 11, 6, 5},
}};
inline constexpr const QuestDefinition &questDefinition(QuestId id) {
    return questDefinitions.at(questIndex(id));
}
inline constexpr std::span<const QuestDefinition> questsForAct(int act) {
    const QuestDefinition *first = nullptr;
    size_t count = 0;
    for (const auto &definition : questDefinitions)
        if (definition.act == act) {
            if (!first) first = &definition;
            ++count;
        }
    return first ? std::span<const QuestDefinition>(first, count) : std::span<const QuestDefinition>{};
}
inline constexpr unsigned questAct(QuestId id) { return unsigned(questDefinition(id).act + 1); }
// Protect the contiguous, display-ordered spans used by projection and rules.
static_assert([] {
    for (size_t i = 0; i < questDefinitions.size(); ++i) {
        const auto &d = questDefinitions[i];
        if (questIndex(d.id) != i || d.act < 0 || d.act >= int(questActCount)) return false;
        if (i && d.act < questDefinitions[i - 1].act) return false;
        if (d.displaySlot != (i && d.act == questDefinitions[i - 1].act
                ? questDefinitions[i - 1].displaySlot + 1 : 0)) return false;
        if (d.nativeSlot >= 48 || d.icon >= questDefinitions.size() || !d.nativeQuest) return false;
        for (size_t j = 0; j < i; ++j)
            if (d.nativeSlot == questDefinitions[j].nativeSlot || d.icon == questDefinitions[j].icon ||
                (d.act == questDefinitions[j].act && d.nativeQuest == questDefinitions[j].nativeQuest)) return false;
    }
    return true;
}());
} // namespace d2x
