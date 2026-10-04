#pragma once
#include "state.hpp"
#include "prelude.hpp"
#include "facts.hpp"
#include <optional>
#include <string_view>
#include <vector>

namespace d2x {
struct QuestNpcFacts {
    int act = -1, level = 0, characterLevel = 1;
    std::string_view npcClass;
    bool introduced = false, hasIntroduction = false;
    std::array<bool, size_t(QuestPreludeId::Count)> preludes{};
    QuestItemFacts items;
};
struct QuestSpeechRequest {
    QuestId quest;
    std::string_view state, fallbackState;
    bool introduction = false;
};
struct QuestDialogueRequest {
    QuestSpeechRequest speech;
    bool automatic = false, advances = false, alert = false, unread = false;
    uint32_t staffExplanation = 0;
};
struct QuestNpcQuery {
    std::vector<QuestSpeechRequest> topics;
    // In priority order. The authority resolves original text and unread keys.
    std::vector<QuestDialogueRequest> dialogues;
    std::optional<QuestPreludeId> prelude;
    bool introductionAlert = false;
};
// No session, content, inventory, MPQ, device or renderer dependencies.
QuestNpcQuery queryNpcQuests(const DifficultyQuests &records, const QuestNpcFacts &facts);
} // namespace d2x
