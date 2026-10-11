#include "act2_decision.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
bool hasAct2Decision(MonsterAiKind kind) {return kind>=MonsterAiKind::SandRaider && kind<=MonsterAiKind::ArcaneTower;}
Act2Decision decideAct2Monster(const MonsterAiProfile &rule,const Act2DecisionInput &input) {
    const auto &i=input.combat;const auto &p=rule.params;
    Act2Decision result;result.deadline=input.deadline;result.novaAt=input.novaAt;result.firewallAt=input.firewallAt;
    result.attempts=input.attempts;
    auto &d=result.action;d.random=i.random;d.phase=i.phase;d.loop=i.loop;d.charged=i.charged;d.alerted=i.alerted;d.waitFrames=10;
    const auto chance=[&](int percent){return int(limitedRandom(d.random,100))<percent;};
    const auto attack=[&](int mode=4){d.action=MonsterDecisionAction::Attack;d.attackMode=uint8_t(mode);};
    const auto cast=[&](int slot){d.action=MonsterDecisionAction::Special;d.skillSlot=slot;};
    const auto approach=[&](int velocity=75){d.action=MonsterDecisionAction::Approach;d.stopDistance=rule.meleeRange;d.velocityPercent=velocity;};
    const auto circle=[&](int radius){d.action=MonsterDecisionAction::Circle;d.stopDistance=radius;};
    const auto retreat=[&](int radius){d.action=MonsterDecisionAction::Retreat;d.stopDistance=radius;};
    if(!input.hasTarget && rule.kind!=MonsterAiKind::MaggotEgg && rule.kind!=MonsterAiKind::SandMaggotQueen &&
       rule.kind!=MonsterAiKind::SandMaggot && rule.kind!=MonsterAiKind::BatDemon && rule.kind!=MonsterAiKind::Vulture) return result;
    switch(rule.kind) {
    case MonsterAiKind::ArcaneTower:
        if(input.tick<input.deadline) break;
        if(!d.loop) d.loop=p[0];
        if(d.phase) {attack();if(--d.loop>0) result.deadline=input.tick+uint64_t(p[5]);else {d.phase=0;d.loop=p[0];result.deadline=input.tick+uint64_t(p[4]);}}
        else {cast(0);if(--d.loop>0) result.deadline=input.tick+uint64_t(p[1]);else {d.phase=1;d.loop=p[3];result.deadline=input.tick+uint64_t(p[2]);}}
        break;
    case MonsterAiKind::SandRaider:
        if(!d.phase) {result.color=0;d.charged=false;}
        ++d.phase;
        if(d.phase==p[4]) {result.overlay=p[5]==1?150:46;d.waitFrames=1;break;}
        if(d.phase>p[4]) {d.charged=true;result.color=p[5]==1?1:2;}
        if(d.loop<7 && i.lifePercent<p[0]) {
            if(input.hasAlly) {result.seekAlly=true;approach();break;}
            ++d.loop;
        }
        if(i.distance>4 && !d.charged && chance(p[1])) circle(0);
        else if(i.contact) {
            if(d.charged) {cast(0);d.phase=0;d.charged=false;}
            else if(chance(p[2])) attack(chance(p[6])?5:4);
        } else if(d.charged || chance(p[3])) {approach();break;}
        if(d.action==MonsterDecisionAction::Attack || d.action==MonsterDecisionAction::Special || d.action==MonsterDecisionAction::Circle) break;
        if(d.phase>std::max(24-p[4],6)+p[4]) {d.phase=0;d.charged=false;}
        d.waitFrames=15;
        break;
    case MonsterAiKind::ClawViper:
        // Fn016 explicitly clears the selected tint on both paths.
        if(d.phase && p[5]) result.color=0;
        if(i.contact) {if(chance(p[2])) attack(chance(p[3])?4:5);else d.waitFrames=p[4];}
        else if(i.distance<p[1] && chance(p[0]) && i.clear) {cast(0);d.phase=1;if(p[5]) result.color=0;}
        else if(chance(50)) approach();else d.waitFrames=p[4];
        break;
    case MonsterAiKind::SandLeaper:
        if(i.distance<5 && chance(p[0])) cast(0);
        else if(i.contact) {if(chance(p[1])) attack(5);}
        else if(i.distance>10) {approach();d.approachRadius=5;}
        else if(chance(p[2])) approach();else if(chance(p[3])) circle(4);
        break;
    case MonsterAiKind::Scarab:
        if(!i.commanded && i.leader && i.distance<20 && chance(p[4])) {d.commandParty=true;d.charged=true;}
        if(i.commanded || d.charged) {if(i.contact) {cast(0);d.charged=false;}else approach(100);}
        else if(!i.contact) {if(i.phase) {approach();if(!chance(10)) d.phase=0;}else {circle(0);d.phase=1;}}
        else if(chance(p[0])) {if(chance(p[3])) cast(0);else attack(chance(p[1])?4:5);}else d.waitFrames=p[2];
        break;
    case MonsterAiKind::PinHead:
        if(i.contact) {
            if(i.phase && !chance(p[0])) d.waitFrames=p[1];
            else {d.phase=1;if(chance(p[4])) cast(0);else attack();}
        } else {d.phase=0;if(chance(p[2])) approach();else d.waitFrames=p[3];}
        break;
    case MonsterAiKind::GreaterMummy:
        if(i.contact && chance(p[0])) attack();
        else if(i.distance<5 && chance(p[0])) attack(5);
        else if(input.heal && chance(p[2])) cast(1);
        else if(input.corpse && chance(p[1])) cast(0);
        else if(chance(p[3]) && i.clear) cast(2);
        else if(!input.supportCount) {approach(50);d.pathSteps=3;}
        else if(chance(50)) circle(3);else d.waitFrames=6;
        break;
    case MonsterAiKind::SandMaggot:
        if(input.burrowed) {if((input.hasTarget || i.distance<16) && input.tick>input.deadline) {cast(0);d.phase=1;d.waitFrames=25;result.deadline=input.tick+uint64_t(p[4]);}else d.waitFrames=20;}
        else if((!input.hasTarget && i.distance>10 && input.tick>input.deadline) || (i.lifePercent<25 && i.distance<7 && input.tick>input.deadline && chance(20))) {cast(1);d.phase=3;d.waitFrames=30;result.deadline=input.tick+uint64_t(p[4]);}
        else if(i.contact && chance(p[3])) attack();
        else if(i.distance<15 && chance(p[1]) && i.clear) attack(5);
        else if(chance(20)) circle(6);
        else if(input.attempts>=p[2] || !chance(p[0])) d.waitFrames=12;
        else if(i.phase==2) {cast(2);d.phase=1;d.waitFrames=20;++result.attempts;}
        else {circle(6);d.phase=2;}
        break;
    case MonsterAiKind::MaggotEgg:
        d.waitFrames=p[0];if(i.phase==1) result.exhaust=true;else if(chance(p[1])) {cast(0);d.phase=1;}
        break;
    case MonsterAiKind::SandMaggotQueen:
        if(i.phase==2) {d.waitFrames=25*p[1];d.phase=0;}
        else if(i.phase==1) {result.spawnQueen=true;d.waitFrames=p[1];d.phase=2;}
        else if(input.born<p[0]) {attack(8);d.phase=1;d.waitFrames=p[1];}
        break;
    case MonsterAiKind::Sarcophagus:
        if(i.distance>25) d.waitFrames=25;
        else if(input.born<=p[2]) {if(input.tick>=input.deadline) {cast(0);result.deadline=input.tick+uint64_t(p[0]);}else d.waitFrames=20+int(limitedRandom(d.random,10));}
        else result.exhaust=true;
        break;
    case MonsterAiKind::Duriel:
        if(i.contact) {if(chance(p[1])) cast(2);else if(chance(p[2])) cast(1);else attack(chance(p[3])?5:4);}
        else if(chance(p[4])) cast(0);else approach();
        break;
    case MonsterAiKind::Summoner: {
        if(i.distance<5 && chance(p[5])) {d.action=MonsterDecisionAction::Retreat;d.stopDistance=6;}
        if(!chance(p[0])) {circle(4);break;}
        if(chance(p[1])) {cast(4);break;}
        bool cold=input.preferCold;if(int(limitedRandom(d.random,100))>p[2]) cold=!cold;
        if(cold && input.tick>input.novaAt && i.distance<p[6]) {cast(1);result.novaAt=input.tick+uint64_t(p[3]);}
        else if(cold && i.clear && i.distance<p[7]) cast(0);
        else if(input.tick>input.firewallAt) {cast(3);result.firewallAt=input.tick+uint64_t(p[4]);}
        else if(i.clear && i.distance<p[7]) cast(2);
        else if(!cold && input.tick>input.novaAt && i.distance<p[6]) {cast(1);result.novaAt=input.tick+uint64_t(p[3]);}
        else cast(4);
        break;
    }
    // These families have no active Skills slots. Their ordinary attack entry
    // remains separate from the spell execution added in this skill pass.
    case MonsterAiKind::Swarm:
        if(i.contact) {if(chance(p[2])) attack();}else if(chance(p[0])) approach();
        if(d.action==MonsterDecisionAction::Idle) d.waitFrames=p[1];
        break;
    case MonsterAiKind::MaggotLarva:
        if(i.contact) {if(i.phase || !chance(p[0])) {d.phase=0;d.waitFrames=p[1];}else {d.phase=1;attack();}}
        else {d.phase=0;if(chance(p[2])) approach();else d.waitFrames=p[3];}break;
    case MonsterAiKind::Mummy:
        if(i.retaliating && !i.contact) approach();
        else if(i.distance>p[0]) {if(chance(p[1])) {d.action=MonsterDecisionAction::Wander;d.stopDistance=3;}else d.waitFrames=p[4];}
        else if(!i.contact) approach();else if(chance(p[2])) attack(chance(p[3])?4:5);else {approach();d.waitFrames=p[4];}
        break;
    case MonsterAiKind::PantherJavelin:
        if(i.distance<8 && chance(p[3])) {d.action=MonsterDecisionAction::Retreat;d.stopDistance=16;}
        else if(i.distance>p[5]-6 && chance(p[0])) {approach();d.stopDistance=4;}
        else if(i.distance>=p[5] || !i.clear) {if(input.hasAlly && input.allyDistanceSquared>p[2]) {result.seekAlly=true;approach();}else d.waitFrames=p[4];}
        else if(chance(p[1])) attack();else d.waitFrames=p[4];
        break;
    case MonsterAiKind::PantherWoman:
        if(i.contact) {if(chance(p[1])) attack();else d.waitFrames=p[3];}
        else if(chance(p[0])) approach();
        else if(input.hasAlly && int64_t(input.allyDistanceSquared)>int64_t(p[2])*p[2]) {result.seekAlly=true;approach();}
        else if(chance(25)) circle(3);else d.waitFrames=p[3];
        break;
    case MonsterAiKind::Baboon:
        if(i.phase) {
            --d.phase;d.loop=0;
            if(!d.phase || i.lifePercent>75) result.regenerationEighths=0;
            if(i.contact && chance(33)) {attack(chance(p[3])?4:5);break;}
            if(!i.contact && i.lifePercent>75) {d.phase=0;approach(75+rule.retreatVelocityBonus);d.running=true;break;}
            if(i.distance>=24 && !i.retaliating) {if(chance(33)) circle(4);d.waitFrames=20;break;}
            retreat(15);d.velocityPercent=75+rule.retreatVelocityBonus;d.running=true;
            d.attackWhenMoveFails=i.contact;d.failedMoveWanderRadius=i.contact?0:5;
        } else if(!i.contact) approach();
        else if(i.retaliating && i.lifePercent<p[0] && chance(50)) {
            d.phase=2+int(limitedRandom(d.random,5));result.regenerationEighths=p[4];retreat(15);d.velocityPercent=75+rule.retreatVelocityBonus;d.running=true;
        } else if(i.retaliating && !i.loop) {d.loop=1;attack(chance(p[3])?4:5);}
        else if(i.retaliating && chance(20)) {circle(3);d.loop=0;}
        else if(!i.loop || chance(p[2])) {d.loop=1;attack(chance(p[3])?4:5);}
        else if(chance(p[1])) {circle(3);d.loop=0;}else d.waitFrames=15;
        break;
    case MonsterAiKind::Vulture:
        d.waitFrames=12;
        if(!input.hasTarget || (!input.sameRoom && input.targetDistanceSquared>144)) {
            if(i.phase<1) {if(input.hasTarget) {approach();d.approachRadius=9;}break;}
            if(input.canLand) {result.pose=9;d.phase=-1;}else d.phase=8;
            break;
        }
        if(!i.phase && i.leader && input.targetDistanceSquared>144 && chance(60)) {
            d.phase=24+int(limitedRandom(d.random,8));result.pose=8;result.holdPose=true;result.flyMove=true;d.stopDistance=8;
            break;
        }
        if(i.phase>=1) {
            if(i.phase>1 && !input.woundedNearby && (i.distance>=6 || !chance(15))) {
                d.phase=i.phase-1;result.pose=8;result.holdPose=true;result.flyMove=true;break;
            }
            if(input.canLand) {result.pose=9;d.phase=-1;}else d.phase=8;
            break;
        }
        if(!i.contact) {
            if(i.phase!=-1 && !chance(p[4])) {d.waitFrames=p[1];break;}
            if(chance(p[3])) circle(6);else {approach();d.approachRadius=9;}
            d.phase=0;
        } else if(chance(p[0])) attack();else d.waitFrames=p[1];
        break;
    case MonsterAiKind::BatDemon:
        if(!i.phase || i.phase==4) {result.pose=10;d.phase=1;d.loop=0;result.regenerationEighths=p[4];d.waitFrames=8;break;}
        if(i.phase==1) {
            if(i.loop>1 && input.hasTarget && (i.contact || i.distance<7 || i.retaliating || (i.distance<14 && i.lifePercent>50))) {
                result.regenerationEighths=0;result.pose=9;d.phase=3;d.waitFrames=6;
            } else {result.pose=11;result.holdPose=true;d.waitFrames=i.loop?15:10;++d.loop;}
            break;
        }
        if(!input.hasTarget) {d.phase=4;d.waitFrames=15;break;}
        if(i.lifePercent<p[0] && !i.retreatBlocked) {retreat(15);d.phase=4;break;}
        if(i.phase==2) {
            if(chance(33)) {approach();d.phase=3;}
            else if(chance(15)) {d.action=MonsterDecisionAction::Wander;d.stopDistance=6;}
            else d.waitFrames=10;
            break;
        }
        if(i.contact) {
            if(i.loop>0) {attack(5);d.loop=0;}
            else if(i.retaliating && chance(p[1]) && !i.retreatBlocked) {retreat(12);d.phase=2;}
            else if(chance(p[2])) attack(chance(p[3])?5:4);else d.waitFrames=10;
        } else approach();
        break;
    default: break;
    }
    return result;
}
}
