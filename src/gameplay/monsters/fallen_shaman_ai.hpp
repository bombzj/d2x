#pragma once
#include "gameplay/monsters/ability_spec.hpp"
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
enum class FallenShamanAction { Melee, Resurrect, Fire, Circle, Idle };
struct FallenShamanDecision {
    FallenShamanAction action;
    bool commandMinions = false;
    bool alternateTarget = false;
};
bool fallenShamanResurrectionTarget(const Enemy &shaman, const Enemy &corpse,
                                   const MonsterResurrection &skill);
int fallenShamanCorpseDistance(const Enemy &shaman, const Enemy &corpse, int size);
FallenShamanDecision fallenShamanThink(Enemy &enemy, const MonsterAiProfile &rules,
                                      float distance, bool inCombat, bool hasCorpse, float alternateDistance);
} // namespace d2x
