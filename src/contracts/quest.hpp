#pragma once
#include "core/id.hpp"
#include "gameplay/quest/id.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
struct QuestEntryView {
    bool active = false, completed = false, known = false;
    int act = 0;
    unsigned displaySlot = 0, icon = 0;
    std::string title;
    std::optional<std::string> description;
    std::optional<unsigned> tombSymbol;
    bool selectable() const { return active || completed || !known; }
};
// One bound player's current-difficulty log; no task flags, inventory or seed.
struct QuestView {
    uint64_t revision = 0;
    EntityId actor;
    int currentAct = 0, tabCount = 1;
    bool showDenRemaining = false;
    std::optional<unsigned> denRemaining;
    std::array<QuestEntryView, size_t(QuestId::Count)> entries;
    std::array<std::vector<QuestId>, questActCount> acts;
    const QuestEntryView &entry(QuestId id) const { return entries.at(questIndex(id)); }
    const std::vector<QuestId> &quests(int act) const { return acts.at(size_t(act)); }
    QuestId displayed(int act, size_t slot) const { return quests(act).at(slot); }
};
} // namespace d2x
