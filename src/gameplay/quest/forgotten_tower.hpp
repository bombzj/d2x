#pragma once
#include "gameplay/quest/state.hpp"

namespace d2x {
enum class TowerStage : uint32_t {
    Unstarted, TomeRead, TowerEntered, CellarEntered, CountessSlain
};
} // namespace d2x
