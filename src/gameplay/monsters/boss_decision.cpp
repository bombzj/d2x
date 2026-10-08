#include "boss_decision.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
bool hasBossDecision(MonsterAiKind k) {
    return k==MonsterAiKind::Smith || k==MonsterAiKind::Griswold || k==MonsterAiKind::Andariel || k==MonsterAiKind::BloodRaven || k==MonsterAiKind::Countess;
}
std::optional<MonsterBossDecision> decideBossMonster(const MonsterAiProfile &profile,MonsterBossInput input) {
    if(!hasBossDecision(profile.kind)) return {};
    const auto &in=input.combat;const auto &p=profile.params;
    MonsterBossDecision result;auto &out=result.action;
    out.random=in.random;out.phase=in.phase;out.loop=in.loop;result.summonChance=input.summonChance;
    auto chance=[&](int n){return int(limitedRandom(out.random,100))<n;};
    auto attack=[&]{out.action=MonsterDecisionAction::Attack;};
    auto approach=[&](int stop=1,int speed=75,bool run=false){out.action=MonsterDecisionAction::Approach;out.stopDistance=stop;out.velocityPercent=speed;out.running=run;};
    auto spell=[&](int slot){out.action=MonsterDecisionAction::Special;out.skillSlot=slot;};
    if(profile.kind==MonsterAiKind::Smith) {if(in.contact) attack();else approach(1,75+(100-in.lifePercent)/2);}
    else if(profile.kind==MonsterAiKind::Griswold) {
        if(in.contact) {if(chance(80)) attack();else out.waitFrames=10;}
        else if(chance(50)) approach();else out.waitFrames=10;
    } else if(profile.kind==MonsterAiKind::Andariel) {
        if(in.contact) {if(chance(p[0])) spell(0);else attack();}
        else if(chance(p[1])) out.waitFrames=5;
        else if(chance(p[2])) spell(chance(p[3])?0:1);
        else approach();
    } else if(profile.kind==MonsterAiKind::Countess) {
        if(!input.sameHomeRoom || input.homeDistance>40) {
            if(input.homeDistance>0) {approach(0,75,true);result.returnHome=true;return result;}
            if(in.distance>=25) {out.waitFrames=10;return result;}
        }
        if(input.firewallReady) {spell(0);return result;}
        if(in.contact) {if(chance(p[2]+10)) attack();else out.waitFrames=p[1];}
        else if(chance(p[0])) approach(1,175,true);else out.waitFrames=p[1];
    } else {
        if(in.distance>45) {out.waitFrames=5;return result;}
        if(input.homeDistance>50 || input.targetHomeDistance>=50) out.phase=1;
        if(out.phase && input.homeDistance>5) {approach(0,175,true);result.returnHome=true;return result;}
        out.phase=0;
        if(in.distance>20 && input.targetHomeDistance<50) {approach(std::max(12,in.distance/2)-1,175,true);return result;}
        result.summonChance+=3;
        if(!in.contact && in.loop<8+2*in.difficulty && chance(result.summonChance)) {
            const int length=int(limitedRandom(out.random,15))+5;
            int x=0,y=0;
            if(rollRandom(out.random)&1) {x=length;y=int(limitedRandom(out.random,unsigned(length)));}
            else {x=int(limitedRandom(out.random,unsigned(length)));y=length;}
            if(rollRandom(out.random)&1) x=-x;
            if(rollRandom(out.random)&1) y=-y;
            result.summonOffset={float(x),float(y)};result.summonChance=0;++out.loop;spell(0);return result;
        }
        if(in.distance>5) {
            if(chance(5) && input.targetHomeDistance<50) {approach(11,175,true);return result;}
            if(in.clear && chance(80)) {if(chance(10*(in.difficulty+4))) spell(1);else attack();return result;}
            out.action=MonsterDecisionAction::Circle;out.stopDistance=4;out.velocityPercent=125;return result;
        }
        if(in.distance<12 && chance(30)) {out.action=MonsterDecisionAction::Retreat;out.stopDistance=12-in.distance;out.velocityPercent=175;out.running=true;out.attackWhenMoveFails=true;}
        else attack();
    }
    return result;
}
}
