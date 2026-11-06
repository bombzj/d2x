#pragma once
#include "gameplay/quest/state.hpp"

namespace d2x {
inline constexpr uint32_t toolsPickupNarrated = 1;
enum class ToolsStage : uint32_t {
    Unstarted, Assigned, BarracksEntered, MalusDropped, MalusAcquired,
    RewardReady, Imbued
};
bool toolsAdvance(QuestRecord &record, ToolsStage stage);
} // namespace d2x
