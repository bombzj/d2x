#pragma once
#include <array>
#include <cstdint>

namespace d2x {
enum class CurseDecisionAction { Restore,Idle,Attack,Escape,Wander,Approach };
struct FearDecisionInput {
    std::array<int32_t,3> parameters{};
    int distance{},walkVelocity{},runVelocity{};
    bool stateActive{},combat{},interact{},attack1{},run{};
};
struct FearDecision {
    CurseDecisionAction action{CurseDecisionAction::Idle};
    std::array<int32_t,3> parameters{};
    int idleFrames{},velocityBonus{};
    bool firstEscape{},run{},fallback{};
};
FearDecision decideFear(const FearDecisionInput &);
CurseDecisionAction decideDimVision(bool combat,bool interact,bool attack1,uint32_t percentage);
CurseDecisionAction decideTaunt(bool targetAvailable,bool targetInTown,int32_t &phase,bool directCombat,bool alternativeCombat);
}
