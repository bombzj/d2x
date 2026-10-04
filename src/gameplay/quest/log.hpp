#pragma once
#include "state.hpp"
#include "facts.hpp"
#include <string_view>
#include <optional>

namespace d2x {
struct QuestLogFacts {
    QuestItemFacts items;
    std::optional<unsigned> denRemaining;
    bool journalRead = false;
    std::optional<unsigned> sealsRemaining;
    bool ancientsBattle = false, gidbinnOnGround = false;
};
struct QuestLogSelection {
    bool active = false, completed = false;
    std::string_view descriptionKey;
    bool appendDenRemaining = false, tombSymbol = false;
    std::optional<unsigned> formatNumber;
};
QuestLogSelection selectQuestLog(QuestId id, const QuestRecord &record, const QuestLogFacts &facts);
} // namespace d2x
