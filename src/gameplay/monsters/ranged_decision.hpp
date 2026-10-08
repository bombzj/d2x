#pragma once
#include "decision.hpp"
#include "ai_spec.hpp"
#include <optional>
namespace d2x {
bool hasRangedDecision(MonsterAiKind);
std::optional<MonsterDecision> decideRangedMonster(const MonsterAiProfile &, MonsterDecisionInput);
}
