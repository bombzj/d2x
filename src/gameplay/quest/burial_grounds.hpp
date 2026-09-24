#pragma once
#include "gameplay/quest/state.hpp"

namespace d2x {
enum class BurialStage : uint32_t { Unstarted, Assigned, Entered, BloodRavenSlain, Rewarded };
bool burialAdvanceOnTalk(QuestRecord &record, bool denCompleted);
bool burialAdvanceOnEntry(QuestRecord &record);
bool burialAdvanceOnBloodRaven(QuestRecord &record);
bool burialClaimReward(QuestRecord &record);
} // namespace d2x
