#pragma once
#include "gameplay/model/state.hpp"

namespace d2x {
enum class FallenShamanAction { Melee, Resurrect, Fire, Circle, Idle };
struct FallenShamanDecision {
    FallenShamanAction action;
    bool commandMinions = false;
    bool alternateTarget = false;
};
bool fallenShamanResurrectionTarget(const Enemy &shaman, const Enemy &corpse,
                                   const MonsterResurrection &skill);
FallenShamanDecision fallenShamanThink(Enemy &enemy, const MonsterAiProfile &rules,
                                      float distance, bool inCombat, bool hasCorpse, float alternateDistance);
} // namespace d2x
