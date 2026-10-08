#include "skirmish_decision.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
bool hasSkirmishDecision(MonsterAiKind kind) {
    return kind == MonsterAiKind::BloodHawk || kind == MonsterAiKind::Fetish;
}
std::optional<MonsterDecision> decideSkirmishMonster(const MonsterAiProfile &rules, MonsterDecisionInput in) {
    if (!hasSkirmishDecision(rules.kind)) return {};
    MonsterDecision out; out.random=in.random; out.phase=in.phase; out.loop=in.loop; out.charged=in.charged;
    const auto &p=rules.params;
    auto chance=[&](int value) { return limitedRandom(out.random,100)<unsigned(value); };
    if (rules.kind == MonsterAiKind::BloodHawk) {
        out.charged=false;
        if (in.contact) {
            if (in.charged || chance(p[2])) out.action=MonsterDecisionAction::Attack;
            else {out.action=MonsterDecisionAction::Retreat;out.stopDistance=4;out.velocityPercent=75+p[3];out.attackWhenMoveFails=true;}
        } else if (chance(p[0])) {
            out.action=MonsterDecisionAction::Approach;out.charged=true;out.velocityPercent=75+p[4];
        } else if (in.distance<=3) {
            out.action=MonsterDecisionAction::Retreat;out.stopDistance=4;out.velocityPercent=75+p[3];out.attackWhenMoveFails=true;
        } else {
            out.action=MonsterDecisionAction::Wander;
            const bool slow=chance(p[1]);out.stopDistance=slow?4:3;out.velocityPercent=slow?25:75;
        }
        return out;
    }
    out.velocityPercent=125;
    if (out.phase==0) {
        if (!in.contact) {out.action=MonsterDecisionAction::Approach;return out;}
        out.phase=1;out.loop=0;
    } else if (out.phase==1) {
        out.loop=std::min(out.loop+1,p[2]+1);
        if (out.loop>p[2] && in.targetLifePercent>p[3]) {
            out.phase=2;out.loop=0;out.action=MonsterDecisionAction::Retreat;out.stopDistance=14;return out;
        }
        if (!in.contact) {out.action=MonsterDecisionAction::Approach;return out;}
    } else if (out.phase==2) {
        if (in.distance<=12) {out.action=MonsterDecisionAction::Retreat;out.stopDistance=14;return out;}
        if (++out.loop>1) {out.phase=0;out.loop=0;}
        if (chance(20)) {out.action=MonsterDecisionAction::Circle;out.stopDistance=4;}
        else out.waitFrames=10;
        return out;
    }
    if (chance(p[0])) out.action=MonsterDecisionAction::Attack;
    else out.waitFrames=p[1];
    return out;
}
}
