#pragma once
#include "decision.hpp"
#include "ai_spec.hpp"
#include <optional>
namespace d2x {
MonsterDecision decideFallenShaman(const MonsterAiProfile &,MonsterDecisionInput,bool corpseAvailable,bool resurrectionReady);
struct FetishShamanDecision {MonsterDecision action;uint8_t command{};bool clearInferno{},corpseTarget{};};
FetishShamanDecision decideFetishShaman(const MonsterAiProfile &,MonsterDecisionInput,int infernoRank,
    bool infernoActive,bool corpseAvailable,bool resurrectionReady,int corpseDistanceSquared);
}
