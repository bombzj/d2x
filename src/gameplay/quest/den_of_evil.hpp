#pragma once
#include "gameplay/quest/state.hpp"

namespace d2x {
enum class DenStage : uint32_t { Unstarted, Assigned, Entered, Cleared, Rewarded };
inline constexpr uint32_t denRespecUsed = 1;

bool denAdvanceOnTalk(QuestRecord &record);
bool denAdvanceOnEntry(QuestRecord &record);
bool denAdvanceOnClear(QuestRecord &record, bool encounteredMonsters, bool monstersRemain);
bool denClaimReward(QuestRecord &record);
bool denClaimRespec(QuestRecord &record);
} // namespace d2x
