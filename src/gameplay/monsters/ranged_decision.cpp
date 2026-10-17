#include "ranged_decision.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
namespace d2x {
bool hasRangedDecision(MonsterAiKind kind) {
    return kind==MonsterAiKind::QuillRat || kind==MonsterAiKind::CorruptArcher ||
        kind==MonsterAiKind::SkeletonBow || kind==MonsterAiKind::SkeletonMage || kind==MonsterAiKind::Bighead || kind==MonsterAiKind::FetishBlowgun;
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
    case MonsterAiKind::FetishBlowgun:
        result.phase=input.phase;result.loop=input.loop;
        if(input.contact) {if(chance(p[1])) result.phase=2;}
        else if(input.distance>p[0]) {
            result.action=MonsterDecisionAction::Wander;result.stopDistance=input.hasTarget?6:2;
            result.velocityPercent=125;result.wanderAroundTarget=input.hasTarget;break;
        } else if(input.distance<6 && chance(p[1])) result.phase=2;
        if(result.phase==0) {
            result.loop=nativeAdd(result.loop,1);
            if(result.loop>int(limitedRandom(random,3))+3) {result.phase=1;result.loop=0;}
            if(input.canShoot) {shoot(4);result.useShootableTarget=true;}else circle(4);
        } else if(result.phase==1) {result.phase=result.loop=0;circle(6);result.velocityPercent=125;}
        else if(result.phase==2) {
            if(input.distance<=12) {
                retreat(14,125,4);
                // Flee is relative to the primary target; only its failed
                // movement attack uses the independent shootable target.
                result.useShootableTarget=false;
            } else if(chance(20)) circle(4);
            else {result.phase=result.loop=0;result.waitFrames=10;}
        } else result.waitFrames=10;
        break;
    case MonsterAiKind::QuillRat:
        result.pathSteps=1;
        if(input.contact) shoot(4);
        else if(input.retaliating) shoot(5);
        else if(input.distance>=p[0]) {result.action=MonsterDecisionAction::Wander;result.stopDistance=std::max(3,p[3]);}
        else if(chance(p[1])) shoot(5);
        else {retreat(p[3],75,5);if(input.distance>=4) {result.attackWhenMoveFails=false;result.failedMoveWanderRadius=std::max(3,p[3]);}}
        break;
    case MonsterAiKind::Bighead:
        result.pathSteps=1;
        if(!input.contact && input.retaliating) shoot(5);
        else if(input.lifePercent>=p[0]) {
            if(input.contact) shoot(4);
            else if(input.distance<15 && input.canShoot && chance(p[2])) shoot(5);
            else approach(0);
        } else if(input.distance<3) retreat(5,125,5);
        else if(input.distance>15) {approach(0);result.pathSteps=6;}
        else if(input.canShoot && chance(p[3])) shoot(5);
        else if(chance(p[1])) circle(3);
        else result.waitFrames=10;
        break;
    case MonsterAiKind::CorruptArcher:
        result.useShootableTarget=input.canShoot;
        if(!input.canShoot) {if(chance(50)) circle(3);else result.waitFrames=p[2];}
        else if(!input.contact && input.retaliating) shoot(4);
        else if(!input.continuation && input.shootDistance<6 && chance(p[3])) {
            retreat(12,175,4);result.running=true;result.attackWhenMoveFails=false;
            result.pathSteps=1;result.pathDistance=12;result.failedEscapeContinuation=1;
        }
        else if(p[7]>0 && input.shootDistance>p[7] && chance(p[0])) {approach(0,85);result.pathSteps=p[7];}
        else if(input.shootDistance>p[4]) {approach(0,175,true);result.pathSteps=p[4];}
        else if(chance(p[1])) {
            if((input.skillMask&2) && chance(p[5])) {result.action=MonsterDecisionAction::Special;result.skillSlot=1;}
            else if((input.skillMask&4) && chance(p[6])) {result.action=MonsterDecisionAction::Special;result.skillSlot=2;}
            else if(input.skillMask&1) {result.action=MonsterDecisionAction::Special;result.skillSlot=0;}
            else shoot(4);
        }
        else result.waitFrames=p[2];
        break;
    case MonsterAiKind::SkeletonBow:
        if(input.canShoot && input.retaliating) {shoot(4);result.useShootableTarget=true;}
        else if(input.canShoot && input.shootDistance<20) {
            if(chance(p[0])) shoot(4);else if(chance(20)) circle(3);else result.waitFrames=p[1];
            result.useShootableTarget=result.action==MonsterDecisionAction::Attack;
        } else if(chance(p[2])) {approach(p[3]);result.targetDistance=p[4];result.pathSteps=1;}
        else result.waitFrames=20;
        break;
    case MonsterAiKind::SkeletonMage:
        result.pathSteps=1;
        if(input.canShoot && input.shootDistance>p[1] && chance(p[2])) {approach(0,85);result.pathSteps=std::max(1,p[1]);result.useShootableTarget=true;}
        else if(input.canShoot && input.shootDistance<=p[3] && chance(p[4])) {retreat(5,100,4);result.useShootableTarget=true;}
        else if(input.canShoot && input.shootDistance<p[5] && chance(p[0])) {shoot(4);result.useShootableTarget=true;}
        else if(input.shootDistance>p[1] && chance(p[2])) {approach(0,85);result.pathSteps=std::max(1,p[1]);}
        else if(chance(p[6])) circle(4);
        else result.waitFrames=p[7];
        break;
    default: return {};
    }
    result.random=random;return result;
}
}
