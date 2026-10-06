#pragma once
#include "gameplay/quest/state.hpp"

namespace d2x {
enum class DenStage : uint32_t { Unstarted, Assigned, Entered, Cleared, Rewarded };
inline constexpr uint32_t denRespecUsed = 1;

} // namespace d2x
