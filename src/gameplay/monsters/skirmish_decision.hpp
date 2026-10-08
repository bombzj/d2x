#pragma once
#include "decision.hpp"
#include "ai_spec.hpp"
#include <optional>
namespace d2x {
bool hasSkirmishDecision(MonsterAiKind);
std::optional<MonsterDecision> decideSkirmishMonster(const MonsterAiProfile &, MonsterDecisionInput);
}
