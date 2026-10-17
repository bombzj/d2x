#include "special_decision.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
bool hasSpecialDecision(MonsterAiKind kind) {return kind==MonsterAiKind::Arach || kind==MonsterAiKind::Vampire || kind==MonsterAiKind::GargoyleTrap || kind==MonsterAiKind::ThornHulk;}
std::optional<MonsterDecision> decideSpecialMonster(const MonsterAiProfile &profile,MonsterDecisionInput in) {
    if(!hasSpecialDecision(profile.kind)) return {};
    const auto &p=profile.params;MonsterDecision out;out.random=in.random;out.phase=in.phase;out.loop=in.loop;out.alerted=in.alerted;out.upgradeCooldown=std::max(0,in.upgradeCooldown-1);
    auto chance=[&](int percent){return int(limitedRandom(out.random,100))<percent;};
    auto circle=[&](int distance){out.action=MonsterDecisionAction::Circle;out.stopDistance=distance;};
    auto retreat=[&](int distance,int speed=75){out.action=MonsterDecisionAction::Retreat;out.stopDistance=distance;out.velocityPercent=speed;};
    auto approach=[&]{out.action=MonsterDecisionAction::Approach;out.stopDistance=profile.meleeRange;};
    auto spell=[&]{out.action=MonsterDecisionAction::Special;out.skillSlot=chance(50)?0:3;};
    auto upgrade=[&] {
        if(out.upgradeCooldown) return false;
        if((p[4]&2) && chance(p[3])) {out.action=MonsterDecisionAction::Special;out.skillSlot=1;out.upgradeCooldown=11;return true;}
        if((p[4]&4) && chance(p[3])) {out.action=MonsterDecisionAction::Special;out.skillSlot=2;out.upgradeCooldown=11;return true;}
        return false;
    };
    if(profile.kind==MonsterAiKind::ThornHulk) {
        // Fn027: the initial frenzy strike is followed by aip6 further
        // strikes. Three ordinary attack decisions then inhibit frenzy.
        if(!in.contact) {out.phase=0;approach();out.pathSteps=1;return out;}
        const auto frenzy=[&] {out.action=MonsterDecisionAction::Special;out.skillSlot=0;out.attackMode=5;};
        if(out.phase>0) {frenzy();if(--out.phase<=0) out.loop=3;}
        else if(chance(p[0])) {
            if(out.loop>0 || !chance(p[3])) {--out.loop;out.action=MonsterDecisionAction::Attack;out.attackMode=chance(p[1])?5:4;}
            else {frenzy();out.phase=p[5];}
        } else if(chance(p[2])) circle(4);
        else out.waitFrames=15;
        return out;
    }
    if(profile.kind==MonsterAiKind::GargoyleTrap) {
        // AITHINK_Fn063: separate recovery invocation, then the MPQ stall.
        if(out.phase) {out.phase=0;out.waitFrames=p[2];return out;}
        if(in.trapAxisAligned && in.distance<p[0] && chance(p[1])) {out.action=MonsterDecisionAction::Special;out.skillSlot=0;out.phase=1;}
        else out.waitFrames=p[3];
        return out;
    }
    if(profile.kind==MonsterAiKind::Arach) {
        // master arachThink, AITHINK_Fn026. SpiderLay is a moving aura,
        // not a substitute A2 damage attack.
        if(out.phase==1) {
            out.alerted=false;
            if(in.lifePercent>75) {out.phase=0;if(!chance(p[2])) circle(6);else {out.phase=2;approach();}}
            else if(in.contact && p[0]>25 && chance(p[0]-25)) out.action=MonsterDecisionAction::Attack;
            else if(in.distance<p[3] || in.retaliating) retreat(4);
            else {out.phase=0;circle(12);}
        } else if(!in.contact) {
            if(in.retaliating || in.alerted) {out.alerted=true;approach();}
            else {
                out.loop=out.loop>=20?0:out.loop+1;
                if(out.loop==1 && chance(p[2])) {out.alerted=true;approach();}
                else if(chance(20)) {out.action=MonsterDecisionAction::Wander;out.stopDistance=6;}
                else out.waitFrames=15;
            }
        } else {
            out.phase=2;
            if(chance(p[0])) out.action=MonsterDecisionAction::Attack;
            else if(in.lifePercent<p[4]) {out.phase=1;if(!in.webActive) {out.action=MonsterDecisionAction::Special;out.skillSlot=0;}else retreat(8);}
            else if(chance(p[1])) circle(4);
            else out.waitFrames=15;
        }
        return out;
    }
    // Fn028. Continuations enter after a real failed escape, retaining the
    // already consumed rolls and this invocation's decremented cooldown.
    if(in.continuation) out.upgradeCooldown=in.upgradeCooldown;
    const bool ordinary=(p[4]&1)!=0;
    const auto walk=[&] {approach();out.pathSteps=1;};
    const auto fire=[&](bool independent) {spell();out.attackMode=7;out.useShootableTarget=independent;};
    if(!in.continuation && in.retaliating) {
        if(!out.phase) out.phase=1;
        if(in.distance<30) out.loop=std::max(out.loop,in.distance);
        if(in.contact) {if(chance(31) && ordinary) fire(false);else out.action=MonsterDecisionAction::Attack;return out;}
    }
    if(out.phase==2 && in.continuation!=2) {
        if(!in.continuation && in.lifePercent>=75) {out.phase=1;walk();return out;}
        if(!in.continuation && (in.distance<14 || in.distance<=out.loop)) {
            retreat(8,75+profile.retreatVelocityBonus);out.failedEscapeContinuation=1;return out;
        }
        if(in.distance>=p[2] || !chance(p[1])) out.waitFrames=15;
        else if(upgrade()) out.attackMode=7;
        else if(ordinary && in.canShoot && in.shootDistance<=20) fire(true);
        else circle(4);
        return out;
    }
    if(!in.continuation && in.lifePercent<33) {out.phase=2;retreat(8);out.failedEscapeContinuation=2;return out;}
    if(in.contact) {
        out.phase=1;
        if(chance(p[0])) {
            if(!ordinary || !in.canShoot || !chance(31)) {out.action=MonsterDecisionAction::Attack;return out;}
            if(in.shootDistance<=20) {fire(true);return out;}
        }
        if(chance(33)) circle(4);else out.waitFrames=10;
    } else if(in.distance>=p[2]) {if(out.phase==1) walk();else out.waitFrames=15;}
    else {
        out.phase=1;
        if(chance(p[1])) {
            if(upgrade()) out.attackMode=7;
            else if(!ordinary || !in.canShoot || in.shootDistance>20) walk();
            else if(!chance(75)) circle(4);
            else fire(true);
        } else if(in.distance>20) walk();
        else if(in.distance<9 && chance(50)) retreat(8);
        else if(chance(50)) circle(4);
        else out.waitFrames=10;
    }
    return out;
}
}
