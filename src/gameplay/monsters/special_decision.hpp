#pragma once
#include "decision.hpp"
#include "ai_spec.hpp"
#include <optional>
namespace d2x {
bool hasSpecialDecision(MonsterAiKind);
std::optional<MonsterDecision> decideSpecialMonster(const MonsterAiProfile &,MonsterDecisionInput);
}
