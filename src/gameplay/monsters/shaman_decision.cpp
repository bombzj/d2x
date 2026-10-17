#include "shaman_decision.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
FetishShamanDecision decideFetishShaman(const MonsterAiProfile &profile,MonsterDecisionInput in,int infernoRank,
    bool infernoActive,bool corpseAvailable,bool resurrectionReady,int corpseDistanceSquared) {
    const auto &p=profile.params;FetishShamanDecision result;auto &out=result.action;
    out.random=in.random;out.waitFrames=10;
    const auto chance=[&](int percent){return int(limitedRandom(out.random,100))<percent;};
    if(in.hasTarget && in.distance<std::max(infernoRank,1) && !infernoActive) {
        result.command=1;out.action=MonsterDecisionAction::Special;out.skillSlot=0;out.attackMode=4;return result;
    }
    result.clearInferno=infernoActive;
    if(corpseAvailable && chance(p[0]) && resurrectionReady) {
        result.command=14;result.corpseTarget=true;
        // Fn065 compares its squared distance directly with aip3, not aip3².
        if(corpseDistanceSquared<=p[2]) {out.action=MonsterDecisionAction::Special;out.skillSlot=2;out.attackMode=14;}
        else {out.action=MonsterDecisionAction::Wander;out.stopDistance=10;out.wanderAroundTarget=true;}
    } else if(chance(p[3])) {out.action=MonsterDecisionAction::Circle;out.stopDistance=4;}
    return result;
}
MonsterDecision decideFallenShaman(const MonsterAiProfile &profile,MonsterDecisionInput in,bool corpseAvailable,bool resurrectionReady) {
    const auto &p=profile.params;MonsterDecision out;out.random=in.random;
    auto chance=[&](int percent){return int(limitedRandom(out.random,100))<percent;};
    // master fallenShamanThink / AITHINK_Fn013, including the independent
    // command and resurrection rolls and the alternate visible target roll.
    if(in.contact && chance(p[2])) {out.action=MonsterDecisionAction::Attack;return out;}
    out.commandParty=chance(p[0]);
    if(corpseAvailable && chance(p[0]) && resurrectionReady) {out.action=MonsterDecisionAction::Special;out.skillSlot=0;}
    else if(in.hasTarget && in.distance<p[4] && chance(p[1])) {out.action=MonsterDecisionAction::Special;out.skillSlot=1;}
    else if(in.canShoot && in.shootDistance<p[4] && chance(p[1])) {out.action=MonsterDecisionAction::Special;out.skillSlot=1;out.useShootableTarget=true;}
    else if(chance(p[2])) {out.action=MonsterDecisionAction::Circle;out.stopDistance=3;}
    else out.waitFrames=10;
    return out;
}
}
