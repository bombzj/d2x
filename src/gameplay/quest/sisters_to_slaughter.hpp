#pragma once
#include "gameplay/quest/state.hpp"

namespace d2x {
enum class SlaughterStage : uint32_t {
    Unstarted, Assigned, CatacombsEntered, AndarielSlain, PassageReady, Completed
};
bool slaughterAdvance(QuestRecord &record, SlaughterStage stage);
} // namespace d2x
