#pragma once
#include "gameplay/quest/state.hpp"
#include <span>
#include <variant>
#include <vector>

namespace d2x {
struct QuestEntryFacts {
    int level = 0, act = -1;
    bool sunScheduled = false;
    bool den = false, burial = false, tristram = false, tower = false;
    bool towerCellar = false, barracks = false, catacombsFour = false;
};
struct QuestTransition { QuestId quest; QuestRecord next; };
struct ScheduleSunDarkening {};
using QuestEntryStep = std::variant<QuestTransition, ScheduleSunDarkening>;
// Ordered steps preserve the existing event/RNG sequence across both acts.
std::vector<QuestEntryStep> planQuestEntry(std::span<const QuestRecord> book,
                                         const QuestEntryFacts &facts);
} // namespace d2x
