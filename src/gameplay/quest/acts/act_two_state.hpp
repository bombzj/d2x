#pragma once
#include "gameplay/quest/state.hpp"

namespace d2x {
enum class RadamentStage : uint32_t { Unstarted, Assigned, LeftTown, Slain, Rewarded };
enum class StaffStage : uint32_t { Unstarted, ArtifactsFound = 1, Assembled = 5, Submitted = 6 };
enum class SunStage : uint32_t { Unstarted, Darkness, Explained, AltarDestroyed, Confirmed };
enum class ArcaneStage : uint32_t { Unstarted, PalaceOpened, JerhynBriefed, Entered, JournalRead };
enum class SummonerStage : uint32_t { Unstarted, Encountered, Slain, Confirmed };
enum class TombsStage : uint32_t { Unstarted, Assigned, DurielSlain, TyraelRescued, JerhynConfirmed, PassageGranted };
inline constexpr uint32_t staffScrollExplained = 1;
inline constexpr uint32_t staffCubeExplained = 2;
inline constexpr uint32_t staffHeadExplained = 4;
inline constexpr uint32_t staffShaftExplained = 8;
inline constexpr uint32_t staffAssemblyExplained = 16;
inline constexpr uint32_t staffExplanationMask = staffScrollExplained | staffCubeExplained |
    staffHeadExplained | staffShaftExplained | staffAssemblyExplained;
inline constexpr uint32_t radamentBookPending = 1;
inline constexpr uint32_t radamentBookUsed = 2;
} // namespace d2x
