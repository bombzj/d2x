#pragma once
#include "ai_spec.hpp"
#include <cstdint>
#include <optional>
namespace d2x {
enum class MeleeDecisionAction { Idle, Approach, Attack };
struct MeleeDecisionInput {
    bool contact{}, charged{};
    int distance{}, difficulty{};
    uint64_t random{};
};
struct MeleeDecision {
    MeleeDecisionAction action{MeleeDecisionAction::Idle};
    int waitFrames{}, velocityPercent{75}, stopDistance{};
    bool running{}, charged{};
    uint64_t random{};
};
// Pure adaptations of master corrupt_rogue_ai, goatman_ai and corrupt_lancer_ai.
// The caller owns cooldowns, pursuit, target lookup and command commitment.
bool hasMeleeDecision(MonsterAiKind);
std::optional<MeleeDecision> decideMeleeMonster(const MonsterAiProfile &, MeleeDecisionInput);
float monsterMovementSpeed(int nativeVelocity, int percentage);
}
