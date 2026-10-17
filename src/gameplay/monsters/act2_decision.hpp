#pragma once
#include "ai_spec.hpp"
#include "decision.hpp"
namespace d2x {
struct Act2DecisionInput {
    MonsterDecisionInput combat;
    uint64_t tick{}, deadline{}, novaAt{}, firewallAt{};
    int born{};
    bool burrowed{}, corpse{}, heal{}, preferCold{true};
    bool hasTarget{true}, hasAlly{};
    int allyDistanceSquared{}, targetDistanceSquared{}, attempts{},supportCount{};
    bool birthAvailable{};
    bool leapAvailable{};
    bool chargeAvailable{true};
    bool resurrectionReady{};
};
struct Act2Decision {
    MonsterDecision action;
    uint64_t deadline{}, novaAt{}, firewallAt{};
    bool spawnQueen{}, exhaust{};
    bool seekAlly{}, holdPose{};
    bool idleAfterMove{};
    bool scarabPhaseAfterMove{};
    int postColor{-1};
    int pose{-1}, color{-1}, overlay{-1}, regenerationEighths{-1}, attempts{};
};
bool hasAct2Decision(MonsterAiKind);
Act2Decision decideAct2Monster(const MonsterAiProfile &,const Act2DecisionInput &);
}
