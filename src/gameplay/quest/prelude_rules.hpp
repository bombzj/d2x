#pragma once
#include "prelude.hpp"
#include <string_view>

namespace d2x {
struct QuestPreludeDefinition {
    QuestPreludeId id;
    int act;
    unsigned nativeSlot;
    std::string_view npcClass; // Reference rule participant; speech is MPQ content.
    uint16_t message, alternate;
    std::string_view alternateClass;
    uint16_t messageFor(std::string_view character) const;
};

extern const std::array<QuestPreludeDefinition, std::size_t(QuestPreludeId::Count)> questPreludes;
const QuestPreludeDefinition *questPreludeForNpc(int act, std::string_view code);
} // namespace d2x
