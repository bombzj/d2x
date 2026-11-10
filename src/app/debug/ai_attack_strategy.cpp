#include "ai_impl.hpp"
#include "ai_combat_observation.hpp"
#include "content/skills/skill_eligibility.hpp"
#include "gameplay/items/equipment_rules.hpp"

namespace d2x {
using debug_ai::position;
using debug_ai::point;
using debug_ai::life;
bool DebugAi::Impl::attackRankQualified(const Context &c,uint16_t skill,bool left) const {
    const auto minimum=policy.minimumSkillLevels.find(skill);
    const unsigned required=minimum==policy.minimumSkillLevels.end()?1:minimum->second;
    return knownSkill(c,skill,left) && std::any_of(c.combat.skills().begin(),c.combat.skills().end(),[&](const auto &entry) {
        return entry.id==skill && std::max(unsigned(entry.level),entry.innate?1u:0u)>=required;
    });
}
void DebugAi::Impl::prepareCombatSkill(const Context &c) {
    if(!policy.enabled || (!c.town.read().town && battle.excursionSkillPrepared)) return;
    const auto &view=c.observation.read();
    const auto actor=view.world.units.find({0,view.load.playerUnitId.value_or(0)});
    std::array<std::optional<SkillHandEquipment>,2> hands;
    if(actor!=view.world.units.end() && actor->second.equipmentObserved) {
        hands={SkillHandEquipment{},SkillHandEquipment{}};
        for(const auto &[id,item]:view.world.items) {
            if(item.mode!=1 || (item.body!=4 && item.body!=5) || !onlineItemOwnedBy(item,view.load.playerUnitId)) continue;
            const auto base=c.inventory.equipmentDefinition(item);
            auto &hand=hands[item.body-4];
            if(!base.known) {hand.reset();continue;}
            hand=SkillHandEquipment{true,(item.flags&0x4100u)!=0,false,base.types,base.shoots,{}};
        }
    }
    const bool left=bool(battleAura(c));
    const auto qualified=[&](uint16_t skill) {
        const auto row=definitions->skillRows.find(skill);
        if(row==definitions->skillRows.end() || !attackRankQualified(c,skill,left)) return false;
        auto equipment=loadSkillEligibilityMetadata(definitions->skills,row->second);
        equipment.noAmmo=true; // Ammunition is a supply, not a weapon type.
        return skillEquipmentReady(equipment,hands).value_or(true);
    };
    // Cooldown, consumable supplies and a particular target never select
    // a different departure attack.
    std::optional<uint16_t> selected;
    const auto primary=policy.skill.value_or(definitions->skill("Attack"));
    if(qualified(primary)) selected=primary;
    else for(const auto skill:policy.fallbackSkills) if(qualified(skill)) {selected=skill;break;}
    if(!battle.excursionSkillPrepared || battle.excursionSkill!=selected)
        note("combat-excursion-skill",selected?std::to_string(*selected):"no qualified configured attack");
    battle.excursionSkill=selected;battle.excursionSkillPrepared=true;
}
bool DebugAi::Impl::meleeAttack(const Context &c,uint16_t skill) const {
    if(skill!=definitions->skill("Attack")) {
        const auto row=definitions->skillRows.find(skill);
        return row!=definitions->skillRows.end() && definitions->skills.value(row->second,"range")=="h2h";
    }
    const auto &view=c.observation.read();
    for(const auto &[id,item]:view.world.items)
        if(item.mode==1 && (item.body==4 || item.body==5) && !(item.flags&0x4100u) &&
            onlineItemOwnedBy(item,view.load.playerUnitId) && !c.inventory.equipmentDefinition(item).shoots.empty()) return false;
    return true;
}
uint16_t DebugAi::Impl::combatAttackMask(const Context &c,uint16_t skill) const {
    const auto found=definitions->attackMasks.find(skill);
    if(skill==definitions->skill("Attack") && !meleeAttack(c,skill)) return uint16_t(4);
    return found==definitions->attackMasks.end()?uint16_t(4):found->second;
}
bool DebugAi::Impl::beginEngagement(Context &c,const OnlineUnit &chosen,bool planningDefense,const RouteCombatTask *task,Clock::time_point now) {
    const bool resurrected=battle.combatTarget==chosen.key && battle.combatRun.assignment==chosen.assignmentRevision &&
        battle.combatRun.bestLife==0 && life(chosen).value_or(0)>0;
    if(battle.combatTarget!=chosen.key || battle.combatRun.assignment!=chosen.assignmentRevision || battle.combatRun.skills.empty() || resurrected) {
        releaseCombat(c);if(!planningDefense) cancelWalk(c);battle.combatTarget=chosen.key;battle.combatRun={};
        battle.combatRun.assignment=chosen.assignmentRevision;battle.combatRun.progressAt=battle.combatRun.strategyAt=now;
        battle.combatRun.orbitDirection=(chosen.key.id&1)?1:-1;
        battle.combatRun.bestLife=battle.combatRun.lastLife=life(chosen);battle.combatRun.hitRevision=chosen.hitRevision;
        battle.combatRun.skills.push_back(*battle.excursionSkill);
        if(policy.groupSkill && policy.groupSkill!=battle.excursionSkill) battle.combatRun.skills.push_back(*policy.groupSkill);
        for(const auto skill:policy.fallbackSkills)
            if(std::find(battle.combatRun.skills.begin(),battle.combatRun.skills.end(),skill)==battle.combatRun.skills.end())
                battle.combatRun.skills.push_back(skill);
        if(navigation.travelPhase==TravelPhase::CrossWait && !navigation.legUnit) {navigation.travelPhase=TravelPhase::Route;navigation.legDestination.reset();}
        note(resurrected?"combat-target-revived":"combat-target",std::to_string(chosen.key.id));
    }
    if(battle.combatRun.suspendedAt!=Clock::time_point{}) {
        const auto suspended=now-battle.combatRun.suspendedAt;
        battle.combatRun.progressAt+=suspended;battle.combatRun.strategyAt+=suspended;
        if(battle.combatRun.approachUntil!=Clock::time_point{}) battle.combatRun.approachUntil+=suspended;
        battle.combatRun.suspendedAt={};battle.combatRun.delayObservedAt=now;
        note("combat-resumed","Continuing the same assigned target's recovery state");
    }
    if(!task && !planningDefense && !battle.combatRun.entryAssessed && teleportAvailable(c) &&
        (position(*chosen.position)-c.position).length()<=combatRange(c,*battle.excursionSkill)) {
        battle.combatRun.entryAssessed=true;
        if(beginCombatEntry(c,chosen)) return true;
    }
    return false;
}
std::string_view DebugAi::Impl::attackQualification(const Context &c,const OnlineUnit &chosen,uint16_t skill,bool aura) const {
    if(!attackRankQualified(c,skill,aura)) return "skill rank or hand below configured requirements";
    if(skill!=definitions->staticFieldSkill && !c.combat.skillTargetEligible(chosen,skill))
        return "native target eligibility currently rejects this skill";
    // A shared native delay does not disqualify an otherwise usable attack.
    if(!c.combat.skillDelayActive(skill) && c.supplies && c.supplies->skillUsable)
        if(const auto usable=c.supplies->skillUsable(skill);usable && !*usable)
            return "normal character projection reports unusable skill (resource / equipment / state)";
    return {};
}
void DebugAi::Impl::changeAttack(Context &c,size_t index,Clock::time_point now) {
    auto &run=battle.combatRun;
    releaseCombat(c);run.skillIndex=index;
    run.strategyAt=now;run.strategyCasts=0;run.kiteAfterCast=false;
}
DebugAi::Impl::AttackStrategy DebugAi::Impl::selectAttackStrategy(Context &c,const CombatSelection &selection,
        const RouteCombatTask *task,bool boss,Clock::time_point now,CombatSight &sight) {
    const auto &enemies=selection.enemies;const auto &chosen=selection.chosen;
    auto &run=battle.combatRun;const Vec targetPosition=position(*chosen->position);
    const auto aura=battleAura(c);
    const auto window=std::chrono::seconds(policy.noProgressSeconds);
    const bool hasGroup=policy.groupSkill && policy.groupSkill!=battle.excursionSkill;
    const size_t firstFallback=hasGroup?2:1;
    size_t groupEnemies=0;
    if(hasGroup && run.skillIndex<firstFallback && !run.groupExhausted && !boss) {
        const auto group=run.skills[1];
        sight.mask=combatAttackMask(c,group);
        const auto range=combatRange(c,group);
        if((targetPosition-c.position).length()<=range && sight.clear(c.position,targetPosition) && attackQualification(c,*chosen,group,bool(aura)).empty()) {
            groupEnemies=std::count_if(enemies.begin(),enemies.end(),[&](const auto &enemy) {
                const auto at=position(*enemy.position);
                return (at-targetPosition).length()<=policy.radius && (at-c.position).length()<=range &&
                    sight.clear(c.position,at) && c.combat.skillTargetEligible(enemy,group);
            });
        }
        if(run.skillIndex==1 && !run.waitingForDelay && run.strategyCasts && now-run.strategyAt>=window)
            run.groupExhausted=true;
    }
    if(run.skillIndex<firstFallback) {
        const size_t nextSkillIndex=!run.groupExhausted && groupEnemies>=size_t(task && task->preferAreaAttack?1:policy.crowd)?1:0;
        if(run.skillIndex!=nextSkillIndex) {
            changeAttack(c,nextSkillIndex,now);
            note(nextSkillIndex?"combat-group-attack":"combat-primary-attack",std::to_string(run.skills[nextSkillIndex]));
        }
    }
    const auto skill=run.skills[run.skillIndex];
    const auto row=definitions->skillRows.at(skill);
    sight.mask=combatAttackMask(c,skill);
    if(run.waitingForDelay && run.delayObservedAt!=Clock::time_point{}) {
        const auto elapsed=now-run.delayObservedAt;
        run.progressAt+=std::min(elapsed,now-run.progressAt);
        run.strategyAt+=std::min(elapsed,now-run.strategyAt);
    }
    run.delayObservedAt=now;
    const auto delay=c.combat.skillDelayFrames(skill);
    const bool delayed=c.combat.skillDelayActive(skill);
    run.waitingForDelay=delayed || (delay && *delay>0 && now<run.nextCast);
    return {aura,skill,row,firstFallback,groupEnemies,delay,delayed};
}
bool DebugAi::Impl::selectFallbackAttack(Context &c,const OnlineUnit &chosen,const AttackStrategy &attack,Clock::time_point now) {
    auto &run=battle.combatRun;
    for(size_t index=std::max(run.skillIndex+1,attack.firstFallback);index<run.skills.size();++index) {
        const auto alternate=run.skills[index];
        if(!attackQualification(c,chosen,alternate,bool(attack.aura)).empty()) continue;
        changeAttack(c,index,now);run.repositioned=false;run.triedPositions.clear();
        note("combat-fallback-attack",std::to_string(alternate));
        battle.combatDiagnostics["status"]="fallback";
        wait("combat-fallback","Trying the next qualified configured attack after observed casts and position reassessment");
        return true;
    }
    return false;
}
bool DebugAi::Impl::submitDirectAttack(Context &c,const OnlineUnit &chosen,const AttackStrategy &attack,
        bool nearbyDefense,bool planningDefense,bool rangedKite,OnlineSkillHand hand,Clock::time_point now) {
    const auto &view=c.observation.read();auto &run=battle.combatRun;
    const auto aura=attack.aura;const auto skill=attack.skill;const auto row=attack.row;
    const auto delay=attack.delay;const bool delayed=attack.delayed;
    battle.combatDiagnostics["skill"]=skill;battle.combatDiagnostics["hand"]=hand==OnlineSkillHand::Left?"left":"right";
    battle.combatDiagnostics["skillDelayFrames"]=delay?Json(*delay):Json(nullptr);
    battle.combatDiagnostics["status"]=delayed?"native-skill-delay":"attack";
    if(delayed || now<run.nextCast || (c.supplies && c.supplies->castInputBusy.value_or(false)) ||
        (view.world.combatRequest && view.world.combatRequest->state==OnlineCombatRequest::State::Pending)) return true;
    if(aura && !selectSkill(c,*aura,OnlineSkillHand::Right)) {wait("combat-aura","Waiting for the normal battle aura selection");return true;}
    if(!selectSkill(c,skill,hand)) {wait("combat-select","Waiting for normal attack skill selection");return true;}
    // A one-shot from the current feet leaves the travel child and its exit
    // intent intact. Actual evasive movement still owns its ordinary child.
    if(!planningDefense) cancelWalk(c);
    OnlineCombatCommand cast;cast.action=OnlineCombatCommand::Action::Cast;
    cast.hand=hand;cast.target=chosen.key;cast.stationary=true;
    if(skill==definitions->staticFieldSkill) {cast.target.reset();cast.point=point(c.position);}
    // Same held pointer as the ordinary controller. Skills with a known
    // positive native delay use separate clicks, not a synthetic channel.
    cast.repeat=!nearbyDefense && !rangedKite && run.repeating && ((delay && *delay<=0) || definitions->skills.value(row,"delay").empty());
    if(submitCombat(c,cast,DebugActionOwner::Combat,"combat-cast",c.position)) {
        if(!run.strategyCasts) run.strategyAt=now;
        ++run.strategyCasts;
        if(planningDefense) navigation.mobilityHold=true;
        battle.combatOwned=true;run.repeating=true;++run.casts;++castRequests;battle.lastAttack=now;
        run.kiteAfterCast=rangedKite;
        run.nextCast=now+(delay && *delay>0?std::chrono::milliseconds(int64_t(*delay)*40):debugInputRetryInterval);
        wait("combat-cast","Observing actual life effect after a normal attack input");
    } else {
        run.obstacle=c.combat.reason();battle.combatDiagnostics["obstacle"]=run.obstacle;
        run.nextCast=now+debugInputRetryInterval;
    }
    return true;
}
} // namespace d2x
