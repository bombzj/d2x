#pragma once
#include "decision.hpp"
#include "ai_spec.hpp"
#include <optional>
namespace d2x {
MonsterDecision decideFallenShaman(const MonsterAiProfile &,MonsterDecisionInput,bool corpseAvailable);
}
