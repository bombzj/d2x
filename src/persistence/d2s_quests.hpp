#pragma once
#include "gameplay/quest/state.hpp"
#include <string_view>

namespace d2x {
struct CharacterRecord;
struct D2sFixedSections;
struct NpcDialogues;
// Native v96 quest/PlrIntro fields only. Unknown bits remain in fixed sections.
void importD2sQuests(CharacterRecord &, const D2sFixedSections &, const NpcDialogues &);
void exportD2sQuests(const CharacterRecord &, D2sFixedSections &, const NpcDialogues &);
int validateD2sQuestRecords(const QuestBook &); // Count supported skill-point rewards.
void reconcileD2sQuestItem(CharacterRecord &, int difficulty, std::string_view code, unsigned nativeDifficulty);
} // namespace d2x
