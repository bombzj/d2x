#pragma once
#include "gameplay/quest/state.hpp"

namespace d2x {
enum class TowerStage : uint32_t {
    Unstarted, TomeRead, TowerEntered, CellarEntered, CountessSlain
};
bool towerAdvance(QuestRecord &record, TowerStage stage);
} // namespace d2x
