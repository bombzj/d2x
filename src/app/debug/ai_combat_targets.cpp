#include "ai_impl.hpp"
#include "ai_combat_observation.hpp"

namespace d2x {
using debug_ai::position;
bool DebugAi::Impl::selectCombatTarget(Context &c,const Goal &goal,bool nearbyDefense,bool planningDefense,
        const RouteCombatTask *task,Clock::time_point now,const CombatSight &sight,CombatSelection &selection) {
    const auto &view=c.session.read();
    const QuestStep *step=cursor<goals.size() && goals[cursor].kind==Kind::Quest && questState.questRun.step<goals[cursor].steps.size()?
        &goals[cursor].steps[questState.questRun.step]:nullptr;
    const bool targetedDrop=step && step->action==QuestAction::SearchDrop && step->target &&
        goal.kind==Kind::Quest && step->area==area;
    const auto objective=!nearbyDefense && (goal.kind==Kind::Hunt || targetedDrop) && goal.target?find(c,*goal.target):std::nullopt;
    const bool nearObjective=objective && (position(*objective->position)-c.position).length()<=policy.engage;
    // Permission to fight for a drop is not a requirement to kill every
    // nearby monster. Only real clear objectives and the active encounter
    // make an unrelated enemy's defeat a necessary postcondition.
    const bool clearing=!nearbyDefense && step && goal.area==area && (step->action==QuestAction::Clear ||
        (step->action==QuestAction::Encounter && step->clearCombat && questState.questRun.encounterPosition &&
         questState.questRun.encounterArea==area && (position(*questState.questRun.encounterPosition)-c.position).length()<=encounterMovementRadius()));
    const auto mustClear=[&](const OnlineUnit &unit) {return clearing && (step->action==QuestAction::Clear || encounterEnemy(c,unit));};
    const auto routeDrop=step?routeDropState(c,goals[cursor]):RouteDrop::None;
    const bool collecting=step && step->action==QuestAction::SearchDrop && (pickup || observedDrop(c,step->item));
    const bool seekingElite=!nearbyDefense && routeDrop==RouteDrop::Seek && !clearing && !targetedDrop && !questState.questRun.pending && !questState.questRun.worldWait &&
        !questTransaction(c) && !objectIssued && !pickup && !portalIssued;
    const bool fastTravel=(mobilityPolicy.mode=="teleport" || navigation.townExitArea || teleportAvailable(c,false)) && !clearing && !nearObjective;
    const bool walkingTransit=c.walk.active() && bool(navigation.walkIntent) && !clearing && !nearObjective &&
        !battle.retreatPoint && !battle.combatRun.reposition && !battle.combatRun.kitePoint &&
        !navigation.walkIntent.purpose.value_or(std::string{}).starts_with("combat-");
    if (!nearbyDefense && (collecting || ((fastTravel || walkingTransit) && !seekingElite))) {
        releaseCombat(c);
        battle.combatTarget.reset(); battle.retreatPoint.reset(); battle.combatRun = {};
        battle.combatDiagnostics = {{"status", collecting?"quest-drop-pickup":"travel-skip-optional-combat"}};
        return false;
    }
    auto &[enemies,chosen,elite,required]=selection;std::optional<OnlineUnit> obstructed;
    float eliteDistance=float(policy.engage);
    const bool elitesOnly=!nearbyDefense && (fastTravel || (step && goals[cursor].routeDrop==questState.questRun.step));
    bool defensiveDanger=false;
    if(nearbyDefense || policy.scope=="defensive") {
        size_t nearby=0;
        for(const auto &[key,unit]:view.world.units) {
            if(key.type==1 && unit.position && c.combat.hostile(unit) && c.combat.monsterTargetEligible(unit,false) &&
                (position(*unit.position)-c.position).length()<=policy.radius) ++nearby;
        }
        if(battle.combatMaxLife && *battle.combatMaxLife>0 && view.world.life) {
            uint16_t peak=*view.world.life;
            for(const auto &sample:battle.combatLifeSamples) peak=std::max(peak,sample.life);
            const auto health=uint64_t(*view.world.life)*100/unsigned(*battle.combatMaxLife);
            const auto loss=uint64_t(peak-*view.world.life)*100/unsigned(*battle.combatMaxLife);
            defensiveDanger=nearby>=size_t(policy.crowd) ||
                (nearby && (health<unsigned(policy.retreatLifePercent) || loss>=unsigned(policy.lifeDropPercent)));
        } else defensiveDanger=nearby>=size_t(policy.crowd);
        defensiveDanger|=planningDefense && c.walk.blockedByUnit() && nearby>0;
    }
    float nearest=clearing && step->action==QuestAction::Encounter && questState.questRun.worldWait?
        float(std::max(policy.engage,2*encounterMovementRadius())):float(policy.engage);
    float nearestObstructed=nearest;
    const int localAttackRange=task && task->preferAreaAttack?routeAttackRange(c):meleeAttack(c,*battle.excursionSkill)?3:policy.range;
    for(const auto &[key,unit]:view.world.units) {
        // A navigation interruption attacks only the observed obstruction
        // captured for this leg, never every nearby/on-screen monster.
        if(task && (!task->targets.contains(key) || task->targets.at(key)!=unit.assignmentRevision)) continue;
        if(key.type!=1 || !unit.position || !c.combat.hostile(unit) || !c.combat.monsterTargetEligible(unit,false) ||
            (!task && !mustClear(unit) && (!objective || objective->key!=key) && battle.bypassedThreats.contains({key,unit.assignmentRevision}))) continue;
        if(!task && !defensiveDanger && (nearbyDefense || policy.scope=="defensive") && !mustClear(unit) &&
            (!objective || objective->key!=key) && !seekingElite) continue;
        const float distance=(position(*unit.position)-c.position).length();
        if(!task && nearbyDefense && distance>policy.radius && (planningDefense || key!=battle.combatTarget) &&
            !(view.load.playerUnitId && unit.destinationUnit==OnlineUnitKey{0,*view.load.playerUnitId})) continue;
        const bool candidate=seekingElite && distance<=policy.engage && c.combat.jadeFigurineCandidate(unit);
        if(elitesOnly && !candidate && distance>policy.radius) continue;
        const bool visible=sight.clear(c.position,position(*unit.position));
        if((navigation.passageDefending || planningDefense) && (!visible || distance>localAttackRange)) continue;
        if(candidate && visible && (!elite || distance<eliteDistance)) {elite=unit;eliteDistance=distance;}
        if(mustClear(unit) && !visible && distance<nearestObstructed) {nearestObstructed=distance;obstructed=unit;}
        if(distance<=policy.engage && (visible || distance<=policy.radius)) enemies.push_back(unit);
        const bool byObjective=nearObjective && (key==objective->key ||
            (position(*unit.position)-position(*objective->position)).length()<=policy.radius);
        const bool relevant=nearbyDefense || policy.scope=="clear" || mustClear(unit) || byObjective || (policy.scope=="defensive" && distance<=policy.radius);
        if(!elitesOnly && relevant && (distance<=policy.engage || mustClear(unit)) && visible && distance<nearest) {nearest=distance;chosen=unit;}
    }
    // A stance change may leave the acquisition radius, and another enemy
    // may briefly be nearer. Finish the same valid engagement inside its
    // existing engage radius; the required objective still takes precedence.
    if(battle.combatTarget) for(const auto &unit:enemies) if(unit.key==battle.combatTarget &&
        unit.assignmentRevision==battle.combatRun.assignment && sight.clear(c.position,position(*unit.position))) {
        const bool byObjective=nearObjective && (unit.key==objective->key ||
            (position(*unit.position)-position(*objective->position)).length()<=policy.radius);
        if(!elitesOnly && (nearbyDefense || policy.scope=="clear" || mustClear(unit) || byObjective ||
           (policy.scope=="defensive" && (position(*unit.position)-c.position).length()<=policy.radius))) chosen=unit;
        if(seekingElite && c.combat.jadeFigurineCandidate(unit)) elite=unit;
        break;
    }
    if(elite) chosen=elite;
    if(nearObjective && c.combat.monsterTargetEligible(*objective,false)) chosen=objective;
    // A required clear may retain a real enemy behind a corner. Give the
    // firing-position recovery the same opportunity as a named hunt target;
    // navigating to its center alone can leave a ranged actor at the wall.
    if(!chosen && obstructed) chosen=obstructed;
    if(!chosen) {
        if(task) {
            releaseCombat(c);battle.combatTarget.reset();battle.combatRun={};
            battle.combatDiagnostics={{"status","route-target-not-actionable"}};return false;
        }
        if(battle.combatTarget || battle.combatOwned) {
            releaseCombat(c);battle.retreatPoint.reset();
            const auto previous=battle.combatTarget?view.world.units.find(*battle.combatTarget):view.world.units.end();
            if(!defensiveDanger && (nearbyDefense || policy.scope=="defensive") && !clearing && !nearObjective && !seekingElite) {
                battle.combatTarget.reset();battle.combatRun={};
                battle.combatDiagnostics={{"status","travel-threat-contained"}};
                note("combat-defense-released","Nearby pressure no longer requires stopping travel");return false;
            }
            if(previous!=view.world.units.end() && previous->second.assignmentRevision==battle.combatRun.assignment &&
                !onlineMonsterCorpse(previous->second) && c.combat.hostile(previous->second)) {
                if(battle.combatRun.suspendedAt==Clock::time_point{}) {
                    battle.combatRun.suspendedAt=now;note("combat-suspended","Retaining the same assigned target's recovery state; releasing input");
                }
                battle.combatDiagnostics["status"]="suspended";return false;
            }
            cancelWalk(c);battle.combatTarget.reset();battle.combatRun={};note("combat-clear");
        }
        battle.combatDiagnostics={{"status",clearing?"clear-awaiting-approach":"no-relevant-target"},
            {"clearing",clearing},{"engageRadius",policy.engage}};
        return false;
    }
    required=mustClear(*chosen) || (objective && chosen->key==objective->key);
    return true;
}
} // namespace d2x
