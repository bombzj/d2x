#include "ranged_decision.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
bool hasRangedDecision(MonsterAiKind kind) {
    return kind==MonsterAiKind::QuillRat || kind==MonsterAiKind::CorruptArcher ||
        kind==MonsterAiKind::SkeletonBow || kind==MonsterAiKind::SkeletonMage || kind==MonsterAiKind::Bighead;
}
// master family adapters, checked against AITHINK_Fn004/014/035/037/064.
// A failed route is reported to the caller; it must use this explicit alternate
// intent rather than converting an unimplemented ranged action into melee.
std::optional<MonsterDecision> decideRangedMonster(const MonsterAiProfile &profile,MonsterDecisionInput input) {
    if(!hasRangedDecision(profile.kind)) return {};
    const auto &p=profile.params;auto random=input.random;
    MonsterDecision result;result.waitFrames=1;
    auto chance=[&](int percent){return int(limitedRandom(random,100))<percent;};
    auto shoot=[&](uint8_t mode){result.action=MonsterDecisionAction::Attack;result.attackMode=mode;};
    auto circle=[&](int radius){result.action=MonsterDecisionAction::Circle;result.stopDistance=radius;};
    auto approach=[&](int radius,int velocity=75,bool run=false){result.action=MonsterDecisionAction::Approach;result.approachRadius=radius;result.velocityPercent=velocity;result.running=run;};
    auto retreat=[&](int radius,int velocity,uint8_t fallback){result.action=MonsterDecisionAction::Retreat;result.stopDistance=radius;result.velocityPercent=velocity;result.attackWhenMoveFails=true;result.failedMoveAttackMode=fallback;};
    switch(profile.kind) {
    case MonsterAiKind::QuillRat:
        if(input.contact) shoot(4);
        else if(input.retaliating) shoot(5);
        else if(input.distance>=p[0]) {result.action=MonsterDecisionAction::Wander;result.stopDistance=std::max(3,p[3]);}
        else if(chance(p[1])) shoot(5);
        else {retreat(p[3],75,5);if(input.distance>=4) {result.attackWhenMoveFails=false;result.failedMoveWanderRadius=std::max(3,p[3]);}}
        break;
    case MonsterAiKind::Bighead:
        if(!input.contact && input.retaliating) shoot(5);
        else if(input.lifePercent>=p[0]) {
            if(input.contact) shoot(4);
            else if(input.distance<15 && input.clear && chance(p[2])) shoot(5);
            else approach(0);
        } else if(input.distance<3) retreat(5,125,5);
        else if(input.distance>15) approach(6);
        else if(input.clear && chance(p[3])) shoot(5);
        else if(chance(p[1])) circle(3);
        else result.waitFrames=10;
        break;
    case MonsterAiKind::CorruptArcher:
        if(!input.clear) {if(chance(50)) circle(3);else result.waitFrames=p[2];}
        else if(!input.contact && input.retaliating) shoot(4);
        else if(input.distance<6 && chance(p[3]) && !input.retreatBlocked) {retreat(12,175,4);result.attackWhenMoveFails=false;}
        else if(p[7]>0 && input.distance>p[7] && chance(p[0])) approach(p[7],85);
        else if(input.distance>p[4]) approach(p[4],175,true);
        else if(chance(p[1])) shoot(4);
        else result.waitFrames=p[2];
        break;
    case MonsterAiKind::SkeletonBow:
        if(input.clear && input.retaliating) shoot(4);
        else if(input.clear && input.distance<20) {
            if(chance(p[0])) shoot(4);else if(chance(20)) circle(3);else result.waitFrames=p[1];
        } else if(chance(p[2])) {approach(p[3]);result.targetDistance=p[4];}
        else result.waitFrames=20;
        break;
    case MonsterAiKind::SkeletonMage:
        if(input.clear && input.distance>p[1] && chance(p[2])) approach(p[1],85);
        else if(input.clear && input.distance<=p[3] && chance(p[4])) retreat(5,100,4);
        else if(input.clear && input.distance<p[5] && chance(p[0])) shoot(4);
        else if(input.distance>p[1] && chance(p[2])) approach(p[1],85);
        else if(chance(p[6])) circle(4);
        else result.waitFrames=p[7];
        break;
    default: return {};
    }
    result.random=random;return result;
}
}
