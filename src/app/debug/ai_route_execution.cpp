#include "ai_impl.hpp"
#include <algorithm>

namespace d2x {
DebugAi::Impl::Json DebugAi::Impl::routeExecutionSnapshot() const {
    if(!navigation.routeExecution) return nullptr;
    const auto &task=*navigation.routeExecution;
    const char *mode=task.mode==RouteExecution::Mode::TeleportTransit?"teleport-transit":
        task.mode==RouteExecution::Mode::WalkingPassage?"walking-passage":"combat-entry";
    const char *phase=task.phase==RouteExecution::Phase::Relocate?"relocate":
        task.phase==RouteExecution::Phase::Engage?"engage":task.phase==RouteExecution::Phase::AwaitCast?"await-cast":"resume";
    Json targets=Json::array();for(const auto &[key,assignment]:task.combat.targets)
        targets.push_back({{"type",key.type},{"id",key.id},{"assignment",assignment}});
    return {{"mode",mode},{"phase",phase},{"area",task.area},{"landing",{task.landing.x,task.landing.y}},
        {"position",{task.position.x,task.position.y}},{"segmentFrom",{task.segmentFrom.x,task.segmentFrom.y}},
        {"segmentTo",{task.segmentTo.x,task.segmentTo.y}},{"targets",std::move(targets)},
        {"motion",battle.hazardDodge?"hazard-evade":battle.retreatPoint?"combat-retreat":battle.combatRun.reposition?"combat-reposition":
            battle.combatRun.kitePoint?"combat-kite":task.phase==RouteExecution::Phase::Relocate?"teleport-relocation":"held"},
        {"preferAreaAttack",task.combat.preferAreaAttack},{"attackSequence",task.attackSequence},
        {"relocated",task.relocated},{"retainedIntent",task.intent}};
}
bool DebugAi::Impl::beginTeleportTransit(Context &c,OnlinePoint next) {
    if(navigation.routeExecution) {
        if(navigation.routeExecution->phase!=RouteExecution::Phase::Resume) return false;
        // Permit the retained cast after its one tactical interruption. A
        // different next landing gets a fresh observation, even on the same leg.
        if(navigation.routeExecution->landing==next) return false;
        navigation.routeExecution.reset();
    }
    if(!area || !battle.excursionSkill || !bool(navigation.walkIntent) || !policy.enabled) return false;
    const Vec destination{float(next.x),float(next.y)};
    if(!crowdedTravelLanding(travelLandingThreats(c),destination)) return false;
    RouteExecution task;task.intent=navigation.walkIntent;task.area=*area;task.step=questState.questRun.step;
    task.landing=next;task.position=next;
    task.deadline=Clock::now()+std::chrono::seconds(eventPolicy.waitSeconds);
    for(const auto &[key,unit]:c.session.read().world.units)
        if(key.type==1 && unit.position && c.combat.hostile(unit) && c.combat.monsterTargetEligible(unit,false) &&
            (Vec{float(unit.position->x),float(unit.position->y)}-destination).length()<4)
            task.combat.targets.emplace(key,unit.assignmentRevision);
    if(task.combat.targets.empty()) return false;
    const auto position=selectCombatLanding(c,next,task.combat,true);
    if(!position) {
        task.phase=RouteExecution::Phase::Resume;navigation.routeExecution=std::move(task);
        note("route-transit-no-stance",routeExecutionSnapshot().dump());
        return false; // Density alone cannot invalidate a geometrically legal transit.
    }
    task.position=*position;navigation.routeExecution=std::move(task);
    note("route-transit-start",routeExecutionSnapshot().dump());
    releaseCombat(c);battle.combatTarget.reset();battle.combatRun={};battle.retreatPoint.reset();cancelWalk(c);
    walk(c,{.goal=DebugWalkGoal::Point,.point=OnlinePoint{position->x,position->y},.interact=false,.exactPoint=true,.purpose="route-relocation"});
    navigation.mobilityHold=true;return true;
}
bool DebugAi::Impl::beginWalkingPassage(Context &c) {
    if((navigation.routeExecution && navigation.routeExecution->phase!=RouteExecution::Phase::Resume) || !area || !policy.enabled || !battle.excursionSkill || c.town.read().town ||
        teleportAvailable(c) || !c.walk.active() || !c.walk.blockedStep() || !bool(navigation.walkIntent) ||
        navigation.pendingDoor || c.walk.openingRouteDoor() || navigation.pendingWaypoint || questState.questRun.pending || pickup ||
        supply.supplyStage!=SupplyStage::None || Clock::now()>=goalDeadline ||
        !canInput(c,DebugActionOwner::Combat,DebugMotion|DebugSkill)) return false;
    if(supplyPolicy.enabled && quiverStock(c).empty()) return false;
    const auto purpose=navigation.walkIntent.purpose.value_or(std::string{});
    if(purpose.starts_with("combat-") || purpose=="route-relocation") return false;
    const auto &step=*c.walk.blockedStep();
    RouteExecution task;task.mode=RouteExecution::Mode::WalkingPassage;task.phase=RouteExecution::Phase::Engage;
    task.intent=navigation.walkIntent;task.area=*area;task.step=questState.questRun.step;task.segmentFrom=step.from;task.segmentTo=step.to;
    task.combat.preferAreaAttack=true;
    task.deadline=Clock::now()+std::chrono::seconds(policy.stallTimeoutSeconds);
    const auto origin=c.town.read().origin;const auto *map=c.town.map();
    if(!origin || !map) return false;
    const CombatSight sight{&map->grid,{float(origin->x),float(origin->y)},combatAttackMask(c,*battle.excursionSkill)};
    bool inRange=false;
    for(const auto key:c.town.navigationBlockers(c.session.read(),c.position,step.to)) {
        const auto found=c.session.read().world.units.find(key);
        if(found==c.session.read().world.units.end()) continue;
        const auto &unit=found->second;
        if(key.type!=1 || !unit.position || !c.combat.hostile(unit) || !c.combat.monsterTargetEligible(unit,false)) continue;
        task.combat.targets.emplace(key,unit.assignmentRevision);
        const Vec at{float(unit.position->x),float(unit.position->y)};
        const float distance=(at-c.position).length();
        inRange|=distance<policy.engage && distance<=routeAttackRange(c) && sight.clear(c.position,at);
    }
    if(!inRange) return false; // Keep approaching; melee must reach its actual attack perimeter.
    navigation.routeExecution=std::move(task);
    releaseCombat(c);battle.combatTarget.reset();battle.combatRun={};battle.retreatPoint.reset();
    c.control.cancelMovement();navigation.mobilityHold=true;
    note("route-passage-start",routeExecutionSnapshot().dump());return true;
}
bool DebugAi::Impl::beginCombatEntry(Context &c,const OnlineUnit &enemy) {
    if(navigation.routeExecution && navigation.routeExecution->phase==RouteExecution::Phase::Resume) navigation.routeExecution.reset();
    if(navigation.routeExecution || !area || !enemy.position || !teleportAvailable(c)) return false;
    RouteExecution task;task.mode=RouteExecution::Mode::CombatEntry;
    task.intent=navigation.walkIntent;task.area=*area;task.step=questState.questRun.step;task.landing=*enemy.position;
    task.combat.targets.emplace(enemy.key,enemy.assignmentRevision);
    const auto position=selectCombatLanding(c,*enemy.position,task.combat,false);
    if(!position || (Vec{float(position->x),float(position->y)}-c.position).length()<2) return false;
    task.position=*position;task.deadline=Clock::now()+std::chrono::seconds(eventPolicy.waitSeconds);
    navigation.routeExecution=std::move(task);
    note("route-combat-entry",routeExecutionSnapshot().dump());
    releaseCombat(c);cancelWalk(c);
    walk(c,{.goal=DebugWalkGoal::Point,.point=OnlinePoint{position->x,position->y},.interact=false,.exactPoint=true,.purpose="route-relocation"});
    navigation.mobilityHold=true;return true;
}
bool DebugAi::Impl::resumeRouteExecution(Context &c,const Goal &goal) {
    if(!navigation.routeExecution) return false;
    auto &task=*navigation.routeExecution;
    if(area!=task.area || questState.questRun.step!=task.step) {navigation.routeExecution.reset();return false;}
    if(task.phase==RouteExecution::Phase::Resume) return false; // Retain landing deduplication through approach relays.
    navigation.mobilityHold=true;
    const auto complete=[&](const char *reason) {
        note("route-execution-complete",Json{{"reason",reason},{"execution",routeExecutionSnapshot()}}.dump());
        releaseCombat(c);navigation.mobilityHold=false;
        battle.hazardDodge.reset();battle.retreatPoint.reset();
        battle.combatRun.reposition.reset();battle.combatRun.kitePoint.reset();battle.combatRun.kiteAfterCast=false;
        const auto mode=task.mode;
        if(mode==RouteExecution::Mode::WalkingPassage) {
            const auto intent=task.intent;
            // Keep the held child. Only a real dodge/reload may have replaced
            // it; that local interruption restores the same original intent.
            battle.combatTarget.reset();battle.combatRun={};battle.retreatPoint.reset();battle.hazardDodge.reset();navigation.routeExecution.reset();
            if(navigation.walkIntent!=intent || !c.walk.active()) {cancelWalk(c);walk(c,intent);}
        } else if(mode==RouteExecution::Mode::CombatEntry) {
            cancelWalk(c);navigation.routeExecution.reset(); // Existing combat goal owns the next input.
        } else {
            battle.combatTarget.reset();battle.combatRun={};cancelWalk(c);
            task.phase=RouteExecution::Phase::Resume;
            const auto intent=task.intent;walk(c,intent);
        }
        return true;
    };
    if(c.walk.recoveringMovement() && navigation.walkIntent!=task.intent) {
        navigation.mobilityHold=false;
        wait("route-local-recovery","Letting the tactical child rejoin observed feet before another input");return true;
    }
    const bool combatPending=c.session.read().world.combatRequest &&
        c.session.read().world.combatRequest->state==OnlineCombatRequest::State::Pending;
    // Observe submitted operations before any completion/cancellation or new
    // tactical input. Inventory ownership also protects the local child.
    if(navigation.mobilityTarget || combatPending ||
        !canInput(c,DebugActionOwner::Combat,DebugMotion|DebugSkill)) {
        wait("route-input-wait","Retaining local execution while a submitted operation owns input");return true;
    }
    if(Clock::now()>=task.deadline) {
        note("route-execution-timeout",routeExecutionSnapshot().dump());
        if(task.mode==RouteExecution::Mode::WalkingPassage) {
            finish("failed","Observed passage remained blocked within its combat execution budget",c);return true;
        }
        return complete("Local relocation/action ended without an assumed attack or kill");
    }
    const auto stock=quiverStock(c);
    if(supplyPolicy.enabled && stock.empty() && !stock.reserve && !supply.quiverTransfer)
        return complete("Ammunition depleted; return the retained route to ordinary supply arbitration");
    if(task.mode!=RouteExecution::Mode::WalkingPassage) {
        navigation.mobilityHold=false;
        if(avoidHazards(c)) {
            if(task.phase==RouteExecution::Phase::Relocate) {task.resumeRelocation=true;task.relocated=false;}
            return true;
        }
        navigation.mobilityHold=true;
    }
    if(task.phase==RouteExecution::Phase::Relocate) {
        const auto host=c.session.read().world.playerPosition;
        const Vec position{float(task.position.x),float(task.position.y)};
        if(task.relocated && !navigation.mobilityTarget && host && (position-c.position).length()<=4 &&
            (position-Vec{float(host->x),float(host->y)}).length()<=4) {
            cancelWalk(c);task.phase=RouteExecution::Phase::Engage;
            note("route-position-observed",routeExecutionSnapshot().dump());
            if(task.mode==RouteExecution::Mode::CombatEntry) return complete("Combat stance observed; hand control to the retained engagement");
        } else {
            if(task.resumeRelocation) {
                task.resumeRelocation=false;
                walk(c,{.goal=DebugWalkGoal::Point,.point=task.position,.interact=false,.exactPoint=true,.purpose="route-relocation"});
            }
            if(!c.walk.active()) return complete("Relocation child ended without a confirmed firing stance");
            if(!teleportAvailable(c)) {wait("route-mobility","Waiting for normal Teleport qualification");return true;}
            // Assignments may change between choosing a gap and selecting
            // Teleport. Recheck the exact stop, never substitute a route relay.
            const auto origin=c.town.read().origin;
            if(origin && !navigation.mobilityTarget) {
                const Vec offset{float(origin->x)-.5f,float(origin->y)-.5f};
                const auto grid=c.town.navigationGrid(c.session.read());
                if(!grid.walkable(position-offset,playerMovement) ||
                    crowdedTravelLanding(travelLandingThreats(c),position) || threatClearance(visibleThreats(c),position)<0) {
                    const auto replacement=selectCombatLanding(c,task.landing,task.combat,task.mode==RouteExecution::Mode::TeleportTransit);
                    if(!replacement) return complete("Current assignments no longer offer the selected local firing gap");
                    task.position=*replacement;cancelWalk(c);
                    walk(c,{.goal=DebugWalkGoal::Point,.point=OnlinePoint{replacement->x,replacement->y},.interact=false,.exactPoint=true,.purpose="route-relocation"});
                    note("route-stance-updated",routeExecutionSnapshot().dump());
                }
            }
            if(!mobility(c) && !navigation.mobilityTarget && Clock::now()>=navigation.nextMobility)
                return complete("The exact local Teleport landing is no longer executable");
            return true;
        }
    }
    if(task.mode==RouteExecution::Mode::WalkingPassage) {
        // Survival movement remains owned by the existing tactical executor.
        // It can replace the local child, never the retained route objective.
        navigation.mobilityHold=false;
        if(avoidHazards(c)) return true;
        const auto origin=c.town.read().origin;
        if(!origin) return true;
        const Vec offset{float(origin->x)-.5f,float(origin->y)-.5f};
        const auto grid=c.town.navigationGrid(c.session.read());
        if(grid.nativeMovementSegment(c.position-offset,task.segmentTo-offset,playerMovement))
            return complete("Actual movement ray is clear; no further kill is required");
        // Refresh only collision contributors. Moved-away, dead and reassigned
        // actors leave the scope before another cast can be submitted.
        task.combat.targets.clear();
        for(const auto key:c.town.navigationBlockers(c.session.read(),c.position,task.segmentTo)) {
            const auto unit=c.session.read().world.units.find(key);
            if(unit!=c.session.read().world.units.end() && key.type==1 && c.combat.hostile(unit->second) &&
                c.combat.monsterTargetEligible(unit->second,false))
                task.combat.targets.emplace(key,unit->second.assignmentRevision);
        }
        if(task.combat.targets.empty()) return complete("No attackable unit blocks the ray; ordinary execution owns the remaining obstruction");
        if(!combat(c,goal,true,true,&task.combat))
            return complete("Scoped blockers need a new approach from the current feet");
        return true;
    }
    if(task.phase==RouteExecution::Phase::Engage) {
        const auto before=castRequests;
        const bool acting=combat(c,goal,true,true,&task.combat);
        if(castRequests!=before) {
            const auto &request=c.session.read().world.combatRequest;
            task.attackSequence=request?request->sequence:0;task.phase=RouteExecution::Phase::AwaitCast;
            note("route-transit-attack",routeExecutionSnapshot().dump());
        } else if(!acting) return complete("Observed landing targets no longer offer an actionable attack");
        return true;
    }
    const auto &request=c.session.read().world.combatRequest;
    if(request && request->sequence==task.attackSequence && request->state==OnlineCombatRequest::State::Pending) {
        wait("route-transit-attack-wait","Observing the one submitted skill result");return true;
    }
    return complete(request && request->sequence==task.attackSequence && request->state==OnlineCombatRequest::State::Confirmed?
        "One skill confirmed; continue transit":"Skill request ended unconfirmed; continue without replay");
}
} // namespace d2x
