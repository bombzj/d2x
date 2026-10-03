#pragma once
#include "core/id.hpp"
#include "gameplay/quest/id.hpp"
#include <array>
#include <optional>
#include <string>

namespace d2x {
struct QuestEntryView {
    bool active = false, completed = false;
    std::string title;
    std::optional<std::string> description;
    std::optional<unsigned> tombSymbol;
};
// One bound player's current-difficulty log; no task flags, inventory or seed.
struct QuestView {
    uint64_t revision = 0;
    EntityId actor;
    int currentAct = 0, tabCount = 1;
    bool showDenRemaining = false;
    std::optional<unsigned> denRemaining;
    std::array<QuestEntryView, size_t(QuestId::Count)> entries;
    const QuestEntryView &entry(QuestId id) const { return entries.at(questIndex(id)); }
};
} // namespace d2x
