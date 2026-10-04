#pragma once
#include "gameplay/quest/state.hpp"
namespace d2x {
enum class IzualStage : uint32_t { Unstarted, Assigned, LeftTown, Slain, Rewarded };
inline constexpr uint32_t izualGhostSpoken = 1;
inline constexpr int izualSkillReward = 2; // A4Q1_Callback11_ScrollMessage.
enum class ForgeStage : uint32_t { Unstarted, Assigned, LeftTown, StonePlaced, Smashed, Completed };
enum class TerrorStage : uint32_t { Unstarted, Assigned, LeftTown, Sanctuary, Completed };
inline constexpr uint32_t terrorTyraelPending = 1, terrorCainPending = 2, terrorPortalOpened = 4;
} // namespace d2x
