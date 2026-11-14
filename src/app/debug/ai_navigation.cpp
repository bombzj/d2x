#include "ai_impl.hpp"
#include "world/navigation_snapshot.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>

namespace d2x {
namespace {
// Operate72 sends the expansion finale. Ordinary travel must leave it to
// the explicit quest action even when the normal town destination is known.
bool ordinaryConnection(const OnlineMapTarget &target,const OnlineView &view) {
    if(!target.destination || (target.interaction!=OnlineMapInteraction::Portal && target.interaction!=OnlineMapInteraction::Exit)) return false;
    const auto unit=view.world.units.find(target.unit);
    return unit!=view.world.units.end() && !(target.unit.type==2 && unit->second.classId==565);
}

float distance(Vec a,Vec b) {return (a-b).length();}
bool worthwhileTransfer(float candidate,float current) {
    // Advisory costs include unknown entry positions and asynchronous terrain
    // estimates. An extra transfer needs a material saving, not float noise.
    return std::isfinite(candidate) && (!std::isfinite(current) || candidate+std::max(16.f,current*.1f)<current);
}

}
bool DebugAi::Impl::beginExplorationReturn(Context &c,OnlinePoint destination) {
    if(!area || c.town.read().town || navigation.explorationReturn || portalIssued ||
        questState.questRun.pending || questState.questRun.worldWait || questState.questRun.encounterAnchored ||
        supply.supplyStage!=SupplyStage::None || !navigation.knownWaypointAreas.contains(*area) ||
        c.town.read().townPortalSkills.empty()) return false;
    const auto town=c.observation.read().load.townArea;
    if(!town || navigation.unavailableWaypointArea==town || navigation.unavailableWaypointArea==area) return false;
    const auto hub=c.town.knownWaypointPosition(*area),townWaypoint=c.town.knownWaypointPosition(*town);
    if(!hub || !townWaypoint) return false;
    const float direct=navigationCost(c,*area,c.position,destination);
    const float viaTown=48+navigationCost(c,*town,{},townWaypoint)+16+
        navigationCost(c,*area,Vec{float(hub->x),float(hub->y)},destination);
    navigation.travelDiagnostics["searchHub"]={{"waypoint",*area},{"directCost",direct},{"townCost",viaTown}};
    if(!worthwhileTransfer(viaTown,direct)) return false;
    navigation.explorationReturn=ExplorationReturn{*area,false,Clock::now()+std::chrono::seconds(120),*area,navigation.travelVisitedAreas};
    cancelWalk(c);portalAttempted=portalIssued=portalUsed=false;oldPortals.clear();
    note("target-search-waypoint-return",navigation.travelDiagnostics["searchHub"].dump());return true;
}
bool DebugAi::Impl::resumeExplorationReturn(Context &c) {
    if(!navigation.explorationReturn) return false;
    if(Clock::now()>=navigation.explorationReturn->deadline) throw std::runtime_error("Exploration town/waypoint shortcut was not confirmed");
    auto &trip=*navigation.explorationReturn;
    trip.visited.insert(navigation.travelVisitedAreas.begin(),navigation.travelVisitedAreas.end());
    if(area) trip.visited.insert(*area);
    // This continuation owns the portal and waypoint transfer, not the
    // onward journey to the quest destination. Complete it at the selected
    // relay so later exploration retains the high-level goal's own budget.
    const auto arrival=trip.waypoint.value_or(trip.area);
    if(!trip.returning) {
        if(!reachTown(c)) return true;
        cancelWalk(c);portalIssued=portalAttempted=portalUsed=false;oldPortals.clear();navigation.submittedTravel.reset();
        navigation.travelDestination=arrival;navigation.travelRoute.clear();navigation.travelVisitedAreas.clear();navigation.unavailableWaypointArea.reset();
        navigation.waypointDestination=arrival;navigation.travelPhase=TravelPhase::WaypointOpen;navigation.legDestination.reset();navigation.legUnit.reset();
        trip.returning=true;note(trip.waypoint?"travel-town-waypoint-transfer":"exploration-waypoint-return",
            trip.waypoint?"Continuing the shorter route through the selected unlocked waypoint":"Returning to the unlocked exploration hub");
    }
    if(!travel(c,arrival)) {
        if(area && area==navigation.unavailableWaypointArea && navigation.travelPhase==TravelPhase::Route) {
            // Resume the original journey from the actual scene, carrying
            // the failed source with it instead of starting a new journey.
            navigation.travelDestination=trip.area;
            navigation.travelRoute.clear();navigation.travelVisitedAreas=trip.visited;
            navigation.legDestination.reset();navigation.legUnit.reset();navigation.waypointDestination.reset();
            navigation.explorationReturn.reset();
            note("travel-town-waypoint-abandoned","Waypoint opening failed; replanning the original destination");
        }
        return true;
    }
    navigation.travelDestination=trip.area;navigation.travelRoute.clear();navigation.travelVisitedAreas=trip.visited;
    navigation.waypointDestination.reset();navigation.legUnit.reset();navigation.submittedTravel.reset();
    navigation.explorationReturn.reset();
    note("exploration-hub-arrived","Continuing remaining observed frontiers");return true;
}
void DebugAi::Impl::rememberTravel(Context &c,OnlineUnitKey key) {
    const bool retry=navigation.passageWaits.contains(Passage::Transfer);
    auto &pending=beginPassage(Passage::Transfer,std::chrono::milliseconds(eventPolicy.initialWaitMs),
        Clock::now()+std::chrono::seconds(eventPolicy.waitSeconds));
    if(retry) pending.retryAt=Clock::now()+passageRetryInterval;
    const auto &view=c.observation.read();const auto unit=view.world.units.find(key);
    const auto child=c.walk.observe();const auto &submitted=child.submittedInteraction;
    if(!area || unit==view.world.units.end() || !submitted) return;
    navigation.submittedTravel=TravelSubmission{key,unit->second.assignmentRevision,view.gameGeneration,view.world.areaGeneration,
        submitted->worldRevision,submitted->discontinuity,*area};
}
bool DebugAi::Impl::recoverTravel(Context &c) {
    if(!navigation.submittedTravel || inputBlocked || actionRecoveryBusy || c.moving) return false;
    const auto &sent=*navigation.submittedTravel;const auto &view=c.observation.read();
    if(view.stage!=OnlineStage::ProtocolReady || !view.load.serverLoadComplete || view.gameGeneration!=sent.game ||
        view.world.areaGeneration!=sent.world || c.town.read().area!=sent.sourceArea || onlinePlayerDead(view.world)) return false;
    const auto unit=view.world.units.find(sent.unit);
    const bool withdrawn=unit==view.world.units.end() || unit->second.assignmentRevision!=sent.assignment;
    if(!withdrawn && !c.town.permitsInteraction(view,sent.unit)) return false;
    if(!retryPassage(Passage::Transfer,withdrawn?"reacquire entrance assignment":"resume normal entrance approach / interaction")) return false;
    auto &attempt=navigation.travelRetries[{sent.sourceArea,sent.unit,sent.assignment}];++attempt;
    note("travel-source-confirmed-recovery",std::to_string(sent.unit.id)+" attempt "+std::to_string(attempt));
    releaseCombat(c);cancelWalk(c);navigation.submittedTravel.reset();return true;
}
void DebugAi::Impl::observeChild(Context &c,const Goal &goal) {
    if(!bool(navigation.walkIntent) || navigation.walkIntent.goal!=DebugWalkGoal::Unit || !navigation.walkIntent.interact.value_or(false)) return;
    const auto child=c.walk.observe();
    if(!child.interactionRequests) return;
    const OnlineUnitKey unit{uint8_t(navigation.walkIntent.unit.value().type),navigation.walkIntent.unit.value().id};
    const bool waypoint=navigation.walkIntent.purpose.value_or(std::string{})=="activate-waypoint";
    if(child.state!=DebugWalkState::Completed) {
        const auto &view=c.observation.read();
        const bool questObject=goal.kind==Kind::Quest && supply.supplyStage==SupplyStage::None &&
            (navigation.walkIntent.purpose.value_or(std::string{})=="quest-object" ||
             navigation.walkIntent.purpose.value_or(std::string{})=="quest-forge");
        const auto &submitted=c.control.interactionSubmission();
        if(questObject && c.walk.active() && child.gameGeneration==view.gameGeneration &&
            child.areaGeneration==view.world.areaGeneration && submitted && submitted->target==unit &&
            onlineWorldMatches(submitted->context,view) && submitted->revision>child.interactionRevision) {
            const auto intent=navigation.walkIntent;observeQuestInteraction(c,intent);return;
        }
        // AI observes packets before the walk ticks this frame. Consume an
        // arrived menu before a new route or combat input replaces its child.
        if(!waypoint || !c.walk.active() || child.gameGeneration!=view.gameGeneration ||
            child.areaGeneration!=view.world.areaGeneration) return;
        const auto &activation=view.world.waypointActivation;
        const bool activated=!navigation.walkIntent.confirmMenu.value_or(false) && activation && activation->source==unit.id &&
            activation->generation==view.world.interactionGeneration;
        if(!activated && view.world.waypointSource!=unit.id) return;
    }
    const auto now=Clock::now();
    const bool door=navigation.walkIntent.purpose.value_or(std::string{})=="door";
    // Capture accepted interactions before combat/pause can discard a finished
    // child intent. The goal still waits for its separate real postcondition.
    if(door) {
        const auto found=c.observation.read().world.units.find(unit);
        if(found!=c.observation.read().world.units.end()) navigation.pendingDoor=std::pair{unit,found->second.assignmentRevision};
        navigation.doorDeadline=beginPassage(Passage::Door,std::chrono::milliseconds(eventPolicy.doorInitialWaitMs),now+std::chrono::seconds(eventPolicy.waitSeconds)).deadline;
        ++interactions;note("door-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if(waypoint) {
        const auto found=c.observation.read().world.units.find(unit);
        if(found!=c.observation.read().world.units.end())
            navigation.pendingWaypoint=WaypointActivation{unit,found->second.assignmentRevision,navigation.walkIntent.waypointArea.value(),navigation.walkIntent.confirmMenu.value_or(false)};
        navigation.waypointDeadline=now+std::chrono::seconds(12);++interactions;
        note("waypoint-activation-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if(navigation.walkIntent.purpose.value_or(std::string{})=="exploration-pad" && navigation.localPad && navigation.localPad->unit==unit) {
        const auto &submission=child.submittedInteraction;
        if(!submission) throw std::runtime_error("Local pad has no actual interaction submission observation");
        navigation.localPad->from=c.observation.read().world.playerPosition.value_or(submission->position);
        navigation.localPad->discontinuity=submission->discontinuity;
        navigation.localPad->requestRevision=submission->worldRevision;
        const auto grace=std::chrono::milliseconds(std::clamp<uint64_t>(2ULL*c.observation.read().latencyMilliseconds.value_or(0)+200,600,2000));
        navigation.localPad->deadline=now+3*grace+std::chrono::milliseconds(500);navigation.localPad->waiting=true;
        ++interactions;note("local-pad-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if(navigation.walkIntent.purpose.value_or(std::string{})=="quest-recover-stash") {
        QuestPending pending;pending.action=QuestPending::Action::RecoverOpen;pending.item=unit.id;
        pending.deadline=now+std::chrono::seconds(12);questState.questRun.pending=pending;cancelWalk(c);
        note("quest-recovery-stash-submitted",std::to_string(unit.id));
    } else if(navigation.walkIntent.purpose.value_or(std::string{})=="supply-heal") {
        supply.healingIssued=true;supply.healingObserved=false;supply.shopDeadline=now+std::chrono::seconds(12);cancelWalk(c);
        note("town-healing-submitted",std::to_string(unit.id));
    } else if(supply.supplyStage==SupplyStage::None && (navigation.walkIntent.purpose.value_or(std::string{})=="quest-object" ||
        navigation.walkIntent.purpose.value_or(std::string{})=="quest-portal" || navigation.walkIntent.purpose.value_or(std::string{})=="quest-orb" || navigation.walkIntent.purpose.value_or(std::string{})=="quest-forge")) {
        const auto intent=navigation.walkIntent;observeQuestInteraction(c,intent);
    } else if(supply.supplyStage==SupplyStage::None && goal.kind==Kind::Acquire && goal.sourceObject && target==unit && !objectIssued) {
        objectIssued=true;setActionDeadline(std::chrono::seconds(12));++interactions;
        note("item-source-submitted",goal.item);cancelWalk(c);
    } else if(navigation.travelPhase==TravelPhase::WarpExit && navigation.legUnit==unit) {
        rememberTravel(c,unit);
        navigation.travelPhase=TravelPhase::CrossWait;setActionDeadline(std::chrono::seconds(eventPolicy.waitSeconds));actionDeadline=navigation.passageWaits.at(Passage::Transfer).deadline;++interactions;
        note("warp-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if((goal.kind==Kind::ReturnTown || goal.kind==Kind::Restock || supply.supplyStage==SupplyStage::ToTown || (navigation.explorationReturn && !navigation.explorationReturn->returning)) && portalIssued && !portalUsed) {
        rememberTravel(c,unit);
        portalUsed=true;setActionDeadline(std::chrono::seconds(eventPolicy.waitSeconds));actionDeadline=navigation.passageWaits.at(Passage::Transfer).deadline;++interactions;
        note("town-portal-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    }
}
bool DebugAi::Impl::walk(Context &c,DebugWalkIntent intent) {
    if(intent.goal==DebugWalkGoal::Waypoint && !intent.searchHint) intent.searchHint=experienceWaypointHint(c);
    intent.prepared=true;intent.timeoutSeconds=120;
    // The native last automatic speech sends 31 then 30. A successful old
    // opening cannot satisfy a new conversation after that interaction ends.
    if(navigation.walkIntent==intent && intent.goal==DebugWalkGoal::Unit &&
        (intent.unit?intent.unit->type:0)==1 && intent.interact.value_or(false) && c.walk.observe().state==DebugWalkState::Completed) {
        const auto &conversation=c.observation.read().world.npcConversation;
        if(!conversation || conversation->type!=1 || conversation->source!=intent.unit.value().id) {
            cancelWalk(c);note("npc-conversation-reopen","Previous opening completed but the conversation has ended");
        }
    }
    const bool needsObservation=navigation.walkIntent==intent && c.walk.observe().state==DebugWalkState::NeedsObservation;
    std::optional<OnlineUnitKey> observationTarget;
    if(needsObservation) {
        const auto child=c.walk.observe();const auto &unit=child.unit;
        if(unit) observationTarget=*unit;
    } else if(navigation.approachObservation.owner==intent && bool(navigation.walkIntent) &&
        navigation.walkIntent.purpose.value_or(std::string{})=="known-target-memory") observationTarget=navigation.approachObservation.unit;
    if(observationTarget) {
        const auto &targets=c.town.read().mapTargets;
        const auto target=std::find_if(targets.begin(),targets.end(),[&](const auto &t){return t.unit==*observationTarget;});
        if(target!=targets.end() && approachPoint(c,target->position,"assigned interaction target",6,false,observationTarget)) {
            navigation.approachObservation.owner=intent;return false;
        }
    }
    if(needsObservation) {note("navigation-needs-observation","Refreshing the observed interaction approach");cancelWalk(c);}
    const bool continuing=c.walk.active() && navigation.walkIntent==intent;
    bool nearbyInteraction=false;
    if(intent.goal==DebugWalkGoal::Unit) {
        const OnlineUnitKey key{intent.unit.value().type,intent.unit.value().id};
        for(const auto &entry:c.town.read().mapTargets) if(entry.unit==key)
            nearbyInteraction=intent.interact.value_or(false) &&
                distance(c.position,{float(entry.position.x),float(entry.position.y)})<=6;
        if(intent.interact.value_or(false) && DebugNavigationSearch::directNpcApproach(c.town,c.observation.read(),key,c.position))
            nearbyInteraction=true;
        if(continuing && c.control.approaching()==key) nearbyInteraction=true;
        const auto assigned=std::find_if(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry){return entry.unit==key;});
        if(assigned!=c.town.read().mapTargets.end() && !continuing && intent.interact.value_or(false)) {
            const auto &view=c.observation.read();
            const auto states=c.combat.states().find({0,view.load.playerUnitId.value_or(0)});
            const bool locked=states!=c.combat.states().end() && states->second.decoded &&
                std::any_of(states->second.states.begin(),states->second.states.end(),[](const auto &state) {
                    return state.second.name=="uninterruptable";
                });
            if((c.supplies && c.supplies->castInputBusy.value_or(false)) || locked) {
                navigation.mobilityHold=true;wait("interaction-action-settle","Waiting for the normal skill/action lock before a new unit click");return false;
            }
            // A request-range check says nothing about observed terrain.
            // A nearby NPC with a checked execution ray may take over now;
            // other targets still use observed stances or passages.
            if(intent.purpose.value_or(std::string{})!="door" &&
                approachPoint(c,assigned->position,"assigned interaction target",6,false,key)) {
                navigation.approachObservation.owner=intent;return false;
            }
            // The observed route has handed off this unit. Give its normal
            // click ownership before considering unrelated nearby doors.
            nearbyInteraction=true;
        }
        if(std::none_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){return t.unit==key;})) {
            const auto unit=c.observation.read().world.units.find(key);
            if(unit!=c.observation.read().world.units.end() && unit->second.position &&
                approachPoint(c,*unit->second.position,"client-observed assigned target")) return false;
            cancelWalk(c);wait("target-reassignment","Waiting for a current interactable client assignment");return false;
        }
    }
    if(intent.goal!=DebugWalkGoal::Point) {
        bool isDoor=intent.purpose.value_or(std::string{})=="door";
        if(intent.goal==DebugWalkGoal::Unit) {
            const OnlineUnitKey unit{intent.unit.value().type,intent.unit.value().id};
            isDoor=isDoor || std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){
                return t.unit==unit && t.interaction==OnlineMapInteraction::Door;
            });
        }
        // An already adjacent portal/item/NPC must not be postponed in favour
        // of unrelated closed doors, taking us out of its assignment range.
        if(!isDoor && !nearbyInteraction && openDoor(c)) return false;
    }
    if(navigation.walkIntent!=intent) {
        cancelWalk(c);navigation.walkIntent=intent;
        if(intent.goal==DebugWalkGoal::Unit) {
            const OnlineUnitKey key{intent.unit.value().type,intent.unit.value().id};
            const auto unit=c.observation.read().world.units.find(key);
            unsigned retry=0;
            if(unit!=c.observation.read().world.units.end()) {
                const auto previous=navigation.travelRetries.find({area.value_or(0),key,unit->second.assignmentRevision});
                if(previous!=navigation.travelRetries.end()) retry=previous->second;
                const auto door=navigation.passageWaits.find(Passage::Door);
                if(door!=navigation.passageWaits.end() && (intent.purpose.value_or(std::string{})=="door" || intent.activation.value_or(false)))
                    retry=std::max(retry,door->second.attempts);
            }
            if(retry && intent.interact.value_or(false) && (key.type==1 || key.type==2 || key.type==5)) {
                // A failed opening needs the same approach recovery even
                // when prediction has already reached the target. Otherwise
                // a host stuck behind collision only receives more clicks.
                intent.forceApproach=true;intent.resyncApproach=retry>=2;
                const auto host=c.observation.read().world.playerPosition;
                note("interaction-approach-retry",std::to_string(key.type)+"/"+std::to_string(key.id)+
                    " n="+std::to_string(retry)+" local="+std::to_string(int(c.position.x))+","+std::to_string(int(c.position.y))+
                    (host?" host="+std::to_string(host->x)+","+std::to_string(host->y):" host=unknown"));
            }
        }
        c.walk.start(intent,c.observation,c.town,c.control);++moveGoals;
        auto description=std::string(debugWalkName(intent.goal));
        if(description=="unit") description+=" "+std::to_string(intent.unit.value().type)+"/"+
            std::to_string(intent.unit.value().id);
        else if(description=="point") description+=" "+std::to_string(intent.point.value().x)+","+
            std::to_string(intent.point.value().y);
        if(bool(intent.level)) description+=" level="+std::to_string(intent.level.value());
        if(bool(intent.purpose)) description+=" "+intent.purpose.value();
        behaviorLog.event(cursor,"walk-goal",description);
        return false;
    }
    const auto status=c.walk.observe();const auto value=status.state;
    if(value==DebugWalkState::Completed) return true;
    if(value==DebugWalkState::Failed && status.failure==DebugWalkFailure::TargetWithdrawn) {
        note("navigation-target-withdrawn");cancelWalk(c);return false;
    }
    if(value==DebugWalkState::Cancelled) {
        // The normal client can replace its scene binding during a walk or
        // teleport before the parent next samples the new area. Rebind the
        // child; only a real area observation can satisfy the travel goal.
        note("navigation-rebound",status.reason);cancelWalk(c);return false;
    }
    if(value==DebugWalkState::Failed) {
        const auto purpose=intent.purpose.value_or(std::string{});
        if(purpose=="initial-waypoint-memory" || purpose=="activate-waypoint") {
            const auto &source=status.unit;
            if(bool(source)) {
                const OnlineUnitKey key=*source;
                const auto &world=c.observation.read().world;
                const auto unit=world.units.find(key);
                navigation.deferredWaypoint=WaypointDeferral{key,unit==world.units.end()?0:unit->second.assignmentRevision,
                    Clock::now()+std::chrono::seconds(30),c.position};
                if(world.waypointRequested==key.id) c.control.useWaypoint(0);
                note("waypoint-approach-deferred",status.reason);cancelWalk(c);return false;
            }
        }
        if((intent.goal==DebugWalkGoal::Waypoint || purpose=="travel-waypoint") && navigation.travelDestination && !navigation.pendingWaypoint) {
            note("waypoint-approach-deferred",status.reason);
            if(status.unit && c.observation.read().world.waypointRequested==status.unit->id) c.control.useWaypoint(0);
            navigation.unavailableWaypointArea=area;navigation.waypointDestination.reset();navigation.travelPhase=TravelPhase::Route;
            cancelWalk(c);return false;
        }
        if(intent.goal==DebugWalkGoal::Point &&
            (intent.purpose.value_or(std::string{})=="known-target-teleport" || intent.directTeleport.value_or(false))) {
            recordFrontierAttempt(c,{intent.point.value().x,intent.point.value().y},
                "failed",status.reason);
            note("travel-landing-deferred","The selected movement could not reach this landing");
            cancelWalk(c);return false;
        }
        const auto detail=status.reason;
        const bool recoverable=recoverableDebugWalkFailure(status.failure);
        if(recoverable && intent.goal==DebugWalkGoal::Unit &&
            !questState.questRun.pending && !supply.shopIssued && !supply.potionRequest) {
            const OnlineUnitKey key{intent.unit.value().type,intent.unit.value().id};
            const auto &world=c.observation.read().world;const auto unit=world.units.find(key);
            // Only an opening or an approach that has not submitted an object
            // operation can be restarted. Never replay a reward/transaction.
            if(unit!=world.units.end() && (key.type==1 || !status.submittedInteraction)) {
                auto &attempt=navigation.navigationRetries[{area.value_or(0),key,unit->second.assignmentRevision}];
                if(attempt<2) {
                    ++attempt;note("navigation-local-recovery",std::to_string(key.id)+" attempt "+std::to_string(attempt)+": "+detail);
                    if(key.type==1 && world.npcRequested==key.id) c.control.closeNpc();
                    cancelWalk(c);navigation.walkIntent=intent;
                    auto retry=intent;retry.forceApproach=true;retry.resyncApproach=attempt==2;
                    c.walk.start(retry,c.observation,c.town,c.control);++moveGoals;return false;
                }
            }
        }
        if(intent.goal==DebugWalkGoal::Unit && navigation.travelPhase==TravelPhase::WarpExit && navigation.legUnit && navigation.legDestination &&
            !status.submittedInteraction) {
            const auto key=*navigation.legUnit;const auto &world=c.observation.read().world;
            const auto unit=world.units.find(key);
            const Vec feet=world.playerPosition?Vec{float(world.playerPosition->x),float(world.playerPosition->y)}:c.position;
            auto &stalled=navigation.connectionStalls[{area.value_or(0),key,unit==world.units.end()?0:unit->second.assignmentRevision}];
            if(!stalled.attempts || distance(stalled.position,feet)>=2) stalled={feet,0};
            if(++stalled.attempts>=3) {
                note("travel-connection-stalled",Json{{"destination",*navigation.legDestination},{"unit",{key.type,key.id}},
                    {"position",{feet.x,feet.y}},{"attempts",stalled.attempts},{"reason",detail}}.dump());
                cancelWalk(c);
                throw std::runtime_error("Travel connection repeatedly failed without observed position progress: "+detail);
            }
            auto &failures=navigation.failedWalkEdges[{area.value_or(0),*navigation.legDestination}];failures=std::min(failures+1,16u);
            note("travel-connection-approach-failed",std::to_string(*navigation.legDestination)+": "+detail);
            cancelWalk(c);navigation.travelRoute.clear();navigation.travelPhase=TravelPhase::Route;navigation.legDestination.reset();navigation.legUnit.reset();
            navigation.exitRetryAt=Clock::now()+std::chrono::seconds(5);return false;
        }
        cancelWalk(c);
        throw std::runtime_error("AI navigation: "+detail);
    }
    return false;
}
bool DebugAi::Impl::walkPoint(Context &c,OnlinePoint point,bool exploration) {
    if(c.walk.active() && bool(navigation.walkIntent) && navigation.walkIntent.goal==DebugWalkGoal::Point) {
        const int x=(navigation.walkIntent.point?navigation.walkIntent.point->x:0),y=(navigation.walkIntent.point?navigation.walkIntent.point->y:0);
        if(navigation.walkIntent.exploration.value_or(false)==exploration && std::abs(x-int(point.x))<=2 && std::abs(y-int(point.y))<=2) return false;
    }
    return walk(c,{.goal=DebugWalkGoal::Point,.point=OnlinePoint{point.x,point.y},.interact=false,.exploration=exploration});
}
std::vector<int> DebugAi::Impl::route(const Context &c,int from,int to,std::optional<Vec> start) const {
    auto graph=c.town.navigationConnections();
    // Special quest portals (notably Tristram's return red portal) are not
    // Levels.Vis links. Add only actually assigned outgoing connections.
    if(c.town.read().area) for(const auto &target:c.town.read().mapTargets)
        if(ordinaryConnection(target,c.observation.read())) graph[*c.town.read().area].push_back(*target.destination);
    // Arrival matters: walking in from a cave and arriving at a waypoint
    // have different onward costs, even though both are in the same area.
    using State=std::pair<int,int>; // previous area, current area
    const State origin{from,from};std::optional<State> finish;
    if(!start && area==from) start=c.position;
    std::map<State,State> previous{{origin,origin}};std::map<State,float> costs{{origin,0}};
    std::map<State,std::optional<Vec>> arrivals{{origin,start}};
    using Pending=std::pair<float,State>;
    std::priority_queue<Pending,std::vector<Pending>,std::greater<Pending>> queue;queue.emplace(0,origin);
    while(!queue.empty()) {
        const auto [cost,state]=queue.top();queue.pop();
        if(cost>costs.at(state)) continue;
        const auto [prior,current]=state;
        if(current==to) {finish=state;break;}
        const auto edges=graph.find(current);if(edges==graph.end()) continue;
        for(int next:edges->second) {
            if(next==from || next==prior) continue;
            const State successor{current,next};
            const auto leg=travelLeg(c,current,next,arrivals.at(state));
            const float candidate=cost+leg.cost;
            if(!costs.contains(successor) || candidate<costs.at(successor)) {
                costs[successor]=candidate;previous[successor]=state;arrivals[successor]=leg.arrival;queue.emplace(candidate,successor);
            }
        }
    }
    if(!finish) return {};
    std::vector<int> result;for(auto p=*finish;;p=previous.at(p)) {result.push_back(p.second);if(p==origin) break;}
    std::reverse(result.begin(),result.end());return result;
}
float DebugAi::Impl::navigationCost(const Context &c,int level,std::optional<Vec> start,std::optional<OnlinePoint> target) const {
    // A policy cost for an unknown leg, never a generated room-size estimate.
    const auto [range,restricted]=travelMobility(c,level,false);
    const auto movementCost=[&](float span) {
        return range>0?std::min(span,8.f*std::ceil(span/range)+span*.1f):span;
    };
    const float unknownLegCost=movementCost(80);
    if(!target || !start) return unknownLegCost;
    // A usable interaction already in reach has no approach leg. In
    // particular, an asynchronous estimate for an occupied waypoint centre
    // must not turn standing beside it into an expensive unknown journey.
    if(area==level) for(const auto &entry:c.town.read().mapTargets)
        if(entry.position==*target && c.town.assignedUnitLevel(entry.unit)==level &&
            c.town.interactionReady(c.observation.read(),entry.unit,*start)) return 0;
    const Vec wanted{float(target->x),float(target->y)};
    float length=movementCost(distance(*start,wanted));
    if(c.walk.navigationSearch().explorationMemory.area(uint16_t(level))) {
        const auto cost=c.walk.navigationSearch().estimatedRouteCost(uint16_t(level),*start,wanted,range,restricted);
        if(!cost) return length+unknownLegCost;
        length=*cost;
    }
    return length;
}
DebugAi::Impl::TravelLeg DebugAi::Impl::travelLeg(const Context &c,int from,int to,std::optional<Vec> start) const {
    TravelLeg result;
    const auto target=c.town.knownConnectionPosition(uint16_t(from),uint16_t(to));
    const auto entry=c.town.knownConnectionPosition(uint16_t(to),uint16_t(from));
    if(entry) result.arrival=Vec{float(entry->x),float(entry->y)};
    float approach=std::numeric_limits<float>::infinity();
    const OnlineMapTarget *connection=nullptr;
    if(from==area) {
        for(const auto &candidate:c.town.read().mapTargets)
            if(candidate.destination==to && ordinaryConnection(candidate,c.observation.read())) {
                const float cost=navigationCost(c,from,start,candidate.position);
                if(cost<approach) {approach=cost;connection=&candidate;}
            }
        // An assigned portal and a walkable boundary may lead to the same
        // area. Score and execute the same entrance, not whichever was first.
        if(target && connection) {
            const auto boundaries=c.town.walkingBoundaries();
            if(std::any_of(boundaries.begin(),boundaries.end(),[&](const auto &edge){return edge.destination==to;})) {
                const float cost=navigationCost(c,from,start,target);
                if(cost<approach) {approach=cost;connection=nullptr;}
            }
        }
    }
    if(connection) {
        result.unit=connection->unit;
        const auto unit=c.observation.read().world.units.find(connection->unit);
        // A player's portal does not arrive at the ordinary reverse exit.
        // Its unobserved far endpoint stays unknown rather than borrowing it.
        if(unit!=c.observation.read().world.units.end() && unit->second.portalOwner) result.arrival.reset();
    }
    if(!std::isfinite(approach)) approach=navigationCost(c,from,start,target);
    result.cost=approach+8;
    const auto failed=navigation.failedWalkEdges.find({from,to});
    if(failed==navigation.failedWalkEdges.end() || failed->second<2) return result;
    // The route estimate covers only one approach. Repeated failed crossings
    // also cost a return trip. Keep the leg usable when it is the only route.
    result.cost+=2*std::max(80.f,result.cost)*failed->second;return result;
}
float DebugAi::Impl::routeCost(const Context &c,const std::vector<int> &path,std::optional<Vec> start) const {
    if(path.empty()) return std::numeric_limits<float>::infinity();
    if(!start && area==path.front()) start=c.position;
    float cost=0;
    for(size_t i=1;i<path.size();++i) {
        const auto leg=travelLeg(c,path[i-1],path[i],start);
        cost+=leg.cost;start=leg.arrival;
    }
    return cost;
}
bool DebugAi::Impl::approachTownGate(Context &c,uint16_t destination) {
    if(!c.town.read().town || !definitions->harrogathGate || area!=definitions->area("Act 5 - Town") ||
        destination!=definitions->area("Act 5 - Siege 1")) return false;
    const Selector gate{2,*definitions->harrogathGate,"Harrogath main gate"};
    const auto &world=c.observation.read().world;
    for(const auto &[key,unit]:world.units) {
        if(key.type!=gate.type || unit.classId!=gate.identity || !unit.position || !unit.mode) continue;
        const auto hint=std::tuple{*area,gate.type,gate.identity,unit.position->x,unit.position->y};
        if(*unit.mode!=0) {
            navigation.checkedHints.insert(hint);
            if(c.walk.active() && bool(navigation.walkIntent) && navigation.walkIntent.purpose.value_or(std::string{})=="door" &&
                (navigation.walkIntent.unit?navigation.walkIntent.unit->id:0u)==key.id) cancelWalk(c);
            return false;
        }
        const auto &targets=c.town.read().mapTargets;
        if(std::none_of(targets.begin(),targets.end(),[&](const auto &target) {
            return target.unit==key && target.interaction==OnlineMapInteraction::Door;
        })) break;
        navigation.checkedHints.erase(hint);
        const bool outsidePointerRange=std::abs(c.position.x-unit.position->x)>50 || std::abs(c.position.y-unit.position->y)>50;
        if(outsidePointerRange && approachPoint(c,*unit.position,gate.name,6)) return true;
        wait("town-exit-gate","Approaching the assigned closed main gate");
        if(walk(c,{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{key.type,key.id},.interact=true,.purpose="door"}))
            observeChild(c,goals[cursor]);
        return true;
    }
    // Returning to a real last-seen gate position can restore its assignment.
    // The remembered position alone never issues an interaction.
    return approachKnown(c,gate);
}
bool DebugAi::Impl::openDoor(Context &c,bool frontier) {
    (void)frontier;
    if(!c.walk.openingRouteDoor()) return false;
    wait("open-door","Continuing the normal door operation on the retained route");
    return true;
}
void DebugAi::Impl::observeWaypointMemory(const Context &c) {
    const auto &view=c.observation.read();const auto &world=view.world;
    if(navigation.waypointMemoryGame!=view.gameGeneration) {
        navigation.waypointMemoryGame=view.gameGeneration;navigation.waypointMemoryRevision=0;navigation.waypointActivationGeneration.reset();
        navigation.knownWaypointAreas.clear();navigation.waypointTownArrivals.clear();
        navigation.observedWaypointMenu.reset();navigation.waypointMenuOpenedAt={};
    }
    if(navigation.observedWaypointMenu!=world.waypointSource) {
        navigation.observedWaypointMenu=world.waypointSource;
        navigation.waypointMenuOpenedAt=world.waypointSource?Clock::now():Clock::time_point{};
    }
    // Normal act travel activates the destination town in SUnitNpc / ObjMode.
    // This is AI route knowledge, never a locally fabricated protocol bit.
    // Observe each arrival once so a later full 63 can correct that knowledge.
    if(c.town.read().town && area && navigation.waypointTownArrivals.insert(*area).second && !navigation.knownWaypointAreas.empty())
        for(size_t row=0;row<definitions->levels.rows().size();++row) {
            const auto id=definitions->levels.number(row,"Id"),number=definitions->levels.number(row,"Waypoint");
            if(id==*area && number && *number>=0 && *number<112) navigation.knownWaypointAreas.insert(*area);
        }
    if(world.waypointHistory && navigation.waypointMemoryRevision!=world.waypointHistoryRevision) {
        navigation.knownWaypointAreas.clear();
        for(size_t row=0;row<definitions->levels.rows().size();++row) {
            const auto id=definitions->levels.number(row,"Id"),number=definitions->levels.number(row,"Waypoint");
            if(id && *id>0 && *id<=UINT16_MAX && number && *number>=0 && *number<112 &&
                ((*world.waypointHistory)[1+*number/16] & (1u<<(*number&15)))) navigation.knownWaypointAreas.insert(uint16_t(*id));
        }
        navigation.waypointMemoryRevision=world.waypointHistoryRevision;
        note("waypoint-memory-synchronized","Actual personal 63 replaced the prior route knowledge");
    }
    const auto &receipt=world.waypointActivation;
    if(receipt && receipt->generation==world.interactionGeneration && navigation.waypointActivationGeneration!=receipt->generation) {
        if(const auto level=c.town.assignedUnitLevel({2,receipt->source})) {
            navigation.waypointActivationGeneration=receipt->generation;
            if(navigation.knownWaypointAreas.insert(*level).second)
                note("waypoint-unlocked","area="+std::to_string(*level)+" unit="+std::to_string(receipt->source));
        }
    }
    // Remember a point passed during a cast or a quest interaction until the
    // scheduler can act on it. Its assignment must belong to this level.
    if(c.town.read().nativeMapReady && area && !c.town.read().town && !navigation.knownWaypointAreas.contains(*area) && navigation.nearbyWaypointArea!=area) {
        for(const auto &target:c.town.read().mapTargets) {
            if(target.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(target.unit)==area &&
                ((*area==definitions->area("Act 1 - Wilderness 2")) ||
                 (std::abs(c.position.x-target.position.x)<=50 && std::abs(c.position.y-target.position.y)<=50))) {
                navigation.nearbyWaypointArea=area;break;
            }
        }
    }
}
bool DebugAi::Impl::activateWaypoint(Context &c,bool required) {
    const auto &world=c.observation.read().world;
    if(!area || (!required && world.npcRequested) || navigation.pendingWaypoint || navigation.closeWaypoint) return false;
    const auto purpose=bool(navigation.walkIntent)?navigation.walkIntent.purpose.value_or(std::string{}):std::string{};
    const bool initial=purpose=="initial-waypoint-memory";
    if(c.town.read().town && (initial || !navigation.waypointMemoryRevision)) {
        if(navigation.deferredWaypoint && Clock::now()<navigation.deferredWaypoint->retryAt) return false;
        if(world.waypointSource && world.waypointHistory) {
            navigation.closeWaypoint=world.waypointSource;cancelWalk(c);return true;
        }
        if(!navigation.waypointMemoryRevision) {
            wait("waypoint-memory-initial","Reading personal waypoint history in town");
            walk(c,{.goal=DebugWalkGoal::Waypoint,.purpose="initial-waypoint-memory"});return true;
        }
        cancelWalk(c);
    }
    if(navigation.knownWaypointAreas.contains(*area)) {
        if(purpose=="activate-waypoint") {
            if(world.waypointSource==navigation.walkIntent.unit.value().id) navigation.closeWaypoint=world.waypointSource;
            cancelWalk(c);
            return navigation.closeWaypoint.has_value();
        }
        return false;
    }
    if(world.waypointSource || (c.town.read().town && !required)) return false;
    if(navigation.deferredWaypoint && Clock::now()>=navigation.deferredWaypoint->retryAt) navigation.deferredWaypoint.reset();
    if(c.walk.active() && purpose=="activate-waypoint") {
        wait("activate-waypoint","Continuing the waypoint approach");
        if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
        return true;
    }
    if(!required && navigation.nearbyWaypointArea!=area && !c.town.knownWaypointPosition(*area)) {
        if(observeColdPlainsWaypoint(c)) return true;
        return false;
    }
    const OnlineMapTarget *selected=nullptr;
    float nearest=std::numeric_limits<float>::infinity();
    for(const auto &entry:c.town.read().mapTargets) {
        if(entry.interaction!=OnlineMapInteraction::Waypoint || c.town.assignedUnitLevel(entry.unit)!=area) continue;
        const auto unit=world.units.find(entry.unit);
        if(unit==world.units.end() || !unit->second.mode) continue;
        if(navigation.deferredWaypoint && navigation.deferredWaypoint->unit==entry.unit && navigation.deferredWaypoint->assignment==unit->second.assignmentRevision) continue;
        const float span=distance(c.position,{float(entry.position.x),float(entry.position.y)});
        if(span<nearest) {nearest=span;selected=&entry;}
    }
    if(!selected) {
        const auto seen=c.town.knownWaypointPosition(*area);
        if(navigation.deferredWaypoint && required) {
            wait("waypoint-retry","Waiting to retry the unconfirmed waypoint in this area");return true;
        }
        if(!navigation.deferredWaypoint && seen && navigation.missingWaypointAt!=seen) {
            if(approachPoint(c,*seen,"previously observed waypoint",4)) {
                if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
                return true;
            }
            // Explore for a fresh assignment instead of orbiting a stale hint.
            navigation.missingWaypointAt=seen;
        }
        if(!navigation.deferredWaypoint && !seen && observeColdPlainsWaypoint(c)) return true;
        return false;
    }
    navigation.missingWaypointAt.reset();
    wait("activate-waypoint","Requesting the current area's waypoint");
    const DebugWalkIntent intent{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{selected->unit.type,selected->unit.id},.interact=true,.confirmMenu=world.units.at(selected->unit).mode!=0,.purpose="activate-waypoint",.waypointArea=*area};
    if(walk(c,intent)) observeChild(c,goals[cursor]);
    if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
    return true;
}
bool DebugAi::Impl::waypointBeforeDeparture(Context &c) {
    if(!area || !navigation.requiredWaypointAreas.contains(*area) || navigation.knownWaypointAreas.contains(*area)) return false;
    if(navigation.waypointDepartureArea!=area) {
        if(bool(navigation.walkIntent) && navigation.walkIntent.goal==DebugWalkGoal::Exit) cancelWalk(c);
        navigation.waypointDepartureArea=area;note("waypoint-required-before-departure",std::to_string(*area));
    }
    if(navigation.pendingWaypoint || navigation.closeWaypoint) return true;
    if(c.observation.read().world.waypointSource) {navigation.closeWaypoint=c.observation.read().world.waypointSource;return true;}
    if(!activateWaypoint(c,true)) {
        explore(c,"required waypoint before leaving this area",false,ExplorationFocus::Units);
        if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
    }
    return true;
}
void DebugAi::Impl::refreshNavigationAttempts(const Context &c) {
    const auto now=Clock::now();const auto revision=c.town.navigationRevision();
    std::erase_if(navigation.frontierAttempts,[&](const auto &entry) {
        const auto &attempt=entry.second;
        return now>=attempt.retryAt || (!attempt.retainAfterMove && (attempt.geometryRevision!=revision || distance(c.position,attempt.position)>=8));
    });

}
void DebugAi::Impl::recordFrontierAttempt(const Context &c,OnlinePoint point,std::string outcome,std::string detail,std::optional<OnlinePoint> relayGoal) {
    refreshNavigationAttempts(c);
    // Attempts control short-term replanning only. Neither completion nor
    // failure writes discovery; the rendered viewport is its sole producer.
    if(navigation.frontierAttempts.size()>=256 && !navigation.frontierAttempts.contains({point.x,point.y}))
        navigation.frontierAttempts.erase(std::min_element(navigation.frontierAttempts.begin(),navigation.frontierAttempts.end(),[](const auto &a,const auto &b) {
            return a.second.retryAt<b.second.retryAt;
        }));
    const bool reached=distance(c.position,{float(point.x),float(point.y)})<=3;
    const bool exploration=bool(navigation.walkIntent) && navigation.walkIntent.exploration.value_or(false);
    if(!reached || exploration || relayGoal==point) relayGoal.reset();
    const bool retain=reached && (exploration || relayGoal.has_value());
    navigation.frontierAttempts[{point.x,point.y}]={c.position,c.town.navigationRevision(),Clock::now()+std::chrono::seconds(retain?30:5),
        reached,retain,std::move(outcome),std::move(detail),relayGoal};
    const auto &attempt=navigation.frontierAttempts.at({point.x,point.y});
    note("navigation-frontier-attempt",Json{{"target",{point.x,point.y}},
        {"position",{c.position.x,c.position.y}},{"reached",reached},{"retainAfterMove",retain},
        {"retryAfterMs",retain?30000:5000},{"outcome",attempt.outcome},{"reason",attempt.reason},
        {"geometryRevision",attempt.geometryRevision},
        {"relayGoal",relayGoal?Json{relayGoal->x,relayGoal->y}:Json(nullptr)}}.dump());
}
bool DebugAi::Impl::frontierCoolingDown(OnlinePoint point,float radius,std::optional<OnlinePoint> relayGoal) const {
    return std::any_of(navigation.frontierAttempts.begin(),navigation.frontierAttempts.end(),[&](const auto &entry) {
        return (!entry.second.relayGoal || entry.second.relayGoal==relayGoal) && Clock::now()<entry.second.retryAt && distance({float(point.x),float(point.y)},
            {float(entry.first.first),float(entry.first.second)})<radius;
    });
}
DebugAi::Impl::TerrainMemory *DebugAi::Impl::observeTerrain(const Context &c) {
    auto &search=c.walk.navigationSearch();
    auto *memory=search.observeTerrain(c.town,c.observation.read(),c.position);
    if(memory) {
        const auto *experience=areaExperience(c);
        const auto bounds=experience?experience->bounds:std::nullopt;
        if(memory->expectedBounds!=bounds) {memory->expectedBounds=bounds;++memory->revision;}
    }
    return memory;
}
DebugAi::Impl::TerrainLeg DebugAi::Impl::terrainLeg(const Context &c,const std::vector<OnlinePoint> &path) const {
    TerrainLeg result;
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    if(!map || !origin || path.empty()) return result;
    float best=std::numeric_limits<float>::max();
    for(size_t i=0;i<path.size();++i) {
        const float span=distance(c.position,{float(path[i].x),float(path[i].y)});
        if(span<best) {best=span;result.nearest=i;}
    }
    const auto &grid=c.walk.navigationSearch().planningGrid(c.town,c.observation.read());
    const bool teleport=teleportAvailable(c,false);
    const auto &walking=grid;
    const TeleportTargets neighborhood(c);
    if(teleport) {
        const auto next=c.walk.navigationSearch().teleportContinuation(path,c.position);
        if(next && neighborhood.permits(c,{float(path[*next].x),float(path[*next].y)},mobilityPolicy.distance))
            result.nearest=*next;
    }
    const Vec offset{float(origin->x),float(origin->y)};
    const auto feet=DebugExplorationMemory::playerCell(c.position);
    Vec previous=c.position;bool walkSegment=true;
    for(size_t i=result.nearest;i<path.size() && i<=result.nearest+40;++i) {
        const Vec at{float(path[i].x),float(path[i].y)},local=at-offset+Vec{.5f,.5f};
        // A mixed route can require short walking edges between cast anchors.
        // Rejecting one landing must not hide a later legal cast across it.
        walkSegment=walkSegment && grid.nativeMovementSegment(previous-offset+Vec{.5f,.5f},local,playerMovement);
        const bool cast=teleport && neighborhood.permits(c,at,mobilityPolicy.distance);
        // Walking may open doors; casting checks resident terrain/objects.
        // Dynamic actors never veto a requested Teleport target.
        const bool walk=walkSegment && walking.walkable(local,playerMovement);
        if((path[i].x!=feet.first || path[i].y!=feet.second) && (walk || cast)) {
            result.point=path[i];result.teleport=!walk && cast;
        }
        previous=at;
    }
    return result;
}
bool DebugAi::Impl::frontierWalking(const Context &c) const {
    return navigation.frontierRoute && c.walk.active() && bool(navigation.walkIntent) &&
        navigation.walkIntent.purpose.value_or(std::string{})=="exploration-frontier";
}
bool DebugAi::Impl::exploreFrontier(Context &c,const std::string &purpose,bool recoveryOnly,ExplorationFocus focus) {
    auto *memory=observeTerrain(c);
    if(!memory || !area) {wait("explore-wait","Waiting for actual terrain observations for "+purpose);return false;}
    auto &m=*memory;
    const auto feet=DebugExplorationMemory::playerCell(c.position);
    const bool owned=bool(navigation.walkIntent) && navigation.walkIntent.purpose.value_or(std::string{})=="exploration-frontier";
    if(navigation.frontierRoute && navigation.frontierRoute->area!=*area) navigation.frontierRoute.reset();
    if(!recoveryOnly && navigation.frontierRoute && navigation.frontierRoute->focus!=focus) {
        if(owned && c.walk.active()) {wait("explore-frontier","Finishing the submitted relay before changing exploration purpose");return false;}
        m.commitment.reset();navigation.frontierRoute.reset();
    }
    const bool followPaths=focus==ExplorationFocus::Connection && !m.paths.empty();
    if(!recoveryOnly && m.followPaths!=followPaths) {
        m.followPaths=followPaths;
        if(m.commitment) if(const auto *passage=m.passage(*m.commitment);passage && passage->road) m.commitment.reset();
        ++m.revision;
    }
    if(navigation.frontierRoute) {
        const auto *passage=m.passage(navigation.frontierRoute->passage);
        const bool stillApproach=passage && (navigation.frontierRoute->transit ||
            std::find(passage->approaches.begin(),passage->approaches.end(),
                DebugExplorationMemory::Cell{navigation.frontierRoute->target.x,navigation.frontierRoute->target.y})!=passage->approaches.end());
        if(!stillApproach) {
            note("exploration-direction-observed",std::to_string(navigation.frontierRoute->passage));
            if(owned) cancelWalk(c);
            navigation.frontierRoute.reset();
        } else {
            navigation.frontierRoute->evidence=passage->evidence;
            if(owned && c.walk.active()) {wait("explore-frontier",purpose);return false;}
        }
    }
    if(recoveryOnly) return false;
    if(owned && bool(navigation.walkIntent)) {
        const auto outcome=c.walk.observe().state;cancelWalk(c);
        if(navigation.frontierRoute && outcome==DebugWalkState::Failed) ++navigation.frontierRoute->stalls;
    }
    if(openDoor(c)) return false;
    const auto defer=[&](const char *why) {
        if(!navigation.frontierRoute) return;
        DebugExplorationMemory::defer(m,navigation.frontierRoute->passage);
        note("exploration-direction-deferred",Json{{"direction",navigation.frontierRoute->passage},{"evidence",navigation.frontierRoute->evidence},
            {"reason",why},{"target",{navigation.frontierRoute->target.x,navigation.frontierRoute->target.y}}}.dump());
        navigation.frontierRoute.reset();
    };
    // Execution failures are not evidence that an opening has been explored.
    // Keep its identity; the current execution snapshot controls the retry.
    const bool teleport=teleportAvailable(c,false);
    bool restricted=false;
    if(teleport) for(size_t row=0;row<definitions->levels.rows().size();++row)
        if(definitions->levels.number(row,"Id")==area) restricted=definitions->levels.number(row,"Teleport").value_or(0)==2;
    navigation.searchDiagnostics={{"decision",decisions},{"area",*area},{"purpose",purpose},{"strategy","observed-passages"},
        { "from",{c.position.x,c.position.y}},{"terrainRevision",m.revision},{"passages",m.passages.size()},
        {"fromCell",feet},{"sourceMask",m.mask(feet.first,feet.second)},{"sourceStandable",m.stand(feet.first,feet.second)},
        {"unitObservationTiles",m.unitObserved.size()},
        {"unitObservationDirections",std::count_if(m.passages.begin(),m.passages.end(),[](const auto &p){return p.units;})},
        {"mapSketchPrimitives",m.sketch?m.sketch->primitives:0},
        {"roadFollowing",m.followPaths},{"observedRoadTiles",m.paths.size()},{"checkedRoadTiles",m.pathChecked.size()},
        {"deferredDirections",std::count_if(m.passages.begin(),m.passages.end(),[](const auto &p){return p.deferred==p.evidence;})},
        {"selected",nullptr}};
    if(!navigation.frontierRoute) {
        const auto direction=experienceDirection(c,focus);
        navigation.searchDiagnostics["experienceDirection"]=direction?Json(*direction):Json(nullptr);
        navigation.searchDiagnostics["experienceBounds"]=m.expectedBounds?Json(*m.expectedBounds):Json(nullptr);
        auto plan=c.walk.navigationSearch().observedRoute(*area,c.position,teleport?mobilityPolicy.distance:0,restricted,
            {},2,0,false,{},direction,!teleport);
        if(followLocalPadRoute(c,plan)) return false;
        navigation.searchDiagnostics["expandedStandingCells"]=plan.expanded;
        navigation.searchDiagnostics["doorFallback"]=plan.doorFallback;
        if(plan.points.empty()) {
            if(plan.outcome==DebugExplorationMemory::Plan::Outcome::ExecutionBlocked) {
                navigation.searchDiagnostics["result"]="execution-blocked";
                wait("explore-blocked","Known passage is temporarily blocked by execution constraints");return false;
            }
            if(m.commitment) {
                const auto pending=*m.commitment;DebugExplorationMemory::defer(m,pending);
                note("exploration-direction-deferred","Retained passage has no observed route");return false;
            }
            if(openDoor(c,true) || exploreLocalPad(c)) return false;
            if(plan.outcome==DebugExplorationMemory::Plan::Outcome::Unreachable && recoverExplorationLanding(c,m)) return false;
            navigation.searchDiagnostics["result"]=plan.outcome==DebugExplorationMemory::Plan::Outcome::StartUnavailable?"start-collision-unavailable":"no-reachable-pending-passage";
            wait(plan.outcome==DebugExplorationMemory::Plan::Outcome::StartUnavailable?"explore-collision-wait":"explore-wait",
                plan.outcome==DebugExplorationMemory::Plan::Outcome::StartUnavailable?
                    "Current feet have no usable acquired collision; waiting for client publication":
                    "No reachable pending passage in acquired terrain for "+purpose);return false;
        }
        navigation.frontierRoute=FrontierRoute{*area,plan.points.back(),std::move(plan.points),plan.passage,plan.evidence,~size_t{},0,{},{},focus};
        navigation.frontierRoute->transit=plan.outcome==DebugExplorationMemory::Plan::Outcome::Partial;
        if(const auto *p=m.passage(plan.passage)) {
            if(p->road) {
                const int dx=int(navigation.frontierRoute->target.x)-feet.first,dy=int(navigation.frontierRoute->target.y)-feet.second;
                m.heading=std::abs(dx)>=std::abs(dy)?DebugExplorationMemory::Cell{dx>0?1:-1,0}:
                    DebugExplorationMemory::Cell{0,dy>0?1:-1};
            } else m.heading=p->direction;
            m.commitment=p->id;
        }
        note(plan.backtracking?"exploration-backtrack":"exploration-direction-selected",Json{
            {"direction",plan.passage},{"target",{navigation.frontierRoute->target.x,navigation.frontierRoute->target.y}},
            {"observation",m.passage(plan.passage)->road?"road":m.passage(plan.passage)->units?"units":"terrain-probe"},
            {"routeCost",plan.cost},{"pathPoints",navigation.frontierRoute->path.size()},{"purpose",purpose}}.dump());
    }
    auto &route=*navigation.frontierRoute;
    navigation.searchDiagnostics["selected"]={route.target.x,route.target.y};navigation.searchDiagnostics["direction"]=route.passage;
    if(feet.first==route.target.x && feet.second==route.target.y) {
        if(route.transit) {
            if(c.moving) {route.observationSince.reset();wait("explore-observation","Waiting for confirmed relay arrival");return false;}
            if(DebugNavigationSearch::observationStatus(c.town,c.observation.read(),
                c.position,c.moving,route.observationSince)!=DebugNavigationSearch::ObservationStatus::Ready) {
                wait("explore-observation","Waiting for relay room publication");return false;
            }
            navigation.frontierRoute.reset();
            wait("explore-observation","Replanning the retained passage from the observed relay");return false;
        }
        if(!DebugNavigationSearch::passageObservationReady(c.town,c.observation.read(),*m.passage(route.passage),
            route.target,c.moving,route.observationSince)) {
            wait("explore-observation","Waiting for confirmed arrival and client collision publication");return false;
        }
        defer("Reached the opening without a new observation of this direction");return false;
    }
    auto leg=terrainLeg(c,route.path);
    if(!leg.point) {
        // Replan the committed opening's actual standing cells. A radius
        // around the old endpoint can incorrectly accept our current cell
        // while the arrival contract still requires another exact cell.
        auto again=c.walk.navigationSearch().observedRoute(*area,c.position,teleport?mobilityPolicy.distance:0,restricted,
            {},0,route.passage,false,{},{},!teleport);
        if(followLocalPadRoute(c,again)) return false;
        if(!again.points.empty()) {
            const auto endpoint=again.points.back();
            if(route.target!=endpoint) route.observationSince.reset();
            route.target=endpoint;route.evidence=again.evidence;route.path=std::move(again.points);
            route.transit=again.outcome==DebugExplorationMemory::Plan::Outcome::Partial;
            navigation.searchDiagnostics["selected"]={route.target.x,route.target.y};
            if(feet.first==route.target.x && feet.second==route.target.y) {
                wait("explore-observation","At the replanned opening; waiting for confirmed observation");return false;
            }
            leg=terrainLeg(c,route.path);
        }
        if(again.outcome==DebugExplorationMemory::Plan::Outcome::ExecutionBlocked) {
            route.blockedSince={};navigation.searchDiagnostics["result"]="execution-blocked";
            wait("explore-blocked","Retaining the passage while its live route is blocked");return false;
        }
        if(!leg.point) {
            if(route.blockedSince==Clock::time_point{}) route.blockedSince=Clock::now();
            navigation.searchDiagnostics["result"]="no-live-relay";
            wait("explore-blocked","Waiting for a legal live relay for "+purpose);return false;
        }
    }
    route.blockedSince={};
    const size_t remaining=route.path.size()-1-leg.nearest;
    if(remaining+1<route.bestRemaining) {route.bestRemaining=remaining;route.stalls=0;}
    else ++route.stalls;
    navigation.searchDiagnostics["remainingPoints"]=remaining;navigation.searchDiagnostics["leg"]={leg.point->x,leg.point->y};
    wait("explore-frontier",purpose);
    walk(c,{.goal=DebugWalkGoal::Point,.point=OnlinePoint{leg.point->x,leg.point->y},.interact=false,.exploration=true,.exactPoint=true,.purpose="exploration-frontier",.routeRelay=true});
    return false;
}
bool DebugAi::Impl::explore(Context &c,std::string purpose,bool recoveryOnly,ExplorationFocus focus) try {
    if(navigation.localPad && !recoveryOnly && exploreLocalPad(c)) return false;
    refreshNavigationAttempts(c);navigation.mapCacheBuilds=c.town.navigationCacheBuilds();
    return exploreFrontier(c,purpose,recoveryOnly,focus);
} catch(const DebugSearchPending &) {
    c.walk.navigationSearch().noteTravelWait();throw;
}
bool DebugAi::Impl::approachKnown(Context &c,const Selector &selector) {
    if(!area) return false;
    // A real quest-state change can create/move a town NPC. Absence checked
    // before that change must not permanently suppress its known service site.
    // Repeated snapshots with identical bits are not new evidence.
    const auto &questFlags=c.observation.read().world.quests.playerFlags;
    if(navigation.checkedHintQuestFlags!=questFlags) {
        navigation.checkedHintQuestFlags=questFlags;
        std::erase_if(navigation.checkedHints,[](const auto &key){return std::get<1>(key)==1;});
    }
    std::optional<OnlinePoint> hint;
    if(selector.type==1 && !selector.npc) {
        // Moving monsters use per-unit, time-stamped leads. A class-level
        // static landmark cannot describe where a remembered enemy is now.
        const auto population=navigation.populationMemory.find(*area);
        float nearest=std::numeric_limits<float>::max();
        if(population!=navigation.populationMemory.end()) for(const auto &[key,enemy]:population->second.enemies) {
            if(enemy.identity!=selector.identity || (selector.superUnique && enemy.superUnique!=selector.superUnique) ||
                (selector.eliteOnly && !enemy.elite)) continue;
            const auto unit=c.observation.read().world.units.find(key);
            if(unit==c.observation.read().world.units.end()) {
                const auto &rooms=c.town.navigationRooms();
                const auto room=std::find_if(rooms.begin(),rooms.end(),[&](const auto &r) {
                    return r.level==*area && enemy.position.x>=r.x && enemy.position.y>=r.y &&
                        enemy.position.x<r.x+r.width && enemy.position.y<r.y+r.height;
                });
                if(room!=rooms.end()) {
                    const auto roomKey=std::tuple{int(room->level),room->x,room->y};
                    const auto nativeKey=std::tuple{uint8_t(room->level),uint16_t(room->x/5),uint16_t(room->y/5)};
                    if(c.observation.read().world.rooms.contains(nativeKey) && population->second.searchedRooms.contains(roomKey) &&
                        !population->second.settlingRooms.contains(roomKey)) continue;
                }
            }
            const float span=distance(c.position,{float(enemy.position.x),float(enemy.position.y)});
            if(span<nearest) {nearest=span;hint=enemy.position;}
        }
    } else hint=selector.type==5?c.town.knownConnectionPosition(*area,selector.identity):
        c.town.knownTargetPosition(*area,selector.type,selector.identity);
    bool townLandmark=false,experienceLandmark=false;
    if(!hint) {
        hint=experienceHint(c,selector);
        experienceLandmark=hint.has_value();
        townLandmark=hint.has_value() && selector.type==1;
    }
    if(!hint && selector.type==1 && selector.npc) {
        hint=c.town.automapNpcPosition(*area,selector.identity);townLandmark=hint.has_value();
    }
    if(!hint && selector.type==2) hint=c.town.automapTargetPosition(*area,selector.identity);
    if(!hint) return approachTerrainClue(c,selector);
    const auto key=std::tuple{*area,selector.type,selector.identity,hint->x,hint->y};
    if(navigation.checkedHints.contains(key)) return false;
    if(distance(c.position,{float(hint->x),float(hint->y)})<=8) {
        if(townLandmark || experienceLandmark) {
            if(navigation.approachHint!=hint) {navigation.approachHint=hint;navigation.approachObservation.observationSince.reset();}
            if(DebugNavigationSearch::observationStatus(c.town,c.observation.read(),
                c.position,c.moving,navigation.approachObservation.observationSince)!=DebugNavigationSearch::ObservationStatus::Ready) {
                wait("known-target-assignment","Waiting for acquired terrain and the landmark assignment");return true;
            }
        }
        navigation.checkedHints.insert(key);note("known-target-not-reassigned",selector.name);return false;
    }
    return approachPoint(c,*hint,experienceLandmark?"configured landmark "+selector.name:
        townLandmark?"town map service "+selector.name:selector.name);
}
bool DebugAi::Impl::approachPoint(Context &c,OnlinePoint hint,std::string purpose,float vicinity,bool exploration,
        std::optional<OnlineUnitKey> interaction,std::optional<std::vector<DebugExplorationMemory::Cell>> stances) try {
    const Vec wanted{float(hint.x),float(hint.y)};
    const auto feet=DebugExplorationMemory::playerCell(c.position);
    const Vec currentCell{float(feet.first),float(feet.second)};
    const float tolerance=std::max(0.f,vicinity);
    // Route nodes and network point goals use native integer feet. Use that
    // same domain for arrival, including zero-radius coordinate goals.
    const bool arrived=interaction?c.town.interactionReady(c.observation.read(),*interaction,c.position):stances?
        std::find(stances->begin(),stances->end(),feet)!=stances->end():
        distance(currentCell,wanted)<=tolerance;
    if(arrived) return false;
    if(interaction && !c.walk.recoveringMovement() &&
        DebugNavigationSearch::directNpcApproach(c.town,c.observation.read(),*interaction,c.position)) {
        // Refine a map relay into a real unit click as soon as its approach
        // is executable. Do this before retaining the active point child.
        navigation.approachDiagnostics={{"decision",decisions},{"purpose",purpose},{"target",{hint.x,hint.y}},
            {"strategy","direct-npc"},{"result","interaction-ray-confirmed"},{"from",{c.position.x,c.position.y}}};
        return false;
    }
    const auto assigned=interaction?c.observation.read().world.units.find(*interaction):c.observation.read().world.units.end();
    const uint64_t assignment=assigned==c.observation.read().world.units.end()?0:assigned->second.assignmentRevision;
    // A map/history hint becoming an actual nearby assignment refines the
    // same destination; keep its already submitted relay while adopting identity.
    if(interaction && !navigation.approachObservation.unit && navigation.approachHint &&
        distance({float(navigation.approachHint->x),float(navigation.approachHint->y)},wanted)<=8) {
        navigation.approachObservation.unit=interaction;navigation.approachObservation.assignment=assignment;
    }
    const bool sameGoal=navigation.approachHint && navigation.approachObservation.unit==interaction && navigation.approachObservation.assignment==assignment &&
        (interaction || navigation.approachHint==hint);
    if(!sameGoal) {navigation.approachObservation={};navigation.approachObservation.unit=interaction;navigation.approachObservation.assignment=assignment;}
    auto *memory=observeTerrain(c);if(!memory || !area) {wait("known-target-observation","Waiting for observed terrain");return true;}
    if(sameGoal && navigation.approachFrontier && bool(navigation.walkIntent) && navigation.walkIntent.purpose.value_or(std::string{})=="known-target-memory" &&
        (navigation.walkIntent.point?navigation.walkIntent.point->x:0)==navigation.approachFrontier->x && (navigation.walkIntent.point?navigation.walkIntent.point->y:0)==navigation.approachFrontier->y) {
        // Extend a geometric relay before its last few cells, while the
        // current native path is still moving. Observation and interaction
        // endpoints retain their own arrival/receipt requirements.
        const bool extend=c.walk.active() && !interaction && !navigation.approachObservation.passage &&
            *navigation.approachFrontier!=navigation.approachObservation.endpoint &&
            distance(currentCell,{float(navigation.approachFrontier->x),float(navigation.approachFrontier->y)})<=6;
        if(c.walk.active() && !extend) {wait("known-target-approach",purpose);return true;}
        const auto child=c.walk.observe();const auto state=child.state;
        if(state==DebugWalkState::NeedsReplan) {
            navigation.approachObservation.relayRecovery=OnlinePoint{uint16_t(feet.first),uint16_t(feet.second)};
            navigation.approachObservation.relayTerrain=c.town.navigationSnapshot();
            navigation.approachObservation.observationSince.reset();
            note("known-target-relay-replan",Json{{"target",{hint.x,hint.y}},
                {"direction",navigation.approachObservation.passage},{"walk",c.walk.snapshot()}}.dump());
        }
        const bool failed=state==DebugWalkState::Failed;
        if(!extend) cancelWalk(c);
        if(failed) {
            auto &observation=navigation.approachObservation;
            const auto at=c.observation.read().world.playerPosition.value_or(OnlinePoint{uint16_t(feet.first),uint16_t(feet.second)});
            if(!observation.failedRelayFrom ||
                distance(Vec{float(observation.failedRelayFrom->x),float(observation.failedRelayFrom->y)},Vec{float(at.x),float(at.y)})>=2) {
                observation.failedRelayFrom=at;observation.failedRelays=0;
            }
            ++observation.failedRelays;
            note("known-target-relay-failed",Json{{"target",{hint.x,hint.y}},{"position",{at.x,at.y}},
                {"attempts",observation.failedRelays},{"reason",child.reason}}.dump());
            if(observation.failedRelays>=3)
                throw std::runtime_error("Known-target walking relay repeatedly failed without position progress: "+child.reason);
            navigation.suppressedHints[{hint.x,hint.y}]=Clock::now()+std::chrono::seconds(3);
            navigation.approachObservation.observationSince.reset();
        }
    }
    if(frontierWalking(c)) {explore(c,"retained passage before approaching "+purpose,true);return true;}
    std::erase_if(navigation.suppressedHints,[](const auto &p){return Clock::now()>=p.second;});
    if(navigation.suppressedHints.contains({hint.x,hint.y})) {wait("known-target-retry","Waiting for blocked route execution");return true;}
    auto &approach=navigation.approachObservation;
    if(approach.relayRecovery) {
        const auto anchor=*approach.relayRecovery;
        const auto terrain=c.town.navigationSnapshot();
        if(std::max(std::abs(feet.first-int(anchor.x)),std::abs(feet.second-int(anchor.y)))>8 ||
            !terrain || !approach.relayTerrain ||
            !terrain->sameGlobalRegion(*approach.relayTerrain,int(anchor.x)-8,int(anchor.y)-8,
                int(anchor.x)+8,int(anchor.y)+8)) {
            approach.relayRecovery.reset();approach.relayTerrain.reset();
        }
    }
    if(approach.passage) {
        const auto *p=memory->passage(approach.passage);
        if(!p || p->evidence!=approach.evidence || p->deferred==p->evidence) {
            approach.passage=0;approach.observationSince.reset();
        } else if(feet.first==approach.endpoint.x && feet.second==approach.endpoint.y) {
            if(!DebugNavigationSearch::passageObservationReady(c.town,c.observation.read(),*p,
                approach.endpoint,c.moving,approach.observationSince)) {
                wait("known-target-observation","Waiting for acquired collision toward "+purpose);return true;
            }
            DebugExplorationMemory::defer(*memory,approach.passage);
            approach.passage=0;approach.observationSince.reset();
        }
    }
    bool restricted=false;const bool teleport=teleportAvailable(c,false);
    if(teleport) for(size_t row=0;row<definitions->levels.rows().size();++row)
        if(definitions->levels.number(row,"Id")==area) restricted=definitions->levels.number(row,"Teleport").value_or(0)==2;
    auto approaches=std::move(stances);
    if(interaction) approaches=c.walk.navigationSearch().interactionApproaches(c.town,c.observation.read(),*interaction);
    const auto plan=c.walk.navigationSearch().observedRoute(*area,c.position,teleport?mobilityPolicy.distance:0,restricted,
        wanted,tolerance,approach.passage,true,std::move(approaches),{},!teleport && !navigation.localPad);
    if(followLocalPadRoute(c,plan)) return true;
    const auto leg=terrainLeg(c,plan.points);
    const bool atEndpoint=!plan.points.empty() && feet.first==plan.points.back().x && feet.second==plan.points.back().y;
    navigation.approachDiagnostics={{"decision",decisions},{"purpose",purpose},{"target",{hint.x,hint.y}},
        {"strategy","observed-route"},{"expanded",plan.expanded},{"direction",plan.passage},
        {"result",plan.outcome==DebugExplorationMemory::Plan::Outcome::ExecutionBlocked?"execution-blocked":
            plan.points.empty()?"no-executable-route":!leg.point && !(plan.passage && atEndpoint)?"no-live-relay":
            plan.passage?"need-observation":"executable-route"},
        {"pathPoints",plan.points.size()},{"doorFallback",plan.doorFallback},{"from",{c.position.x,c.position.y}},
        {"fromCell",feet},{"sourceMask",memory->mask(feet.first,feet.second)},{"sourceStandable",memory->stand(feet.first,feet.second)},
        {"relay",leg.point?Json{leg.point->x,leg.point->y}:Json(nullptr)},
        {"endpoint",plan.points.empty()?Json(nullptr):Json{plan.points.back().x,plan.points.back().y}},
        {"requiresTeleport",leg.teleport},{"nearestRouteIndex",leg.nearest},{"localRelayRecovery",approach.relayRecovery.has_value()}};
    if(interaction && plan.outcome==DebugExplorationMemory::Plan::Outcome::Goal &&
        (atEndpoint || (leg.point && !leg.teleport && *leg.point==plan.points.back()))) {
        const bool nativeReady=c.town.nativeInteractionApproach(c.observation.read(),*interaction,c.position);
        navigation.approachDiagnostics["nativeApproachReady"]=nativeReady;
        if(nativeReady) {
            navigation.approachDiagnostics["result"]="interaction-route-confirmed";
            note("known-target-interaction-ready",Json{{"target",{hint.x,hint.y}},{"routeCost",plan.cost},
                {"standingPosition",{plan.points.back().x,plan.points.back().y}}}.dump());
            approach.passage=0;approach.observationSince.reset();return false;
        }
        navigation.approachDiagnostics["result"]="native-approach-needs-relay";
    }
    if(plan.passage && !plan.points.empty() && feet.first==plan.points.back().x && feet.second==plan.points.back().y) {
        approach.passage=plan.passage;approach.evidence=plan.evidence;approach.endpoint=plan.points.back();
        approach.observationSince.reset();navigation.approachHint=hint;
        wait("known-target-observation","Observing the reached passage toward "+purpose);return true;
    }
    if(!leg.point) {
        if(approach.passage && plan.outcome==DebugExplorationMemory::Plan::Outcome::Unreachable) {
            DebugExplorationMemory::defer(*memory,approach.passage);approach.passage=0;
        }
        wait(plan.points.empty() && plan.outcome!=DebugExplorationMemory::Plan::Outcome::ExecutionBlocked?
            "known-target-observation":"known-target-blocked",
            plan.points.empty()?"No currently executable known route or pending passage toward "+purpose:
                "Known route has no legal live relay toward "+purpose);return true;
    }
    if(plan.passage && approach.passage!=plan.passage)
        note("known-target-direction-selected",Json{{"target",{hint.x,hint.y}},{"direction",plan.passage},
            {"approach",{plan.points.back().x,plan.points.back().y}},{"routeCost",plan.cost}}.dump());
    approach.passage=plan.passage;approach.evidence=plan.evidence;approach.endpoint=plan.points.back();
    approach.observationSince.reset();
    wait(plan.passage?"known-target-observation":"known-target-memory",
        plan.passage?"Exploring the observed passage toward "+purpose:"Following observed terrain toward "+purpose);
    walk(c,{.goal=DebugWalkGoal::Point,.point=OnlinePoint{leg.point->x,leg.point->y},.interact=false,.exploration=exploration || plan.passage!=0,.exactPoint=true,.recoverableRelay=c.town.read().town && !approach.relayRecovery,.adjustLane=!approach.relayRecovery,.purpose="known-target-memory",.routeRelay=true});
    navigation.approachHint=hint;navigation.approachFrontier=leg.point;
    return true;
} catch(const DebugSearchPending &) {
    c.walk.navigationSearch().noteTravelWait();throw;
}
// WalkExit already owns a resolved walking connection. As its rooms load,
// reconsider the actual floor on the far side before probing another frontier.
bool DebugAi::Impl::tryTeleportAcrossExit(Context &c,uint16_t to) {
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    if(c.town.read().town || !map || !origin) return false;
    const TeleportTargets neighborhood(c);
    if(!teleportAvailable(c)) return false;
    bool allowed=false;
    for(size_t row=0;row<definitions->levels.rows().size();++row) if(definitions->levels.number(row,"Id")==to) {
        allowed=definitions->levels.number(row,"Teleport").value_or(0)!=0;break;
    }
    if(!allowed) return false;
    std::optional<OnlinePoint> landing;float best=std::numeric_limits<float>::max();
    const auto observed=c.town.knownNavigationTerrain(to);
    const std::set<std::pair<int,int>> visible(observed.begin(),observed.end());
    const auto seen=[&](int x,int y) {return x>=0 && y>=0 && visible.contains({x/5,y/5});};
    for(const auto &[tx,ty]:observed) for(int dy=0;dy<5;++dy) for(int dx=0;dx<5;++dx) {
        const int x=tx*5+dx,y=ty*5+dy;
        if(x<0 || y<0 || x>65535 || y>65535 || !seen(x-1,y) || !seen(x+1,y) || !seen(x,y-1) || !seen(x,y+1)) continue;
        const Vec at{float(x),float(y)};const OnlinePoint point{uint16_t(x),uint16_t(y)};
        const float span=distance(at,c.position);
        const auto trial=std::tuple{area.value_or(0),point.x,point.y,point.x,point.y};
        if(span<2 || !neighborhood.permits(c,at,mobilityPolicy.distance) ||
            frontierCoolingDown(point,3) ||
            (navigation.mobilityTrials.contains(trial) && navigation.mobilityTrials.at(trial)>=2)) continue;
        if(span<best) {best=span;landing=point;}
    }
    if(!landing) return false;
    navigation.travelDiagnostics["result"]="observed-neighbor-teleport";
    navigation.travelDiagnostics["landing"]={landing->x,landing->y};
    wait("travel-teleport","Crossing to actually observed adjacent-level floor with normal Teleport");
    walk(c,{.goal=DebugWalkGoal::Point,.point=OnlinePoint{landing->x,landing->y},.interact=false,.purpose="known-target-teleport",.routeRelay=true});
    return true;
}
bool DebugAi::Impl::approachWalkingConnection(Context &c,uint16_t destination) {
    const auto *memory=observeTerrain(c);
    const auto connection=c.walk.navigationSearch().walkingConnection(c.town,c.position,destination);
    if(!connection.bearing) return false;
    const auto origin=c.town.read().origin;
    if(!memory || !origin) return true;
    const Vec offset{float(origin->x),float(origin->y)};
    const auto live=c.town.navigationGrid(c.observation.read());
    const auto feet=DebugExplorationMemory::playerCell(c.position);
    const Vec startCell{float(feet.first),float(feet.second)};
    const auto local=[&](OnlinePoint at) {return Vec{float(at.x),float(at.y)}-offset+Vec{.5f,.5f};};
    const auto inDestination=[&](OnlinePoint at) {
        const auto owner=memory->terrainLevels.find({at.x/5,at.y/5});
        return owner!=memory->terrainLevels.end() && owner->second==destination;
    };
    // The seam is an alternative set of openings, not a mandatory exact
    // waypoint. As soon as a local crossing is executable, click acquired
    // floor beyond it without first stopping on the source-side cell.
    std::vector<OnlinePoint> crossings;
    for(const auto &opening:connection.openings) if(opening.crossing) {
        const int dx=int(opening.crossing->x)-opening.point.x,dy=int(opening.crossing->y)-opening.point.y;
        for(int depth:{2,1}) {
            const int x=int(opening.crossing->x)+dx*depth,y=int(opening.crossing->y)+dy*depth;
            if(x<0 || y<0 || x>65535 || y>65535) continue;
            const OnlinePoint at{uint16_t(x),uint16_t(y)};
            if(distance(startCell,{float(x),float(y)})>18 || !inDestination(at) ||
                !memory->stand(x,y) || !live.nativeMovementSegment(local(*opening.crossing),local(at),playerMovement)) continue;
            crossings.push_back(at);
        }
    }
    const bool crossingIntent=navigation.walkIntent.purpose.value_or(std::string{})=="walking-connection-crossing" &&
        navigation.walkIntent.level==destination;
    std::stable_sort(crossings.begin(),crossings.end(),[&](OnlinePoint a,OnlinePoint b) {
        const bool retainedA=crossingIntent && navigation.walkIntent.point==a;
        const bool retainedB=crossingIntent && navigation.walkIntent.point==b;
        if(retainedA!=retainedB) return retainedA;
        return distance(c.position,{float(a.x),float(a.y)})<distance(c.position,{float(b.x),float(b.y)});
    });
    const Vec from=c.position-offset+Vec{.5f,.5f};
    for(const auto at:crossings) {
        const auto path=live.nativePlayerPath(from,local(at),playerMovement);
        if(path.empty()) continue;
        const auto end=path.back()+offset;
        if(end.x<0 || end.y<0 || end.x>=65536 || end.y>=65536) continue;
        const OnlinePoint reached{uint16_t(end.x),uint16_t(end.y)};
        // A native coordinate path may stop one cell short. Its actual last
        // cell must still cross the boundary, not stop beside an occupied lip.
        if(!inDestination(reached) || distance({float(reached.x),float(reached.y)},{float(at.x),float(at.y)})>1) continue;
        if(!crossingIntent || navigation.walkIntent.point!=at || !c.walk.active()) {
            note("walking-connection-route",Json{{"destination",destination},{"from",{c.position.x,c.position.y}},
                {"target",{at.x,at.y}},{"nativeEndpoint",{reached.x,reached.y}},{"reselected",crossingIntent}}.dump());
            if(!c.walk.active()) cancelWalk(c);
        }
        navigation.travelDiagnostics["result"]="live-walking-crossing";
        navigation.travelDiagnostics["crossingTarget"]={at.x,at.y};
        navigation.travelPhase=TravelPhase::WalkExit;
        wait("travel-crossing","Walking through a currently clear opening to "+std::to_string(destination));
        walk(c,{.goal=DebugWalkGoal::Point,.point=at,.level=destination,.interact=false,
            .purpose="walking-connection-crossing",.routeRelay=true});
        return true;
    }
    std::vector<DebugExplorationMemory::Cell> stances;
    const bool blockedApproach=c.walk.blockedByUnit() && navigation.approachFrontier &&
        navigation.walkIntent.purpose.value_or(std::string{})=="known-target-memory" &&
        navigation.walkIntent.point==navigation.approachFrontier &&
        *navigation.approachFrontier==navigation.approachObservation.endpoint;
    const auto residentFootprint=[&](Vec position) {
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if(nativeCollisionSizeCell(playerMovement.size,dx,dy) &&
                live.movementMask(int(position.x)+dx,int(position.y)+dy)==0xffff) return false;
        return true;
    };
    for(const auto &opening:connection.openings) {
        // The child already tried a local native bypass. Its endpoint can
        // be empty while the way to it is blocked; reconsider other openings
        // now instead of retaining that point until the child times out.
        if(blockedApproach && opening.point==navigation.approachFrontier) continue;
        const auto at=local(opening.point);
        // Only current, resident occupancy can veto an approach. This
        // execution choice never marks an opening as impassable terrain.
        const bool resident=distance(c.position,{float(opening.point.x),float(opening.point.y)})<=18 &&
            residentFootprint(at) && (!opening.crossing || residentFootprint(local(*opening.crossing)));
        if(resident && (!live.walkable(at,playerMovement) || (opening.crossing &&
            !live.nativeMovementSegment(at,local(*opening.crossing),playerMovement)))) continue;
        stances.emplace_back(opening.point.x,opening.point.y);
    }
    navigation.travelDiagnostics["connectionOpenings"]=connection.openings.size();
    navigation.travelDiagnostics["availableConnectionOpenings"]=stances.size();
    navigation.travelDiagnostics["connectionBearing"]={connection.bearing->x,connection.bearing->y};
    if(!connection.openings.empty() && stances.empty()) {
        wait("walking-connection-blocked","All observed openings are currently occupied; waiting for a clear route");
        return true;
    }
    if(crossingIntent || blockedApproach) {
        cancelWalk(c);navigation.approachFrontier.reset();
        navigation.suppressedHints.erase({connection.bearing->x,connection.bearing->y});
        note("walking-connection-replan","Selecting another observed opening from current unit occupancy");
    }
    const auto purpose="walking connection to "+std::to_string(destination);
    const bool pending=approachPoint(c,*connection.bearing,purpose,0,false,{},std::move(stances));
    // Preserve normal failed-edge recovery when the shared planner proves no
    // route or pending direction. Temporary execution blocking still waits.
    return pending && !(navigation.approachDiagnostics.is_object() && navigation.approachDiagnostics.value("decision",uint64_t{})==decisions &&
        navigation.approachDiagnostics.value("purpose",std::string{})==purpose &&
        navigation.approachDiagnostics.value("result",std::string{})=="no-executable-route");
}
bool DebugAi::Impl::travel(Context &c,uint16_t destination,bool allowWaypoint) {
    if(!area) return false;
    if(*area==destination) {
        cancelWalk(c);navigation.travelDestination.reset();navigation.travelRoute.clear();navigation.travelVisitedAreas.clear();navigation.travelPhase=TravelPhase::Route;navigation.legDestination.reset();return true;
    }
    const bool awaitingTransfer=navigation.legDestination && navigation.legDestination!=area &&
        (navigation.travelPhase==TravelPhase::WaypointWait || navigation.travelPhase==TravelPhase::CrossWait);
    if(!awaitingTransfer && waypointBeforeDeparture(c)) return false;
    if(allowWaypoint && !c.town.read().town) {
        std::optional<int> sourceAct,destinationAct;bool sourceWaypoint=false;
        for(size_t row=0;row<definitions->levels.rows().size();++row) {
            const auto level=definitions->levels.number(row,"Id");
            if(level==area) {
                sourceAct=definitions->levels.number(row,"Act");
                const auto number=definitions->levels.number(row,"Waypoint");
                sourceWaypoint=number && *number>=0 && *number<112;
            }
            if(level==destination) destinationAct=definitions->levels.number(row,"Act");
        }
        if(sourceAct && destinationAct && sourceAct!=destinationAct && !sourceWaypoint && route(c,*area,destination).empty()) {
            // A waypoint journey may begin on a level without a source.
            // Retain reachTown's normal portal/walking continuation before
            // changing travelDestination; an observed direct portal wins.
            reachTown(c);return false;
        }
    }
    if(navigation.travelDestination!=destination) {
        navigation.connectionStalls.clear();
        // Failed connections are knowledge of this game, not this destination.
        // Clearing them here makes a quest switch undo the cost that caused it.
        navigation.unavailableWaypointArea.reset();cancelWalk(c);navigation.travelDestination=destination;navigation.travelPhase=TravelPhase::Route;navigation.legDestination.reset();
        navigation.waypointDestination.reset();navigation.legUnit.reset();navigation.travelRoute.clear();navigation.travelVisitedAreas.clear();
    }
    navigation.travelVisitedAreas.insert(*area);
    if(navigation.legDestination && *area==*navigation.legDestination) {
        cancelWalk(c);navigation.travelPhase=TravelPhase::Route;navigation.legDestination.reset();navigation.waypointDestination.reset();navigation.legUnit.reset();
    }
    if(!navigation.travelRoute.empty() && navigation.travelRoute.front()!=*area) {
        const auto reached=std::find(navigation.travelRoute.begin(),navigation.travelRoute.end(),int(*area));
        if(reached==navigation.travelRoute.end()) {
            note("travel-route-deviation","Actual area is outside the retained route; planning from the observed arrival");
            navigation.travelRoute.clear();
        } else navigation.travelRoute.erase(navigation.travelRoute.begin(),reached);
    }
    if(navigation.travelRoute.size()>1) {
        const auto &connections=c.town.navigationConnections();
        const auto edge=connections.find(*area);
        const bool assignedPortal=std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &target) {
            return target.destination==navigation.travelRoute[1] && ordinaryConnection(target,c.observation.read());
        });
        if(!assignedPortal && (edge==connections.end() || std::find(edge->second.begin(),edge->second.end(),navigation.travelRoute[1])==edge->second.end())) {
            note("travel-route-invalidated","The next retained connection is absent from the current resolved graph");
            navigation.travelRoute.clear();
        }
    }
    if(navigation.travelRoute.empty()) {
        navigation.travelRoute=route(c,*area,destination);
        if(!navigation.travelRoute.empty()) note("travel-route-planned",Json(navigation.travelRoute).dump());
    }
    const auto now=Clock::now();
    const auto &targets=c.town.read().mapTargets;
    const auto localWaypoint=std::find_if(targets.begin(),targets.end(),[&](const auto &entry) {
        return entry.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(entry.unit)==area;
    });
    const bool allowWaypointHere=allowWaypoint && area!=navigation.unavailableWaypointArea;
    const auto waypointPosition=[&]() -> std::optional<OnlinePoint> {
        if(localWaypoint!=targets.end()) return localWaypoint->position;
        return c.town.knownWaypointPosition(*area);
    };
    const auto levelAct=[&](int level) -> std::optional<int> {
        for(size_t row=0;row<definitions->levels.rows().size();++row)
            if(definitions->levels.number(row,"Id")==level) return definitions->levels.number(row,"Act");
        return {};
    };
    const auto sourceAct=levelAct(*area),destinationAct=levelAct(destination);
    const bool anotherAct=sourceAct && destinationAct && sourceAct!=destinationAct;
    const auto canObserveDestinationAct=[&](uint16_t level) {
        return anotherAct && destinationAct==levelAct(level);
    };
    const auto chooseWaypoint=[&]() -> std::optional<uint16_t> {
        const auto source=waypointPosition();if(!source) return {};
        const auto &current=navigation.travelRoute;
        float shortest=routeCost(c,current);const float ordinaryCost=shortest;
        std::optional<uint16_t> best,observationWaypoint;
        const bool ordinaryAvailable=std::isfinite(shortest);
        const bool recoveringRoute=current.size()>1 && navigation.failedWalkEdges.contains({*area,current[1]}) && navigation.failedWalkEdges.at({*area,current[1]})>=2;
        const float approach=navigationCost(c,*area,c.position,source);
        navigation.travelDiagnostics={{"from",*area},{"destination",destination},{"ordinaryRoute",current},
            {"movement",travelMobility(c,*area).first>0?"teleport":"walk"},{"visitedAreas",navigation.travelVisitedAreas},
            {"ordinaryCost",std::isfinite(shortest)?Json(shortest):Json(nullptr)},
            {"waypointApproachCost",approach},{"waypoints",Json::array()},{"selectedWaypoint",nullptr}};
        for(const auto level:navigation.knownWaypointAreas) if(level!=*area) {
            // Do not replace a viable committed route with a waypoint back
            // into its own visited prefix on every new estimate. An absent
            // ordinary route still permits every legal recovery waypoint.
            if(ordinaryAvailable && !recoveringRoute && navigation.travelVisitedAreas.contains(level)) {
                navigation.travelDiagnostics["waypoints"].push_back({{"level",level},{"result","visited-on-current-journey"}});
                continue;
            }
            const auto landing=c.town.knownWaypointPosition(level);
            const auto start=landing?std::optional<Vec>{{float(landing->x),float(landing->y)}}:std::nullopt;
            const auto rest=route(c,level,destination,start);
            // A newly joined client may not have materialized the other act's
            // connections yet. An owned waypoint is still a legal transfer:
            // observe that act, then replan from its actual scene. This is a
            // fallback, not an invented onward edge or a same-act retry loop.
            if(!ordinaryAvailable && rest.empty() && !observationWaypoint && canObserveDestinationAct(level))
                observationWaypoint=level;
            const float cost=approach+16+routeCost(c,rest,start);
            navigation.travelDiagnostics["waypoints"].push_back({{"level",level},{"route",rest},
                {"cost",std::isfinite(cost)?Json(cost):Json(nullptr)}});
            if(cost<shortest && worthwhileTransfer(cost,ordinaryCost)) {shortest=cost;best=level;}
        }
        if(!best && observationWaypoint) {
            best=observationWaypoint;
            navigation.travelDiagnostics["result"]="observe-destination-act-via-unlocked-waypoint";
        }
        if(best) navigation.travelDiagnostics["selectedWaypoint"]=*best;
        return best;
    };
    if(navigation.travelPhase==TravelPhase::WaypointWait || navigation.travelPhase==TravelPhase::CrossWait) {
        if(navigation.travelPhase==TravelPhase::CrossWait && !navigation.legUnit && !c.moving &&
            retryPassage(Passage::Transfer,"resume ordinary walking boundary approach")) {
            // A walking pointer can stop at a newly occupied seam. It has no
            // irreversible service result to replay: choose a fresh ordinary
            // approach from current client collision instead of waiting forever.
            if(navigation.legDestination) {
                auto &failures=navigation.failedWalkEdges[{*area,*navigation.legDestination}];failures=std::min(failures+1,16u);
            }
            cancelWalk(c);navigation.travelRoute.clear();navigation.travelPhase=TravelPhase::Route;navigation.legDestination.reset();navigation.exitRetryAt=now+std::chrono::seconds(1);
            note("walking-crossing-stopped","Replanning the ordinary boundary approach from observed occupancy");return false;
        }
        wait("travel-wait","Waiting for the destination's native scene assignment");
        if(now>=actionDeadline) throw std::runtime_error("Travel was submitted but the destination was not observed");
        return false;
    }
    if(navigation.travelPhase==TravelPhase::WaypointOpen) {
        const bool open=c.observation.read().world.waypointSource && c.observation.read().world.waypointHistory &&
            std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry) {
                return entry.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(entry.unit)==area &&
                    entry.unit.id==*c.observation.read().world.waypointSource && c.observation.read().world.units.contains(entry.unit);
            });
        if(!open) {
            if(localWaypoint==targets.end()) {
                if(const auto hint=c.town.knownWaypointPosition(*area);hint && approachPoint(c,*hint,"known waypoint")) return false;
                if(navigation.travelRoute.size()>1 && !(navigation.explorationReturn && navigation.explorationReturn->returning)) {
                    navigation.unavailableWaypointArea=area;navigation.waypointDestination.reset();navigation.travelPhase=TravelPhase::Route;cancelWalk(c);
                    note("waypoint-source-unavailable","Using the ordinary route until the source is assigned");return false;
                }
            }
            wait("waypoint","Opening the actual assigned waypoint");
            if(teleportAvailable(c)) {
                if(localWaypoint==targets.end()) {explore(c,"assignment of the waypoint",false,ExplorationFocus::Units);return false;}
                if(!walk(c,{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{localWaypoint->unit.type,localWaypoint->unit.id},.interact=true,.confirmMenu=true,.purpose="travel-waypoint"})) return false;
            } else if(!walk(c,{.goal=DebugWalkGoal::Waypoint,.purpose="travel-waypoint"})) return false;
        }
        if(navigation.waypointDestination && !navigation.knownWaypointAreas.contains(*navigation.waypointDestination)) {
            if(navigation.explorationReturn && navigation.explorationReturn->returning)
                throw std::runtime_error("The actual waypoint menu did not confirm the exploration return destination");
            note("waypoint-memory-corrected","The actual menu disproved the planned unlock; reselecting from current knowledge");
            navigation.waypointDestination.reset();
        }
        if(!navigation.waypointDestination) navigation.waypointDestination=chooseWaypoint();
        if(!navigation.waypointDestination) {
            c.control.useWaypoint(0);cancelWalk(c);navigation.travelPhase=TravelPhase::Route;return false;
        }
        const auto entry=std::find_if(c.town.read().waypoints.begin(),c.town.read().waypoints.end(),
            [&](const auto &v){return v.level==navigation.waypointDestination && v.unlocked;});
        if(entry==c.town.read().waypoints.end()) throw std::runtime_error("Waypoint destination is not unlocked");
        if(c.control.useWaypoint(entry->level,entry->number)) {
            navigation.legDestination=entry->level;navigation.travelPhase=TravelPhase::WaypointWait;setActionDeadline(std::chrono::seconds(20));cancelWalk(c);
            note("waypoint-request-submitted","from="+std::to_string(*area)+" to="+std::to_string(entry->level));
        }
        return false;
    }
    if(navigation.travelPhase==TravelPhase::WalkExit || navigation.travelPhase==TravelPhase::WarpExit) {
        const bool warp=navigation.travelPhase==TravelPhase::WarpExit;
        if(!warp && approachTownGate(c,*navigation.legDestination)) return false;
        if(warp && navigation.legUnit && std::none_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){return t.unit==navigation.legUnit;})) {
            if(c.walk.active() && bool(navigation.walkIntent) && navigation.walkIntent.goal==DebugWalkGoal::Point) {
                wait("known-target-approach","Continuing the observed exit's position approach while its assignment is outside the client scene");
                return false;
            }
            cancelWalk(c);navigation.travelPhase=TravelPhase::Route;navigation.legUnit.reset();note("exit-assignment-withdrawn");return false;
        }
        if(!warp && tryTeleportAcrossExit(c,*navigation.legDestination)) return false;
        if(!warp && approachWalkingConnection(c,*navigation.legDestination)) return false;
        if(!warp && bool(navigation.walkIntent) && navigation.walkIntent.goal==DebugWalkGoal::Exit && c.walk.observe().state==DebugWalkState::Failed) {
            auto &failures=navigation.failedWalkEdges[{*area,*navigation.legDestination}];
            failures=std::min(failures+1,16u);
            note("walking-connection-not-yet-reachable",std::to_string(*area)+"->"+std::to_string(*navigation.legDestination)+
                " attempt "+std::to_string(failures)+": "+c.walk.observe().reason);
            navigation.exitRetryAt=now+std::chrono::seconds(5);
            cancelWalk(c);navigation.travelPhase=TravelPhase::Route;navigation.travelRoute.clear();
            if(failures>=2) {
                navigation.travelRoute=route(c,*area,destination);
                if(navigation.travelRoute.size()>1 && navigation.travelRoute[1]!=*navigation.legDestination) {
                    navigation.legDestination.reset();note("travel-alternate-route",Json(navigation.travelRoute).dump());return false;
                }
            }
            explore(c,"unobserved boundary toward level "+std::to_string(*navigation.legDestination),false,ExplorationFocus::Connection);return false;
        }
        wait("travel-exit","Approaching the connection to "+std::to_string(*navigation.legDestination));
        if(warp && navigation.legUnit && navigation.legUnit->type==2) {
            const auto &world=c.observation.read().world;const auto unit=world.units.find(*navigation.legUnit);
            if(unit!=world.units.end() && unit->second.mode==0)
                for(size_t row=0;row<definitions->objects.rows().size();++row)
                    if(definitions->objects.number(row,"Id")==unit->second.classId && definitions->objects.number(row,"OperateFn")==16) {
                        if(navigation.pendingDoor) {wait("door-wait","Waiting for the trapdoor to open before entering");return false;}
                        walk(c,{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{2,navigation.legUnit->id},.interact=true,.purpose="door"});
                        return false;
                    }
        }
        DebugWalkIntent intent=warp?DebugWalkIntent{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{navigation.legUnit->type,navigation.legUnit->id},.interact=true}:
            DebugWalkIntent{.goal=DebugWalkGoal::Exit,.level=*navigation.legDestination,.interact=false};
        if(!walk(c,intent)) return false;
        if(warp && navigation.legUnit) rememberTravel(c,*navigation.legUnit);
        const auto observed=c.walk.observe();
        if(!warp) {
            const auto &t=observed.crossingTarget;
            if(!t) throw std::runtime_error("Walking exit has no planned crossing target");
            int x=t->x,y=t->y;
            if(x<0 || y<0 || x>65535 || y>65535) throw std::runtime_error("Exit crossing outside native coordinates");
            const auto origin=c.town.read().origin;
            if(origin) {
                const Vec offset{float(origin->x),float(origin->y)};
                const auto grid=c.town.navigationGrid(c.observation.read());
                const Vec crossing=Vec{float(x),float(y)}-offset+Vec{.5f,.5f};
                const bool known=grid.movementMask(int(crossing.x),int(crossing.y))!=0xffff;
                if(known && !grid.nativeMovementSegment(c.position-offset+Vec{.5f,.5f},crossing,playerMovement)) {
                    cancelWalk(c);
                    note("walking-crossing-occupied","Selecting another opening after new client unit/terrain updates");return false;
                }
            }
            // The ordinary move is still subject to client-map and server gates.
            const auto owner=DebugActionOwner::Navigation;
            const auto action="walking-crossing:"+std::to_string(*area)+"->"+std::to_string(*navigation.legDestination)+
                "@"+std::to_string(x)+","+std::to_string(y);
            const auto result=submitInput(c,owner,action,DebugMotion,
                [&]{return c.control.move({uint16_t(x),uint16_t(y)},true,{},c.position);},[&]{return c.control.reason();});
            if(result==DebugSubmitResult::Waiting) return false;
            if(result==DebugSubmitResult::Expired)
                throw std::runtime_error("Exit crossing input was not accepted: "+submissionWaits.at(owner).reason);
            // The approach already completed. Keep the newly accepted crossing pointer alive.
            navigation.walkIntent={};
        } else cancelWalk(c);
        ++interactions;navigation.travelPhase=TravelPhase::CrossWait;setActionDeadline(std::chrono::seconds(eventPolicy.waitSeconds));
        if(warp) actionDeadline=navigation.passageWaits.at(Passage::Transfer).deadline;
        else actionDeadline=beginPassage(Passage::Transfer,std::chrono::milliseconds(eventPolicy.initialWaitMs),actionDeadline).deadline;
        return false;
    }
    // Costs for unknown entry points are estimates. Repricing after every
    // boundary can prefer the area just left and create a two-area loop.
    // Retain the chosen sequence until progress, a real deviation, a resolved
    // missing connection, or an explicit approach failure changes the plan.
    const auto &path=navigation.travelRoute;
    const float ordinaryCost=routeCost(c,path);
    navigation.travelDiagnostics={{"from",*area},{"destination",destination},{"ordinaryRoute",path},
        {"movement",travelMobility(c,*area).first>0?"teleport":"walk"},{"visitedAreas",navigation.travelVisitedAreas},
        {"ordinaryCost",std::isfinite(ordinaryCost)?Json(ordinaryCost):Json(nullptr)},{"selectedWaypoint",nullptr}};
    if(allowWaypointHere && !(navigation.deferredWaypoint && now<navigation.deferredWaypoint->retryAt)) {
        const bool source=waypointPosition().has_value();
        // 63 reports unlocked destinations only after an actual menu is opened.
        // Being in town does not disclose its waypoint's location. Do not
        // replace the requested exit route with an unrelated room search.
        const bool nearby=std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &v) {
            return v.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(v.unit)==area &&
                distance(c.position,{float(v.position.x),float(v.position.y)})<=8;
        });
        bool mayShorten=false;
        // An activation receipt only confirms that source. It is not the
        // full personal destination list returned by opening the menu (63).
        const bool noMemory=!navigation.waypointMemoryRevision;
        if(source && noMemory) {
            const float approach=navigationCost(c,*area,c.position,waypointPosition());
            for(size_t row=0;row<definitions->levels.rows().size() && !mayShorten;++row) {
                const auto number=definitions->levels.number(row,"Waypoint"),level=definitions->levels.number(row,"Id");
                if(!number || *number<0 || *number>=112 || !level || *level<=0 || *level==*area) continue;
                const auto rest=route(c,*level,destination);
                mayShorten=worthwhileTransfer(approach+16+routeCost(c,rest),ordinaryCost);
            }
        }
        if((nearby || source) && mayShorten && noMemory) {navigation.travelPhase=TravelPhase::WaypointOpen;return false;}
        if(source) navigation.waypointDestination=chooseWaypoint();
    }
    if(allowWaypoint && !c.town.read().town && !navigation.explorationReturn && !questState.questRun.pending &&
        !questState.questRun.worldWait && !questState.questRun.encounterAnchored && !portalIssued &&
        supply.supplyStage==SupplyStage::None && !c.town.read().townPortalSkills.empty()) {
        const auto town=c.observation.read().load.townArea.value_or(0);
        const auto townWaypoint=c.town.knownWaypointPosition(town);
        const bool recovering=path.size()>1 && navigation.failedWalkEdges.contains({*area,path[1]}) &&
            navigation.failedWalkEdges.at({*area,path[1]})>=2;
        if(town && destination!=town && town!=navigation.unavailableWaypointArea && townWaypoint && !anotherAct &&
            (!navigation.travelVisitedAreas.contains(town) || !std::isfinite(ordinaryCost) || recovering)) {
            float bestCost=ordinaryCost;
            if(navigation.waypointDestination) {
                const auto landing=c.town.knownWaypointPosition(*navigation.waypointDestination);
                const auto start=landing?std::optional<Vec>{{float(landing->x),float(landing->y)}}:std::nullopt;
                bestCost=std::min(bestCost,navigationCost(c,*area,c.position,waypointPosition())+16+
                    routeCost(c,route(c,*navigation.waypointDestination,destination,start),start));
            }
            const float portalCost=48+navigationCost(c,town,{},townWaypoint)+16;
            std::optional<uint16_t> relay;
            const float directCost=bestCost;
            for(const auto level:navigation.knownWaypointAreas) {
                if(level==*area || level==town || navigation.travelVisitedAreas.contains(level)) continue;
                const auto landing=c.town.knownWaypointPosition(level);
                const auto start=landing?std::optional<Vec>{{float(landing->x),float(landing->y)}}:std::nullopt;
                const float cost=portalCost+routeCost(c,route(c,level,destination,start),start);
                if(cost<bestCost) {bestCost=cost;relay=level;}
            }
            if(relay && worthwhileTransfer(bestCost,directCost)) {
                navigation.travelDiagnostics["townRelay"]={{"waypoint",*relay},{"cost",bestCost},
                    {"directCost",std::isfinite(directCost)?Json(directCost):Json(nullptr)}};
                note("travel-town-waypoint-shortcut",navigation.travelDiagnostics["townRelay"].dump());
                navigation.explorationReturn=ExplorationReturn{destination,false,now+std::chrono::seconds(120),relay,navigation.travelVisitedAreas};
                cancelWalk(c);portalAttempted=portalIssued=portalUsed=false;oldPortals.clear();
                return false;
            }
        }
    }
    if(navigation.waypointDestination) {navigation.travelPhase=TravelPhase::WaypointOpen;return false;}
    if(path.empty()) {
        bool localWaypoint=false;
        for(size_t row=0;row<definitions->levels.rows().size();++row) if(definitions->levels.number(row,"Id")==area) {
            const auto number=definitions->levels.number(row,"Waypoint");
            localWaypoint=number && *number>=0 && *number<112;break;
        }
        bool possible=!navigation.waypointMemoryRevision;
        if(!possible) for(const auto level:navigation.knownWaypointAreas) if(level!=area &&
            (!route(c,level,destination).empty() || canObserveDestinationAct(level))) {possible=true;break;}
        if(allowWaypointHere && localWaypoint && possible && !waypointPosition()) {
            // An unresolved ordinary graph must not force the caller to give
            // us the waypoint's coordinates. Only this fallback may search for
            // an unknown source; its actual assignment and 63 remain required.
            navigation.travelDiagnostics["result"]="observe-waypoint-source-without-ordinary-route";
            explore(c,"an actual current-level waypoint / travel connection");return false;
        }
        throw std::runtime_error("No observed portal, resolved level route or available unlocked waypoint route; AI does not invent a transfer or unlock");
    }
    navigation.legDestination=uint16_t(path.at(1));
    const auto connection=travelLeg(c,*area,*navigation.legDestination,c.position);
    if(connection.unit) {navigation.legUnit=connection.unit;navigation.travelPhase=TravelPhase::WarpExit;return false;}
    if(approachTownGate(c,*navigation.legDestination) || openDoor(c)) return false;
    const auto edges=c.town.walkingBoundaries();
    const bool walkingEdge=std::any_of(edges.begin(),edges.end(),[&](const auto &edge){return edge.destination==navigation.legDestination;});
    if(!walkingEdge && approachKnown(c,Selector{5,*navigation.legDestination,"known connection to "+std::to_string(*navigation.legDestination)})) return false;
    if(!walkingEdge) {
        auto hints=c.town.automapEntrancePositions(*area);
        std::stable_sort(hints.begin(),hints.end(),[&](OnlinePoint first,OnlinePoint second) {
            const bool retainedFirst=navigation.approachHint==first,retainedSecond=navigation.approachHint==second;
            if(retainedFirst!=retainedSecond) return retainedFirst;
            return distance(c.position,{float(first.x),float(first.y)})<distance(c.position,{float(second.x),float(second.y)});
        });
        for(const auto hint:hints) {
            const auto key=std::tuple{*area,uint8_t{5},*navigation.legDestination,hint.x,hint.y};
            if(navigation.checkedHints.contains(key)) continue;
            if(const auto excluded=navigation.suppressedHints.find({hint.x,hint.y});excluded!=navigation.suppressedHints.end() && now<excluded->second) continue;
            if(distance(c.position,{float(hint.x),float(hint.y)})<=8) {navigation.checkedHints.insert(key);continue;}
            navigation.travelDiagnostics["automapEntranceHint"]={hint.x,hint.y};
            if(approachPoint(c,hint,"natural automap entrance observation",6)) return false;
            navigation.suppressedHints[{hint.x,hint.y}]=now+std::chrono::seconds(30);
            break;
        }
    }
    if(walkingEdge && tryTeleportAcrossExit(c,*navigation.legDestination)) return false;
    if(walkingEdge && approachWalkingConnection(c,*navigation.legDestination)) return false;

    if(frontierWalking(c)) {explore(c,"connection to level "+std::to_string(*navigation.legDestination),false,ExplorationFocus::Connection);return false;}
    if(now>=navigation.exitRetryAt && walkingEdge) {
        navigation.travelPhase=TravelPhase::WalkExit;return false;
    }
    explore(c,"connection to level "+std::to_string(*navigation.legDestination),false,ExplorationFocus::Connection);return false;
}
}
