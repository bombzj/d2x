#pragma once
#include "ai_spec.hpp"
#include "decision.hpp"
#include <optional>
namespace d2x {
// Pure adaptations of master corrupt_rogue_ai, goatman_ai and corrupt_lancer_ai.
// The caller owns cooldowns, pursuit, target lookup and command commitment.
bool hasMeleeDecision(MonsterAiKind);
std::optional<MonsterDecision> decideMeleeMonster(const MonsterAiProfile &, MonsterDecisionInput);
float monsterMovementSpeed(int nativeVelocity, int percentage);
}
