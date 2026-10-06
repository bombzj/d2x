#pragma once
#include "gameplay/quest/state.hpp"

namespace d2x {
enum class CainStage : uint32_t {
    Unstarted, Assigned, TreeOpened, BarkAcquired, ScrollTranslated,
    PortalOpened, TristramEntered, Rescued, Rewarded
};
inline constexpr uint32_t cainStoneCountMask = 7;
inline constexpr uint32_t cainRescuedByRogues = 8;
} // namespace d2x
