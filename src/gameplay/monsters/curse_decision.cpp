#include "curse_decision.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x {
FearDecision decideFear(const FearDecisionInput &input) {
    FearDecision result;result.parameters=input.parameters;
    if(!input.stateActive) {result.action=CurseDecisionAction::Restore;result.idleFrames=1;return result;}
    // SpecialState11 uses a default threshold without storing it in param 0.
    const int threshold=input.parameters[0]?input.parameters[0]:30;
    if(input.distance>threshold) {result.idleFrames=10;return result;}
    if(input.walkVelocity>0) {
        const int ratio=nativeDivide(nativeMultiply(100,input.runVelocity),input.walkVelocity);
        if(ratio>=100) result.velocityBonus=std::min(ratio-100,120);
    }
    if(input.parameters[2] && input.combat && !input.interact && input.attack1) {
        result.action=CurseDecisionAction::Attack;return result;
    }
    result.action=CurseDecisionAction::Escape;result.firstEscape=!input.parameters[2];
    result.fallback=!result.firstEscape;result.run=input.run;result.parameters[2]=1;
    return result;
}
CurseDecisionAction decideDimVision(bool combat,bool interact,bool attack1,uint32_t percentage) {
    if(combat && !interact && attack1) return CurseDecisionAction::Attack;
    return percentage<20?CurseDecisionAction::Wander:CurseDecisionAction::Idle;
}
CurseDecisionAction decideTaunt(bool available,bool town,int32_t &phase,bool directCombat,bool alternativeCombat) {
    if(!available || town) return CurseDecisionAction::Restore;
    if(!phase) {phase=1;return CurseDecisionAction::Approach;}
    if(directCombat) return CurseDecisionAction::Attack;
    if(alternativeCombat) {phase=0;return CurseDecisionAction::Attack;}
    phase=1;return CurseDecisionAction::Approach;
}
}
