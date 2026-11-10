#include "ai_impl.hpp"
#include "ai_combat_observation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
using debug_ai::position;
using debug_ai::life;
void DebugAi::Impl::releaseCombat(Context &c) {
    if(battle.combatOwned) {
        OnlineCombatCommand stop;stop.action=OnlineCombatCommand::Action::Stop;
        if(!c.combat.submit(stop)) note("combat-release-unavailable",c.combat.reason());
    }
    battle.combatOwned=false;battle.combatRun.repeating=false;
}
void DebugAi::Impl::observeCombat(Context &c) {
    const auto now=Clock::now();const auto &world=c.observation.read().world;
    // Reuse only an actually observed animation, under the same player,
    // equipment and state inputs. A stale current-cast object cannot refresh
    // the estimate after those inputs change.
    const auto player=world.units.find({0,c.observation.read().load.playerUnitId.value_or(0)});
    if(c.supplies && c.supplies->navigationFasterCast && c.supplies->observedCastSeconds && player!=world.units.end()) {
        const auto sample=c.supplies->observedCastSeconds(definitions->skill("Teleport"));
        const auto key=std::tuple{c.observation.read().gameGeneration,player->first.id,player->second.assignmentRevision,
            player->second.appearanceRevision,player->second.stateSequence,world.itemRevision,
            c.supplies->navigationWeaponSet,*c.supplies->navigationFasterCast};
        if(navigation.teleportTiming.key!=key) {
            navigation.teleportTiming.key=key;navigation.teleportTiming.seconds.reset();
            if(sample) navigation.teleportTiming.revision=sample->first;
        } else if(sample && sample->first!=navigation.teleportTiming.revision) {
            navigation.teleportTiming.revision=sample->first;
            if(std::isfinite(sample->second) && sample->second>0) navigation.teleportTiming.seconds=sample->second;
        }
    } else {navigation.teleportTiming.key.reset();navigation.teleportTiming.seconds.reset();}
    if(c.supplies && c.supplies->maxLife) battle.combatMaxLife=c.supplies->maxLife;
    if(world.life && (battle.combatLifeSamples.empty() || now-battle.combatLifeSamples.back().at>=debugInputRetryInterval))
        battle.combatLifeSamples.push_back({now,*world.life});
    while(!battle.combatLifeSamples.empty() && (battle.combatLifeSamples.size()>32 || now-battle.combatLifeSamples.front().at>std::chrono::seconds(4)))
        battle.combatLifeSamples.pop_front();
    if(!battle.combatTarget) return;
    const auto unit=world.units.find(*battle.combatTarget);
    if(unit==world.units.end() || unit->second.assignmentRevision!=battle.combatRun.assignment) return;
    const auto health=life(unit->second);
    if(health) {
        if(battle.combatRun.lastLife && *health>*battle.combatRun.lastLife) battle.combatRun.healingObserved=true;
        if(!battle.combatRun.bestLife) battle.combatRun.bestLife=health;
        else if(*health<*battle.combatRun.bestLife) {
            battle.combatRun.bestLife=health;battle.combatRun.progressAt=battle.combatRun.strategyAt=now;
            if(battle.combatRun.suspendedAt!=Clock::time_point{}) battle.combatRun.suspendedAt=now;
            battle.combatRun.repositioned=false;note("combat-net-life-progress",std::to_string(*health));
        }
        battle.combatRun.lastLife=health;
    }
    if(unit->second.hitRevision>battle.combatRun.hitRevision) {battle.combatRun.hitRevision=unit->second.hitRevision;++battle.combatRun.hits;}
}
bool DebugAi::Impl::combat(Context &c,const Goal &goal,bool nearbyDefense,bool planningDefense,
        const RouteCombatTask *task) {
    const auto &view=c.observation.read();const auto now=Clock::now();
    if(!policy.enabled || c.town.read().town || view.world.npcRequested || view.world.waypointSource) return false;
    if(!battle.excursionSkill) throw std::runtime_error("No configured attack met the departure skill requirements");
    if(nearbyDefense && (view.world.npcConversation || view.world.waypointRequested || view.world.staffSource ||
        view.world.townPortalPending || onlineHasCursorItem(view.world.items,view.load.playerUnitId))) return false;
    std::erase_if(battle.bypassedThreats,[&](const auto &entry) {
        const auto unit=view.world.units.find(entry.first.first);
        if(unit==view.world.units.end() || unit->second.assignmentRevision!=entry.first.second || onlineMonsterCorpse(unit->second)) return true;
        // Elapsed time alone cannot repair the same failed attack angle.
        // Let navigation advance before another optional engagement, or retry
        // when the actual assigned target has moved to a different position.
        return now>=entry.second.retryAt && (((c.position-entry.second.observer).length()>=8 &&
            (c.position-entry.second.target).length()>policy.engage+policy.retreat) ||
            (unit->second.position && (position(*unit->second.position)-entry.second.target).length()>=4));
    });
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    const Vec offset=origin?position(*origin):Vec{};
    CombatSight sight{map && origin?&map->grid:nullptr,offset,combatAttackMask(c,*battle.excursionSkill)};
    CombatSelection selection;
    if(!selectCombatTarget(c,goal,nearbyDefense,planningDefense,task,now,sight,selection)) return false;
    const auto &[enemies,chosen,elite,required]=selection;
    bool boss=false;
    if(const auto id=c.combat.monsterClass(*chosen))
        if(const auto row=findCompiledMonsterRow(definitions->monsters,*id)) boss=definitions->monsters.number(*row,"boss").value_or(0)!=0;
    const auto support=staticFieldOpportunity(c,boss);
    if(beginEngagement(c,*chosen,planningDefense,task,now)) return true;
    observeCombat(c);
    auto &run=battle.combatRun;const Vec targetPosition=position(*chosen->position);
    const auto attack=selectAttackStrategy(c,selection,task,boss,now,sight);
    const auto aura=attack.aura;const auto skill=attack.skill;const auto row=attack.row;
    const auto window=std::chrono::seconds(policy.noProgressSeconds);
    bool ray=sight.clear(c.position,targetPosition);
    const auto close=std::count_if(enemies.begin(),enemies.end(),[&](const auto &unit){return (position(*unit.position)-c.position).length()<=policy.radius;});
    const auto healing=potionCount(c,PotionKind::Life)+potionCount(c,PotionKind::Rejuvenation);
    const auto manaPotions=potionCount(c,PotionKind::Mana)+potionCount(c,PotionKind::Rejuvenation);
    int lifePercent=-1,lossPercent=0;
    if(battle.combatMaxLife && *battle.combatMaxLife>0 && view.world.life) {
        lifePercent=int(uint64_t(*view.world.life)*100/unsigned(*battle.combatMaxLife));
        uint16_t peak=*view.world.life;for(const auto &sample:battle.combatLifeSamples) peak=std::max(peak,sample.life);
        lossPercent=int(uint64_t(peak-*view.world.life)*100/unsigned(*battle.combatMaxLife));
    }
    battle.combatDiagnostics={{"target",{{"type",chosen->key.type},{"id",chosen->key.id},{"assignment",run.assignment},
        {"position",{chosen->position->x,chosen->position->y}},{"life",run.lastLife?Json(*run.lastLife):Json(nullptr)},
        {"bestLife",run.bestLife?Json(*run.bestLife):Json(nullptr)}}},
        {"required",required},{"figurineCandidate",elite && elite->key==chosen->key},{"lineClear",ray},{"lifeFeedbackKnown",run.lastLife.has_value()},
        {"healingObserved",run.healingObserved},{"hitFeedbackCount",run.hits},
        {"playerLifePercent",lifePercent<0?Json(nullptr):Json(lifePercent)},{"recentLifeLossPercent",lossPercent},
        {"mana",view.world.mana?Json(*view.world.mana):Json(nullptr)},{"healingPotions",healing},{"manaPotions",manaPotions},
        {"closeEnemies",close},{"groupEnemies",attack.groupEnemies},{"bossFromTable",boss},{"skillIndex",run.skillIndex},{"skills",run.skills},
        {"casts",run.casts},{"noNetProgressMs",std::chrono::duration_cast<std::chrono::milliseconds>(now-run.progressAt).count()},
        {"strategyCasts",run.strategyCasts},
        {"attempts",run.attempts},{"obstacle",run.obstacle},{"status","assess"},
        {"departureSkill",*battle.excursionSkill},{"staticFieldRank",support.rank},{"staticFieldRadius",support.range?Json(*support.range):Json(nullptr)},
        {"staticFieldCasts",run.staticCasts},{"staticFieldFloorPercent",support.floorKnown?Json(support.floor):Json(nullptr)},
        {"targetAbsoluteLife",nullptr},{"staticFieldAbsoluteStop","unavailable: native monster life is a ratio"}};
    const auto obstruct=[&](std::string detail) {
        if(!run.lastLife) detail+="; target life feedback unavailable";
        battle.combatDiagnostics["status"]="blocked";battle.combatDiagnostics["obstacle"]=detail;battle.combatDiagnostics["attempts"]=run.attempts;
        releaseCombat(c);
        if(task) throw std::runtime_error("Route combat blocked on assigned unit "+std::to_string(chosen->key.id)+": "+detail);
        cancelWalk(c);
        // Lack of net life progress is not proof of immunity. Preserve all
        // observations and the actual scene; never skip a required kill.
        if(required) throw std::runtime_error("Combat blocked on assigned unit "+std::to_string(chosen->key.id)+": "+detail+
            "; obstruction, misses, recovery or insufficient feedback remain possible");
        battle.bypassedThreats[{chosen->key,chosen->assignmentRevision}]={now+std::chrono::seconds(10),c.position,targetPosition};
        note("unproductive-threat-bypassed",detail);battle.combatTarget.reset();battle.retreatPoint.reset();return false;
    };
    if(!run.waitingForDelay && now-run.progressAt>=std::chrono::seconds(policy.stallTimeoutSeconds))
        return obstruct("local combat execution budget exhausted; last obstacle: "+run.obstacle);
    if(battle.retreatPoint && !navigation.passageDefending && (!task || task->preferAreaAttack)) {
        if(c.walk.observe().state!=DebugWalkState::Failed && now<battle.retreatUntil && (position(*battle.retreatPoint)-c.position).length()>2 &&
            tacticalPositionAllowed(c,position(*battle.retreatPoint)) && threatClearance(visibleThreats(c),position(*battle.retreatPoint))>=0) {
            battle.combatDiagnostics["status"]="retreat";wait("retreat","Following the known safe retreat while observing life and supplies");
            walkPoint(c,*battle.retreatPoint);mobility(c);return true;
        }
        if(c.walk.observe().state==DebugWalkState::Failed) note("retreat-unavailable",c.walk.observe().reason);
        cancelWalk(c);battle.retreatPoint.reset();
    }
    const auto localThreats=travelLandingThreats(c);
    const bool singleThreat=(targetPosition-c.position).length()<=policy.engage && (ray || close>0 || lossPercent>=policy.lifeDropPercent);
    const bool lowLife=lifePercent>=0 && lifePercent<=policy.retreatLifePercent;
    const bool falling=lossPercent>=policy.lifeDropPercent;
    const bool scarce=singleThreat && (required || boss) && !healing && lifePercent>=0 && lifePercent<70;
    if(!navigation.passageDefending && (!task || task->preferAreaAttack) && (close>=policy.crowd || (singleThreat && (lowLife || falling || scarce))) && now>=battle.retreatReady) {
        const auto safe=selectCombatPosition(c,selection,sight,localThreats,true,combatRange(c,skill));
        battle.retreatReady=now+std::chrono::seconds(4);
        if(safe) {
            releaseCombat(c);cancelWalk(c);run.reposition.reset();run.kitePoint.reset();run.kiteAfterCast=false;
            battle.retreatPoint=safe;battle.retreatUntil=now+std::chrono::seconds(2);
            battle.combatDiagnostics["status"]="retreat";note("retreat-started",lowLife?"low life":falling?"observed life loss":scarce?"single threat and scarce healing":"crowd");
            walkPoint(c,*safe);mobility(c);return true;
        }
        run.obstacle="survival pressure with no safe known retreat";battle.combatDiagnostics["obstacle"]=run.obstacle;
        if(lifePercent>=0 && lifePercent<=supplyPolicy.criticalLifePercent && !healing) return obstruct(run.obstacle);
    }
    if(run.reposition && !navigation.passageDefending) {
        const auto state=c.walk.observe().state;const bool reached=(position(*run.reposition)-c.position).length()<=1;
        const bool failed=!reached && (state==DebugWalkState::Failed || state==DebugWalkState::Completed || now>=run.approachUntil);
        if(!failed && !reached) {
            if(!c.walk.active()) walkPoint(c,*run.reposition);
            battle.combatDiagnostics["status"]="reposition";wait("combat-reposition","Changing an observed attack angle through a complete known path");mobility(c);return true;
        }
        run.obstacle=failed?"attack position was not reached":"new attack position reached; awaiting effect";
        run.reposition.reset();cancelWalk(c);run.strategyAt=failed?now-window:now;
        if(!failed) run.strategyCasts=0;
    }
    if(cooldownAttack(c,*chosen,attack,support,ray,planningDefense,task,now)) return true;
    // Resume a ready primary from a valid firing position before continuing
    // optional circling. Survival retreats above retain their priority.
    if(!run.waitingForDelay && ray && (targetPosition-c.position).length()<=combatRange(c,skill)) {
        if(run.kitePoint) {cancelWalk(c);run.kitePoint.reset();}
        run.kiteAfterCast=false;
    }
    if(run.kitePoint && !navigation.passageDefending) {
        const bool reached=(position(*run.kitePoint)-c.position).length()<=2;
        const auto field=visibleThreats(c);
        const bool failed=!reached && (now>=run.kiteUntil || c.walk.observe().state==DebugWalkState::Failed || !tacticalPositionAllowed(c,position(*run.kitePoint)) ||
            threatClearance(field,position(*run.kitePoint))<0);
        if(!reached && !failed) {
            if(!c.walk.active()) walk(c,{.goal=DebugWalkGoal::Point,.point=OnlinePoint{run.kitePoint->x,run.kitePoint->y},.interact=false,.purpose="combat-kite"});
            battle.combatDiagnostics["status"]="kite";wait("combat-kite","Circling the observed group outside its close threats after the completed cast");mobility(c);return true;
        }
        cancelWalk(c);run.kitePoint.reset();
        if(failed) run.orbitDirection=-run.orbitDirection;
        note(failed?"combat-kite-replan":"combat-kite-arrived");
        // Ordinary movement never resets the damage/effect recovery window.
        return true;
    }
    if(c.supplies && c.supplies->castInputBusy.value_or(false)) {
        battle.combatDiagnostics["status"]="attack-animation";return true;
    }
    const auto unavailable=attackQualification(c,*chosen,skill,bool(aura));
    if(!unavailable.empty()) {
        releaseCombat(c);run.obstacle=unavailable;
        battle.combatDiagnostics["status"]="qualification-wait";
        battle.combatDiagnostics["obstacle"]=unavailable;
        wait("combat-resources","Retaining departure skill; waiting for its actual qualification / resources");return true;
    }
    // Use the ordinary stationary left attack whenever the actual skill allows
    // it, leaving the right hand available for movement or the active aura.
    // Right-only attacks still use their permitted hand.
    const auto hand=knownSkill(c,skill,true)?OnlineSkillHand::Left:OnlineSkillHand::Right;
    sight.mask=combatAttackMask(c,skill);ray=sight.clear(c.position,targetPosition);
    battle.combatDiagnostics["lineClear"]=ray;
    const int range=skill==definitions->staticFieldSkill && support.range?std::max(2,*support.range-2):
        combatRange(c,skill);
    battle.combatDiagnostics["attackRange"]=range;
    if(((navigation.pendingWaypoint || (navigation.pendingDoor && !nearbyDefense)) && (!ray || (targetPosition-c.position).length()>range)) ||
        (view.world.combatRequest && view.world.combatRequest->state==OnlineCombatRequest::State::Pending)) {
        // A real opening/selection/animation wait has its own contract and
        // deadline. It is not an unsuccessful damage observation window.
        run.strategyAt=now;battle.combatDiagnostics["status"]="normal-action-wait";
        wait("combat-action-wait","Observing the current native action before judging attack effectiveness");return true;
    }
    if(!navigation.passageDefending && !run.waitingForDelay && now-run.strategyAt>=window && now>=run.nextCast && run.strategyCasts && ray && (targetPosition-c.position).length()<=range) {
        if(!run.repositioned && (!task || task->preferAreaAttack)) {
            releaseCombat(c);const auto at=selectCombatPosition(c,selection,sight,localThreats,false,range);
            run.repositioned=true;
            if(at) {
                run.triedPositions.emplace(at->x,at->y);run.reposition=at;run.approachUntil=now+std::chrono::seconds(8);
                cancelWalk(c);walkPoint(c,*at);battle.combatDiagnostics["status"]="reposition";note("combat-effect-reposition","No new low life value; reassessing line and attack angle");mobility(c);return true;
            }
            run.obstacle=ray?"no different reachable firing position":"blocked line and no reachable clear firing position";
        }
        if(selectFallbackAttack(c,*chosen,attack,now)) return true;
        return obstruct(run.obstacle.empty()?"no observed net life progress after configured attack and position reassessment":run.obstacle);
    }
    if((targetPosition-c.position).length()>range || !ray) {
        if(navigation.passageDefending || planningDefense) return false;
        releaseCombat(c);
        if(!nearbyDefense && !navigation.pendingDoor && !navigation.pendingWaypoint && openDoor(c)) return true;
        run.obstacle=ray?"outside attack range":"attack ray obstructed";
        battle.combatDiagnostics["status"]="approach";
        approachEnemy(c,*chosen,"assigned combat target",range);
        mobility(c);
        return true;
    }
    run.approachUntil={};
    // Retail St11/Do19 owns the held Inferno / Arctic Blast sequence.
    const bool channelled=definitions->skills.number(row,"srvstfunc")==11 && definitions->skills.number(row,"srvdofunc")==19;
    const bool rangedKite=!navigation.passageDefending && (!task || task->preferAreaAttack) && policy.rangedKite && range>3 && skill!=definitions->staticFieldSkill && !channelled;
    if(rangedKite && run.kiteAfterCast && now>=run.nextKite) {
        const auto destination=selectKitePosition(c,selection,sight,localThreats,range);
        run.kiteAfterCast=false;run.nextKite=now+std::chrono::milliseconds(policy.kiteIntervalMs);
        if(destination) {
            releaseCombat(c);cancelWalk(c);run.kitePoint=destination;run.kiteUntil=now+std::chrono::seconds(4);
            battle.combatDiagnostics["status"]="kite";battle.combatDiagnostics["kiteDestination"]={destination->x,destination->y};
            note("combat-kite-selected",battle.combatDiagnostics["kiteAssessment"].dump());
            walk(c,{.goal=DebugWalkGoal::Point,.point=OnlinePoint{destination->x,destination->y},.interact=false,.purpose="combat-kite"});
            mobility(c);return true;
        }
        if(battle.combatDiagnostics["kiteAssessment"].value("result",std::string{})=="hold-effective-position")
            note("combat-kite-held",battle.combatDiagnostics["kiteAssessment"].dump());
        else {run.orbitDirection=-run.orbitDirection;note("combat-kite-unavailable",battle.combatDiagnostics["kiteAssessment"].dump());}
    }
    return submitDirectAttack(c,*chosen,attack,nearbyDefense,planningDefense,rangedKite,hand,now);
}
} // namespace d2x
