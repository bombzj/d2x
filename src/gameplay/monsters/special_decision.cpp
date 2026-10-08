#include "special_decision.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
bool hasSpecialDecision(MonsterAiKind kind) {return kind==MonsterAiKind::Arach || kind==MonsterAiKind::Vampire;}
std::optional<MonsterDecision> decideSpecialMonster(const MonsterAiProfile &profile,MonsterDecisionInput in) {
    if(!hasSpecialDecision(profile.kind)) return {};
    const auto &p=profile.params;MonsterDecision out;out.random=in.random;out.phase=in.phase;out.loop=in.loop;out.alerted=in.alerted;
    auto chance=[&](int percent){return int(limitedRandom(out.random,100))<percent;};
    auto circle=[&](int distance){out.action=MonsterDecisionAction::Circle;out.stopDistance=distance;};
    auto retreat=[&](int distance,int speed=75){out.action=MonsterDecisionAction::Retreat;out.stopDistance=distance;out.velocityPercent=speed;};
    auto approach=[&]{out.action=MonsterDecisionAction::Approach;out.stopDistance=profile.meleeRange;};
    auto spell=[&]{out.action=MonsterDecisionAction::Special;out.skillSlot=chance(50)?0:3;};
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
    // Act1 vampire5 has spell mask 1 in all three difficulties. Fn028's
    // Firewall/Meteor upgrade bits are not enabled by these original rows.
    const bool ordinary=(p[4]&1)!=0;
    if(in.retaliating) {
        if(!out.phase) out.phase=1;
        if(in.distance<30) out.loop=std::max(out.loop,in.distance);
        if(in.contact) {if(ordinary && chance(31)) spell();else out.action=MonsterDecisionAction::Attack;return out;}
    }
    if(out.phase==2) {
        if(in.lifePercent>=75) {out.phase=1;approach();}
        else if((in.distance<14 || in.distance<=out.loop) && !in.retreatBlocked) retreat(8,75+profile.retreatVelocityBonus);
        else if(in.distance>=p[2] || !chance(p[1])) out.waitFrames=15;
        else if(ordinary && in.clear && in.distance<=20) spell();
        else circle(4);
        return out;
    }
    if(in.lifePercent<33) {out.phase=2;if(!in.retreatBlocked) {retreat(8);return out;}}
    if(in.contact) {
        out.phase=1;
        if(chance(p[0])) {if(ordinary && in.clear && chance(31) && in.distance<=20) spell();else out.action=MonsterDecisionAction::Attack;}
        else if(chance(33)) circle(4);else out.waitFrames=10;
    } else if(in.distance>=p[2]) {if(out.phase==1) approach();else out.waitFrames=15;}
    else {
        out.phase=1;
        if(chance(p[1])) {if(!ordinary || !in.clear || in.distance>20) approach();else if(!chance(75)) circle(4);else spell();}
        else if(in.distance>20) approach();
        else if(in.distance<9 && chance(50) && !in.retreatBlocked) retreat(8);
        else if(chance(50)) circle(4);
        else out.waitFrames=10;
    }
    return out;
}
}
