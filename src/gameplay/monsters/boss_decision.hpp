#pragma once
#include "ai_spec.hpp"
#include "decision.hpp"
#include "core/math.hpp"
#include <optional>
namespace d2x {
struct MonsterBossInput {
    MonsterDecisionInput combat;
    int homeDistance{}, targetHomeDistance{}, summonChance{};
    bool sameHomeRoom{true}, firewallReady{};
};
struct MonsterBossDecision {
    MonsterDecision action;
    bool returnHome{};
    int summonChance{};
    Vec summonOffset;
};
// master monster_special_ai, verified against AITHINK_Fn019/029/034/059
// and Countess special state 13 (sub_6FCE5520). Server AI policy only.
bool hasBossDecision(MonsterAiKind);
std::optional<MonsterBossDecision> decideBossMonster(const MonsterAiProfile &,MonsterBossInput);
} // namespace d2x
