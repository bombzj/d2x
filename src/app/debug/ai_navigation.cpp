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

}
bool DebugAi::Impl::resumeExplorationReturn(Context &c) {
    if(!explorationReturn) return false;
    if(Clock::now()>=explorationReturn->deadline) throw std::runtime_error("Exploration town/waypoint shortcut was not confirmed");
    auto &trip=*explorationReturn;
    if(!trip.returning) {
        if(!reachTown(c)) return true;
        cancelWalk(c);portalIssued=portalAttempted=portalUsed=false;oldPortals.clear();submittedTravel.reset();
        travelDestination=trip.area;travelRoute.clear();travelVisitedAreas.clear();unavailableWaypointArea.reset();
        waypointDestination=trip.waypoint.value_or(trip.area);travelPhase=TravelPhase::WaypointOpen;legDestination.reset();legUnit.reset();
        trip.returning=true;note(trip.waypoint?"travel-town-waypoint-transfer":"exploration-waypoint-return",
            trip.waypoint?"Continuing the shorter route through the selected unlocked waypoint":"Returning to the unlocked exploration hub");
    }
    if(!travel(c,trip.area)) return true;
    explorationReturn.reset();
    note("exploration-hub-arrived","Continuing remaining observed frontiers");return true;
}
std::string DebugAi::Impl::walkState(const Context &c) const {return c.walk.snapshot().at("state").get<std::string>();}
void DebugAi::Impl::rememberTravel(Context &c,OnlineUnitKey key) {
    const bool retry=passageWaits.contains(Passage::Transfer);
    auto &pending=beginPassage(Passage::Transfer,std::chrono::milliseconds(eventPolicy.initialWaitMs),
        Clock::now()+std::chrono::seconds(eventPolicy.waitSeconds));
    if(retry) pending.retryAt=Clock::now()+passageRetryInterval;
    const auto &view=c.session.read();const auto unit=view.world.units.find(key);
    const auto child=c.walk.snapshot();const auto &submitted=child.at("submittedInteraction");
    if(!area || unit==view.world.units.end() || !submitted.is_object()) return;
    submittedTravel=TravelSubmission{key,unit->second.assignmentRevision,view.gameGeneration,view.world.areaGeneration,
        submitted.at("worldRevision").get<uint64_t>(),submitted.at("discontinuity").get<uint64_t>(),*area};
}
bool DebugAi::Impl::recoverTravel(Context &c) {
    if(!submittedTravel || inputBlocked || actionRecoveryBusy || c.moving) return false;
    const auto &sent=*submittedTravel;const auto &view=c.session.read();
    if(view.stage!=OnlineStage::ProtocolReady || !view.load.serverLoadComplete || view.gameGeneration!=sent.game ||
        view.world.areaGeneration!=sent.world || c.town.read().area!=sent.sourceArea || onlinePlayerDead(view.world)) return false;
    const auto unit=view.world.units.find(sent.unit);
    const bool withdrawn=unit==view.world.units.end() || unit->second.assignmentRevision!=sent.assignment;
    if(!withdrawn && !c.town.permitsInteraction(view,sent.unit)) return false;
    if(!retryPassage(Passage::Transfer,withdrawn?"reacquire entrance assignment":"resume normal entrance approach / interaction")) return false;
    auto &attempt=travelRetries[{sent.sourceArea,sent.unit,sent.assignment}];++attempt;
    note("travel-source-confirmed-recovery",std::to_string(sent.unit.id)+" attempt "+std::to_string(attempt));
    releaseCombat(c);cancelWalk(c);submittedTravel.reset();return true;
}
void DebugAi::Impl::observeChild(Context &c,const Goal &goal) {
    if(!walkIntent.is_object() || walkIntent.value("goal",std::string{})!="unit" || !walkIntent.value("interact",false)) return;
    const auto child=c.walk.snapshot();
    if(!child.value("interactionRequests",0u)) return;
    const OnlineUnitKey unit{uint8_t(walkIntent.at("unitType").get<int>()),walkIntent.at("unitId").get<uint32_t>()};
    const bool waypoint=walkIntent.value("purpose",std::string{})=="activate-waypoint";
    if(child.value("state",std::string{})!="completed") {
        const auto &view=c.session.read();
        const bool questObject=goal.kind==Kind::Quest && supplyStage==SupplyStage::None &&
            walkIntent.value("purpose",std::string{})=="quest-object";
        const auto &submitted=c.control.interactionSubmission();
        if(questObject && c.walk.active() && child.at("gameGeneration")==view.gameGeneration &&
            child.at("areaGeneration")==view.world.areaGeneration && submitted && submitted->target==unit &&
            onlineWorldMatches(submitted->context,view) && submitted->revision>child.at("interactionRevision").get<uint64_t>()) {
            const auto intent=walkIntent;observeQuestInteraction(c,intent);return;
        }
        // AI observes packets before the walk ticks this frame. Consume an
        // arrived menu before a new route or combat input replaces its child.
        if(!waypoint || !c.walk.active() || child.at("gameGeneration")!=view.gameGeneration ||
            child.at("areaGeneration")!=view.world.areaGeneration) return;
        const auto &activation=view.world.waypointActivation;
        const bool activated=!walkIntent.value("confirmMenu",false) && activation && activation->source==unit.id &&
            activation->generation==view.world.interactionGeneration;
        if(!activated && view.world.waypointSource!=unit.id) return;
    }
    const auto now=Clock::now();
    const bool door=walkIntent.value("purpose",std::string{})=="door";
    // Capture accepted interactions before combat/pause can discard a finished
    // child intent. The goal still waits for its separate real postcondition.
    if(door) {
        const auto found=c.session.read().world.units.find(unit);
        if(found!=c.session.read().world.units.end()) pendingDoor=std::pair{unit,found->second.assignmentRevision};
        doorDeadline=beginPassage(Passage::Door,std::chrono::milliseconds(eventPolicy.doorInitialWaitMs),now+std::chrono::seconds(eventPolicy.waitSeconds)).deadline;
        ++interactions;note("door-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if(waypoint) {
        const auto found=c.session.read().world.units.find(unit);
        if(found!=c.session.read().world.units.end())
            pendingWaypoint=WaypointActivation{unit,found->second.assignmentRevision,walkIntent.at("waypointArea").get<uint16_t>(),walkIntent.value("confirmMenu",false)};
        waypointDeadline=now+std::chrono::seconds(12);++interactions;
        note("waypoint-activation-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if(walkIntent.value("purpose",std::string{})=="exploration-pad" && localPad && localPad->unit==unit) {
        const auto &submission=child.at("submittedInteraction");
        if(submission.is_null()) throw std::runtime_error("Local pad has no actual interaction submission observation");
        localPad->from={submission.at("position").at("x").get<uint16_t>(),submission.at("position").at("y").get<uint16_t>()};
        localPad->discontinuity=submission.at("discontinuity").get<uint64_t>();
        localPad->requestRevision=submission.at("worldRevision").get<uint64_t>();
        localPad->deadline=now+std::chrono::seconds(20);localPad->waiting=true;
        ++interactions;note("local-pad-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if(walkIntent.value("purpose",std::string{})=="quest-recover-stash") {
        QuestPending pending;pending.action=QuestPending::Action::RecoverOpen;pending.item=unit.id;
        pending.deadline=now+std::chrono::seconds(12);questRun.pending=pending;cancelWalk(c);
        note("quest-recovery-stash-submitted",std::to_string(unit.id));
    } else if(walkIntent.value("purpose",std::string{})=="supply-heal") {
        healingIssued=true;healingObserved=false;shopDeadline=now+std::chrono::seconds(12);cancelWalk(c);
        note("town-healing-submitted",std::to_string(unit.id));
    } else if(walkIntent.value("purpose",std::string{})=="supply-return" && supplyStage==SupplyStage::Return) {
        rememberTravel(c,unit);
        supplyPortalUsed=true;supplyPortalDeadline=passageWaits.at(Passage::Transfer).deadline;cancelWalk(c);
        note("supply-return-submitted",std::to_string(unit.id));
    } else if(supplyStage==SupplyStage::None && (walkIntent.value("purpose",std::string{})=="quest-object" ||
        walkIntent.value("purpose",std::string{})=="quest-portal" || walkIntent.value("purpose",std::string{})=="quest-orb" || walkIntent.value("purpose",std::string{})=="quest-forge")) {
        const auto intent=walkIntent;observeQuestInteraction(c,intent);
    } else if(supplyStage==SupplyStage::None && goal.kind==Kind::Acquire && goal.sourceObject && target==unit && !objectIssued) {
        objectIssued=true;setActionDeadline(std::chrono::seconds(12));++interactions;
        note("item-source-submitted",goal.item);cancelWalk(c);
    } else if(travelPhase==TravelPhase::WarpExit && legUnit==unit) {
        rememberTravel(c,unit);
        travelPhase=TravelPhase::CrossWait;setActionDeadline(std::chrono::seconds(eventPolicy.waitSeconds));actionDeadline=passageWaits.at(Passage::Transfer).deadline;++interactions;
        note("warp-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    } else if((goal.kind==Kind::ReturnTown || goal.kind==Kind::Restock || supplyStage==SupplyStage::ToTown || (explorationReturn && !explorationReturn->returning)) && portalIssued && !portalUsed) {
        rememberTravel(c,unit);
        portalUsed=true;setActionDeadline(std::chrono::seconds(eventPolicy.waitSeconds));actionDeadline=passageWaits.at(Passage::Transfer).deadline;++interactions;
        note("town-portal-interaction-submitted",std::to_string(unit.id));cancelWalk(c);
    }
}
bool DebugAi::Impl::defendNavigation(Context &c,const Goal &goal) {
    using Owner=DebugActionOwner;
    if(c.moving || c.town.read().town || pendingDoor || pendingWaypoint || questRun.pending || pickup ||
        supplyStage!=SupplyStage::None || Clock::now()>=goalDeadline || (!c.walk.navigationSearch().prolongedWait() && !c.walk.blockedByUnit()) ||
        !canInput(c,Owner::Combat,DebugMotion|DebugSkill)) return false;
    if(!defendedTravel && c.walk.active() && walkIntent.is_object() && !walkIntent.value("interact",false)) {
        const auto kind=walkIntent.value("goal",std::string{});
        const auto purpose=walkIntent.value("purpose",std::string{});
        if(kind=="exit" || (kind=="point" && (walkIntent.value("exploration",false) || purpose.starts_with("known-target")))) {
            defendedTravel=walkIntent;defendedStep=questRun.step;
        }
    }
    if(canInput(c,Owner::Potion,DebugItem) && drink(c)) return true;
    return avoidHazards(c) || maintainArmor(c) || combat(c,goal,true,true);
}
void DebugAi::Impl::resumeDefendedTravel(Context &c) {
    if(!defendedTravel) return;
    auto intent=std::move(*defendedTravel);defendedTravel.reset();
    if(intent.value("goal",std::string{})=="exit" && area && requiredWaypointAreas.contains(*area) && !knownWaypointAreas.contains(*area)) return;
    if(questRun.step!=defendedStep || supplyStage!=SupplyStage::None || questRun.pending || pickup) return;
    // Only ordinary travel may be replayed. Preserve the actual outcome of
    // a child which finished while a stationary defensive cast was in flight.
    if(walkIntent==intent) return;
    if(walkIntent.is_object()) {
        const auto purpose=walkIntent.value("purpose",std::string{});
        if(!purpose.starts_with("combat-") && !retreatPoint && !combatRun.reposition && !combatRun.kitePoint &&
            !(walkIntent.value("goal",std::string{})=="point" && purpose.empty())) return;
    }
    releaseCombat(c);cancelWalk(c);combatTarget.reset();combatRun={};retreatPoint.reset();
    note("travel-resumed-after-defense",intent.value("goal",std::string{}));walk(c,std::move(intent));
}
bool DebugAi::Impl::walk(Context &c,Json intent) {
    intent["command"]="debug-walk";intent["timeoutSeconds"]=120;
    // The native last automatic speech sends 31 then 30. A successful old
    // opening cannot satisfy a new conversation after that interaction ends.
    if(walkIntent==intent && intent.value("goal",std::string{})=="unit" &&
        intent.value("unitType",0)==1 && intent.value("interact",false) && walkState(c)=="completed") {
        const auto &conversation=c.session.read().world.npcConversation;
        if(!conversation || conversation->type!=1 || conversation->source!=intent.at("unitId").get<uint32_t>()) {
            cancelWalk(c);note("npc-conversation-reopen","Previous opening completed but the conversation has ended");
        }
    }
    const bool needsObservation=walkIntent==intent && walkState(c)=="needs-observation";
    std::optional<OnlineUnitKey> observationTarget;
    if(needsObservation) {
        const auto child=c.walk.snapshot();const auto &unit=child.at("unit");
        if(unit.is_object()) observationTarget=OnlineUnitKey{unit.at("type").get<uint8_t>(),unit.at("id").get<uint32_t>()};
    } else if(approachObservation.owner==intent && walkIntent.is_object() &&
        walkIntent.value("purpose",std::string{})=="known-target-memory") observationTarget=approachObservation.unit;
    if(observationTarget) {
        const auto &targets=c.town.read().mapTargets;
        const auto target=std::find_if(targets.begin(),targets.end(),[&](const auto &t){return t.unit==*observationTarget;});
        if(target!=targets.end() && approachPoint(c,target->position,"assigned interaction target",6,false,observationTarget)) {
            approachObservation.owner=intent;return false;
        }
    }
    if(needsObservation) {note("navigation-needs-observation","Refreshing the observed interaction approach");cancelWalk(c);}
    const bool continuing=c.walk.active() && walkIntent==intent;
    bool nearbyInteraction=false;
    if(intent.value("goal",std::string{})=="unit") {
        const OnlineUnitKey key{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
        for(const auto &entry:c.town.read().mapTargets) if(entry.unit==key)
            nearbyInteraction=intent.value("interact",false) &&
                distance(c.position,{float(entry.position.x),float(entry.position.y)})<=6;
        if(intent.value("interact",false) && DebugNavigationSearch::directNpcApproach(c.town,c.session.read(),key,c.position))
            nearbyInteraction=true;
        const auto assigned=std::find_if(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry){return entry.unit==key;});
        if(assigned!=c.town.read().mapTargets.end() && !continuing && intent.value("interact",false)) {
            const auto &view=c.session.read();
            const auto states=c.combat.states().find({0,view.load.playerUnitId.value_or(0)});
            const bool locked=states!=c.combat.states().end() && states->second.decoded &&
                std::any_of(states->second.states.begin(),states->second.states.end(),[](const auto &state) {
                    return state.second.name=="uninterruptable";
                });
            if((c.supplies && c.supplies->castInputBusy.value_or(false)) || locked) {
                mobilityHold=true;wait("interaction-action-settle","Waiting for the normal skill/action lock before a new unit click");return false;
            }
            // A request-range check says nothing about observed terrain.
            // A nearby NPC with a checked execution ray may take over now;
            // other targets still use observed stances or passages.
            if(intent.value("purpose",std::string{})!="door" &&
                approachPoint(c,assigned->position,"assigned interaction target",6,false,key)) {
                approachObservation.owner=intent;return false;
            }

        }
        if(std::none_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){return t.unit==key;})) {
            const auto unit=c.session.read().world.units.find(key);
            if(unit!=c.session.read().world.units.end() && unit->second.position &&
                approachPoint(c,*unit->second.position,"client-observed assigned target")) return false;
            cancelWalk(c);wait("target-reassignment","Waiting for a current interactable client assignment");return false;
        }
    }
    if(intent.value("goal",std::string{})!="point") {
        bool isDoor=intent.value("purpose",std::string{})=="door";
        if(intent.value("goal",std::string{})=="unit") {
            const OnlineUnitKey unit{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
            isDoor=isDoor || std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){
                return t.unit==unit && t.interaction==OnlineMapInteraction::Door;
            });
        }
        // An already adjacent portal/item/NPC must not be postponed in favour
        // of unrelated closed doors, taking us out of its assignment range.
        if(!isDoor && !nearbyInteraction && openDoor(c)) return false;
    }
    if(walkIntent!=intent) {
        cancelWalk(c);walkIntent=intent;
        if(intent.value("goal",std::string{})=="unit") {
            const OnlineUnitKey key{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
            const auto unit=c.session.read().world.units.find(key);
            unsigned retry=0;
            if(unit!=c.session.read().world.units.end()) {
                const auto previous=travelRetries.find({area.value_or(0),key,unit->second.assignmentRevision});
                if(previous!=travelRetries.end()) retry=previous->second;
                const auto door=passageWaits.find(Passage::Door);
                if(door!=passageWaits.end() && (intent.value("purpose",std::string{})=="door" || intent.value("activation",false)))
                    retry=std::max(retry,door->second.attempts);
            }
            if(retry && intent.value("interact",false) && (key.type==1 || key.type==2 || key.type==5)) {
                // A failed opening needs the same approach recovery even
                // when prediction has already reached the target. Otherwise
                // a host stuck behind collision only receives more clicks.
                intent["forceApproach"]=true;intent["resyncApproach"]=retry>=2;
                const auto host=c.session.read().world.playerPosition;
                note("interaction-approach-retry",std::to_string(key.type)+"/"+std::to_string(key.id)+
                    " n="+std::to_string(retry)+" local="+std::to_string(int(c.position.x))+","+std::to_string(int(c.position.y))+
                    (host?" host="+std::to_string(host->x)+","+std::to_string(host->y):" host=unknown"));
            }
        }
        c.walk.command(intent,c.session,c.town,c.control,c.combat);++moveGoals;
        auto description=intent.at("goal").get<std::string>();
        if(description=="unit") description+=" "+std::to_string(intent.at("unitType").get<unsigned>())+"/"+
            std::to_string(intent.at("unitId").get<uint32_t>());
        else if(description=="point") description+=" "+std::to_string(intent.at("x").get<int>())+","+
            std::to_string(intent.at("y").get<int>());
        if(intent.contains("level")) description+=" level="+std::to_string(intent.at("level").get<int>());
        if(intent.contains("purpose")) description+=" "+intent.at("purpose").get<std::string>();
        behaviorLog.event(cursor,"walk-goal",description);
        return false;
    }
    const auto status=c.walk.snapshot();const auto value=status.at("state").get<std::string>();
    if(value=="completed") return true;
    if(value=="failed" && status.value("reason",std::string{})=="Target is no longer assigned in the client scene") {
        note("navigation-target-withdrawn");cancelWalk(c);return false;
    }
    if(value=="cancelled") {
        // The normal client can replace its scene binding during a walk or
        // teleport before the parent next samples the new area. Rebind the
        // child; only a real area observation can satisfy the travel goal.
        note("navigation-rebound",status.value("reason",std::string{}));cancelWalk(c);return false;
    }
    if(value=="failed") {
        const auto purpose=intent.value("purpose",std::string{});
        if(purpose=="initial-waypoint-memory" || purpose=="activate-waypoint") {
            const auto &source=status.at("unit");
            if(source.is_object()) {
                const OnlineUnitKey key{source.at("type").get<uint8_t>(),source.at("id").get<uint32_t>()};
                const auto &world=c.session.read().world;
                const auto unit=world.units.find(key);
                deferredWaypoint=WaypointDeferral{key,unit==world.units.end()?0:unit->second.assignmentRevision,
                    Clock::now()+std::chrono::seconds(30),c.position};
                if(world.waypointRequested==key.id) c.control.useWaypoint(0);
                note("waypoint-approach-deferred",status.value("reason",std::string{}));cancelWalk(c);return false;
            }
        }
        if((intent.value("goal",std::string{})=="waypoint" || purpose=="travel-waypoint") && travelDestination && !pendingWaypoint) {
            note("waypoint-approach-deferred",status.value("reason",std::string{}));
            unavailableWaypointArea=area;waypointDestination.reset();travelPhase=TravelPhase::Route;
            cancelWalk(c);return false;
        }
        if(intent.value("goal",std::string{})=="point" &&
            (intent.value("purpose",std::string{})=="known-target-teleport" || intent.value("directTeleport",false))) {
            recordFrontierAttempt(c,{intent.at("x").get<uint16_t>(),intent.at("y").get<uint16_t>()},
                "failed",status.value("reason",std::string{}));
            note("travel-landing-deferred","The selected movement could not reach this landing");
            cancelWalk(c);return false;
        }
        const auto detail=status.value("reason",std::string{});
        const bool recoverable=detail.starts_with("No observed player progress") ||
            detail=="No integer input target on the collision-checked leg" ||
            detail=="No client collision path to target" ||
            detail=="Path produced no reachable movement segment" ||
            detail=="Server did not open the requested interaction" ||
            detail=="Interaction opening had no result after quick clicks and local approaches";
        if(recoverable && intent.value("goal",std::string{})=="unit" &&
            !questRun.pending && !shopIssued && !potionRequest) {
            const OnlineUnitKey key{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
            const auto &world=c.session.read().world;const auto unit=world.units.find(key);
            // Only an opening or an approach that has not submitted an object
            // operation can be restarted. Never replay a reward/transaction.
            if(unit!=world.units.end() && (key.type==1 || status.at("submittedInteraction").is_null())) {
                auto &attempt=navigationRetries[{area.value_or(0),key,unit->second.assignmentRevision}];
                if(attempt<2) {
                    ++attempt;note("navigation-local-recovery",std::to_string(key.id)+" attempt "+std::to_string(attempt)+": "+detail);
                    if(key.type==1 && world.npcRequested==key.id) c.control.closeNpc();
                    cancelWalk(c);walkIntent=intent;
                    auto retry=intent;retry["forceApproach"]=true;retry["resyncApproach"]=attempt==2;
                    c.walk.command(retry,c.session,c.town,c.control,c.combat);++moveGoals;return false;
                }
            }
        }
        if(intent.value("goal",std::string{})=="unit" && travelPhase==TravelPhase::WarpExit && legUnit && legDestination &&
            status.at("submittedInteraction").is_null()) {
            auto &failures=failedWalkEdges[{area.value_or(0),*legDestination}];failures=std::min(failures+1,16u);
            note("travel-connection-approach-failed",std::to_string(*legDestination)+": "+detail);
            cancelWalk(c);travelRoute.clear();travelPhase=TravelPhase::Route;legDestination.reset();legUnit.reset();
            exitRetryAt=Clock::now()+std::chrono::seconds(5);return false;
        }
        cancelWalk(c);
        throw std::runtime_error("AI navigation: "+detail);
    }
    return false;
}
bool DebugAi::Impl::walkPoint(Context &c,OnlinePoint point,bool exploration) {
    if(c.walk.active() && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point") {
        const int x=walkIntent.value("x",0),y=walkIntent.value("y",0);
        if(walkIntent.value("exploration",false)==exploration && std::abs(x-int(point.x))<=2 && std::abs(y-int(point.y))<=2) return false;
    }
    return walk(c,{{"goal","point"},{"x",point.x},{"y",point.y},{"interact",false},{"exploration",exploration}});
}
std::vector<int> DebugAi::Impl::route(const Context &c,int from,int to) const {
    auto graph=c.town.navigationConnections();
    // Special quest portals (notably Tristram's return red portal) are not
    // Levels.Vis links. Add only actually assigned outgoing connections.
    if(c.town.read().area) for(const auto &target:c.town.read().mapTargets)
        if(ordinaryConnection(target,c.session.read())) graph[*c.town.read().area].push_back(*target.destination);
    std::map<int,int> previous{{from,from}};std::map<int,float> costs{{from,0}};
    using Pending=std::pair<float,int>;
    std::priority_queue<Pending,std::vector<Pending>,std::greater<Pending>> queue;queue.emplace(0,from);
    while(!queue.empty()) {
        const auto [cost,current]=queue.top();queue.pop();
        if(cost>costs.at(current)) continue;
        if(current==to) break;
        const auto edges=graph.find(current);if(edges==graph.end()) continue;
        for(int next:edges->second) {
            const std::optional<Vec> start=current==from && area==from?std::optional<Vec>{c.position}:std::nullopt;
            const float candidate=cost+travelLegCost(c,current,next,start);
            if(!costs.contains(next) || candidate<costs.at(next)) {
                costs[next]=candidate;previous[next]=current;queue.emplace(candidate,next);
            }
        }
    }
    if(!previous.contains(to)) return {};
    std::vector<int> result;for(int p=to;;p=previous.at(p)) {result.push_back(p);if(p==from) break;}
    std::reverse(result.begin(),result.end());return result;
}
float DebugAi::Impl::navigationCost(const Context &c,int level,std::optional<Vec> start,std::optional<OnlinePoint> target) const {
    // A policy cost for an unknown leg, never a generated room-size estimate.
    constexpr float unknownLegCost=80;
    if(!target || !start) return unknownLegCost;
    const Vec wanted{float(target->x),float(target->y)};
    float length=distance(*start,wanted);
    if(c.walk.navigationSearch().explorationMemory.area(uint16_t(level))) {
        const auto cost=c.walk.navigationSearch().estimatedRouteCost(uint16_t(level),*start,wanted);
        if(!cost) return length+unknownLegCost;
        length=*cost;
    }
    return length;
}
float DebugAi::Impl::travelLegCost(const Context &c,int from,int to,std::optional<Vec> start) const {
    const float cost=navigationCost(c,from,start,c.town.knownConnectionPosition(uint16_t(from),uint16_t(to)))+8;
    const auto failed=failedWalkEdges.find({from,to});
    if(failed==failedWalkEdges.end() || failed->second<2) return cost;
    // The route estimate covers only one approach. Repeated failed crossings
    // also cost a return trip. Keep the leg usable when it is the only route.
    return cost+2*std::max(80.f,cost)*failed->second;
}
float DebugAi::Impl::routeCost(const Context &c,const std::vector<int> &path,std::optional<Vec> start) const {
    if(path.empty()) return std::numeric_limits<float>::infinity();
    if(!start && area==path.front()) start=c.position;
    float cost=0;
    for(size_t i=1;i<path.size();++i) {
        cost+=travelLegCost(c,path[i-1],path[i],start);
        const auto entry=c.town.knownConnectionPosition(uint16_t(path[i]),uint16_t(path[i-1]));
        start=entry?std::optional<Vec>{{float(entry->x),float(entry->y)}}:std::nullopt;
    }
    return cost;
}
bool DebugAi::Impl::approachTownGate(Context &c,uint16_t destination) {
    if(!c.town.read().town || !definitions->harrogathGate || area!=definitions->area("Act 5 - Town") ||
        destination!=definitions->area("Act 5 - Siege 1")) return false;
    const Selector gate{2,*definitions->harrogathGate,"Harrogath main gate"};
    const auto &world=c.session.read().world;
    for(const auto &[key,unit]:world.units) {
        if(key.type!=gate.type || unit.classId!=gate.identity || !unit.position || !unit.mode) continue;
        const auto hint=std::tuple{*area,gate.type,gate.identity,unit.position->x,unit.position->y};
        if(*unit.mode!=0) {
            checkedHints.insert(hint);
            if(c.walk.active() && walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="door" &&
                walkIntent.value("unitId",0u)==key.id) cancelWalk(c);
            return false;
        }
        const auto &targets=c.town.read().mapTargets;
        if(std::none_of(targets.begin(),targets.end(),[&](const auto &target) {
            return target.unit==key && target.interaction==OnlineMapInteraction::Door;
        })) break;
        checkedHints.erase(hint);
        const bool outsidePointerRange=std::abs(c.position.x-unit.position->x)>50 || std::abs(c.position.y-unit.position->y)>50;
        if(outsidePointerRange && approachPoint(c,*unit.position,gate.name,6)) return true;
        wait("town-exit-gate","Approaching the assigned closed main gate");
        if(walk(c,{{"goal","unit"},{"unitType",key.type},{"unitId",key.id},{"interact",true},{"purpose","door"}}))
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
    const auto &view=c.session.read();const auto &world=view.world;
    if(waypointMemoryGame!=view.gameGeneration) {
        waypointMemoryGame=view.gameGeneration;waypointMemoryRevision=0;waypointActivationGeneration.reset();
        knownWaypointAreas.clear();waypointTownArrivals.clear();
        observedWaypointMenu.reset();waypointMenuOpenedAt={};
    }
    if(observedWaypointMenu!=world.waypointSource) {
        observedWaypointMenu=world.waypointSource;
        waypointMenuOpenedAt=world.waypointSource?Clock::now():Clock::time_point{};
    }
    // Normal act travel activates the destination town in SUnitNpc / ObjMode.
    // This is AI route knowledge, never a locally fabricated protocol bit.
    // Observe each arrival once so a later full 63 can correct that knowledge.
    if(c.town.read().town && area && waypointTownArrivals.insert(*area).second && !knownWaypointAreas.empty())
        for(size_t row=0;row<definitions->levels.rows().size();++row) {
            const auto id=definitions->levels.number(row,"Id"),number=definitions->levels.number(row,"Waypoint");
            if(id==*area && number && *number>=0 && *number<112) knownWaypointAreas.insert(*area);
        }
    if(world.waypointHistory && waypointMemoryRevision!=world.waypointHistoryRevision) {
        knownWaypointAreas.clear();
        for(size_t row=0;row<definitions->levels.rows().size();++row) {
            const auto id=definitions->levels.number(row,"Id"),number=definitions->levels.number(row,"Waypoint");
            if(id && *id>0 && *id<=UINT16_MAX && number && *number>=0 && *number<112 &&
                ((*world.waypointHistory)[1+*number/16] & (1u<<(*number&15)))) knownWaypointAreas.insert(uint16_t(*id));
        }
        waypointMemoryRevision=world.waypointHistoryRevision;
        note("waypoint-memory-synchronized","Actual personal 63 replaced the prior route knowledge");
    }
    const auto &receipt=world.waypointActivation;
    if(receipt && receipt->generation==world.interactionGeneration && waypointActivationGeneration!=receipt->generation) {
        if(const auto level=c.town.assignedUnitLevel({2,receipt->source})) {
            waypointActivationGeneration=receipt->generation;
            if(knownWaypointAreas.insert(*level).second)
                note("waypoint-unlocked","area="+std::to_string(*level)+" unit="+std::to_string(receipt->source));
        }
    }
    // Remember a point passed during a cast or a quest interaction until the
    // scheduler can act on it. Its assignment must belong to this level.
    if(c.town.read().nativeMapReady && area && !c.town.read().town && !knownWaypointAreas.contains(*area) && nearbyWaypointArea!=area) {
        for(const auto &target:c.town.read().mapTargets) {
            if(target.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(target.unit)==area &&
                ((*area==definitions->area("Act 1 - Wilderness 2")) ||
                 (std::abs(c.position.x-target.position.x)<=50 && std::abs(c.position.y-target.position.y)<=50))) {
                nearbyWaypointArea=area;break;
            }
        }
    }
}
bool DebugAi::Impl::activateWaypoint(Context &c,bool required) {
    const auto &world=c.session.read().world;
    if(!area || (!required && world.npcRequested) || pendingWaypoint || closeWaypoint) return false;
    const auto purpose=walkIntent.is_object()?walkIntent.value("purpose",std::string{}):std::string{};
    const bool initial=purpose=="initial-waypoint-memory";
    if(c.town.read().town && (initial || !waypointMemoryRevision)) {
        if(deferredWaypoint && Clock::now()<deferredWaypoint->retryAt) return false;
        if(world.waypointSource && world.waypointHistory) {
            closeWaypoint=world.waypointSource;cancelWalk(c);return true;
        }
        if(!waypointMemoryRevision) {
            wait("waypoint-memory-initial","Reading personal waypoint history in town");
            walk(c,{{"goal","waypoint"},{"purpose","initial-waypoint-memory"}});return true;
        }
        cancelWalk(c);
    }
    if(knownWaypointAreas.contains(*area)) {
        if(purpose=="activate-waypoint") {
            if(world.waypointSource==walkIntent.at("unitId").get<uint32_t>()) closeWaypoint=world.waypointSource;
            cancelWalk(c);
            return closeWaypoint.has_value();
        }
        return false;
    }
    if(world.waypointSource || (c.town.read().town && !required)) return false;
    if(deferredWaypoint && Clock::now()>=deferredWaypoint->retryAt) deferredWaypoint.reset();
    if(c.walk.active() && purpose=="activate-waypoint") {
        wait("activate-waypoint","Continuing the waypoint approach");
        if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
        return true;
    }
    if(!required && nearbyWaypointArea!=area && !c.town.knownWaypointPosition(*area)) {
        if(observeColdPlainsWaypoint(c)) return true;
        return false;
    }
    const OnlineMapTarget *selected=nullptr;
    float nearest=std::numeric_limits<float>::infinity();
    for(const auto &entry:c.town.read().mapTargets) {
        if(entry.interaction!=OnlineMapInteraction::Waypoint || c.town.assignedUnitLevel(entry.unit)!=area) continue;
        const auto unit=world.units.find(entry.unit);
        if(unit==world.units.end() || !unit->second.mode) continue;
        if(deferredWaypoint && deferredWaypoint->unit==entry.unit && deferredWaypoint->assignment==unit->second.assignmentRevision) continue;
        const float span=distance(c.position,{float(entry.position.x),float(entry.position.y)});
        if(span<nearest) {nearest=span;selected=&entry;}
    }
    if(!selected) {
        const auto seen=c.town.knownWaypointPosition(*area);
        if(deferredWaypoint && required) {
            wait("waypoint-retry","Waiting to retry the unconfirmed waypoint in this area");return true;
        }
        if(!deferredWaypoint && seen && missingWaypointAt!=seen) {
            if(approachPoint(c,*seen,"previously observed waypoint",4)) {
                if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
                return true;
            }
            // Explore for a fresh assignment instead of orbiting a stale hint.
            missingWaypointAt=seen;
        }
        if(!deferredWaypoint && !seen && observeColdPlainsWaypoint(c)) return true;
        return false;
    }
    missingWaypointAt.reset();
    wait("activate-waypoint","Requesting the current area's waypoint");
    const Json intent{{"goal","unit"},{"unitType",selected->unit.type},{"unitId",selected->unit.id},
        {"interact",true},{"purpose","activate-waypoint"},{"waypointArea",*area},{"confirmMenu",world.units.at(selected->unit).mode!=0}};
    if(walk(c,intent)) observeChild(c,goals[cursor]);
    if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
    return true;
}
bool DebugAi::Impl::waypointBeforeDeparture(Context &c) {
    if(!area || !requiredWaypointAreas.contains(*area) || knownWaypointAreas.contains(*area)) return false;
    if(waypointDepartureArea!=area) {
        if(walkIntent.is_object() && walkIntent.value("goal",std::string{})=="exit") cancelWalk(c);
        waypointDepartureArea=area;note("waypoint-required-before-departure",std::to_string(*area));
    }
    if(pendingWaypoint || closeWaypoint) return true;
    if(c.session.read().world.waypointSource) {closeWaypoint=c.session.read().world.waypointSource;return true;}
    if(!activateWaypoint(c,true)) {
        explore(c,"required waypoint before leaving this area",false,ExplorationFocus::Units);
        if(canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
    }
    return true;
}
void DebugAi::Impl::refreshNavigationAttempts(const Context &c) {
    const auto now=Clock::now();const auto revision=c.town.navigationRevision();
    std::erase_if(frontierAttempts,[&](const auto &entry) {
        const auto &attempt=entry.second;
        return now>=attempt.retryAt || (!attempt.retainAfterMove && (attempt.geometryRevision!=revision || distance(c.position,attempt.position)>=8));
    });

}
void DebugAi::Impl::recordFrontierAttempt(const Context &c,OnlinePoint point,std::string outcome,std::string detail,std::optional<OnlinePoint> relayGoal) {
    refreshNavigationAttempts(c);
    // Attempts control short-term replanning only. Neither completion nor
    // failure writes discovery; the rendered viewport is its sole producer.
    if(frontierAttempts.size()>=256 && !frontierAttempts.contains({point.x,point.y}))
        frontierAttempts.erase(std::min_element(frontierAttempts.begin(),frontierAttempts.end(),[](const auto &a,const auto &b) {
            return a.second.retryAt<b.second.retryAt;
        }));
    const bool reached=distance(c.position,{float(point.x),float(point.y)})<=3;
    const bool exploration=walkIntent.is_object() && walkIntent.value("exploration",false);
    if(!reached || exploration || relayGoal==point) relayGoal.reset();
    const bool retain=reached && (exploration || relayGoal.has_value());
    frontierAttempts[{point.x,point.y}]={c.position,c.town.navigationRevision(),Clock::now()+std::chrono::seconds(retain?30:5),
        reached,retain,std::move(outcome),std::move(detail),relayGoal};
    const auto &attempt=frontierAttempts.at({point.x,point.y});
    note("navigation-frontier-attempt",Json{{"target",{point.x,point.y}},
        {"position",{c.position.x,c.position.y}},{"reached",reached},{"retainAfterMove",retain},
        {"retryAfterMs",retain?30000:5000},{"outcome",attempt.outcome},{"reason",attempt.reason},
        {"geometryRevision",attempt.geometryRevision},
        {"relayGoal",relayGoal?Json{relayGoal->x,relayGoal->y}:Json(nullptr)}}.dump());
}
bool DebugAi::Impl::frontierCoolingDown(OnlinePoint point,float radius,std::optional<OnlinePoint> relayGoal) const {
    return std::any_of(frontierAttempts.begin(),frontierAttempts.end(),[&](const auto &entry) {
        return (!entry.second.relayGoal || entry.second.relayGoal==relayGoal) && Clock::now()<entry.second.retryAt && distance({float(point.x),float(point.y)},
            {float(entry.first.first),float(entry.first.second)})<radius;
    });
}
DebugAi::Impl::TerrainMemory *DebugAi::Impl::observeTerrain(const Context &c) {
    auto &search=c.walk.navigationSearch();
    auto *memory=search.observeTerrain(c.town,c.session.read(),c.position);
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
    const auto &grid=c.walk.navigationSearch().planningGrid(c.town,c.session.read());
    const bool teleport=teleportAvailable(c);
    const auto live=c.town.navigationGrid(c.session.read());
    const auto walking=c.walk.navigationSearch().planningCollision(c.town,c.session.read(),{});
    const TeleportRooms neighborhood(c);
    const Vec offset{float(origin->x),float(origin->y)};
    const auto feet=DebugExplorationMemory::playerCell(c.position);
    Vec previous=c.position;bool walkSegment=true;
    for(size_t i=result.nearest;i<path.size() && i<=result.nearest+40;++i) {
        const Vec at{float(path[i].x),float(path[i].y)},local=at-offset+Vec{.5f,.5f};
        // A mixed route can require short walking edges between cast anchors.
        // Rejecting one landing must not hide a later legal cast across it.
        walkSegment=walkSegment && grid.nativeMovementSegment(previous-offset+Vec{.5f,.5f},local,playerMovement);
        const bool cast=teleport && distance(c.position,at)<=mobilityPolicy.distance && neighborhood.contains(at) &&
            c.combat.teleportPathAllowed(*area,live,c.position-offset,at-offset);
        // A continuous walking leg can include an ordinary door operation;
        // only casting needs the destination to be physically clear already.
        const bool walk=walkSegment && walking.walkable(local,playerMovement);
        if((path[i].x!=feet.first || path[i].y!=feet.second) && (walk || (cast && live.walkable(local,playerMovement)))) {
            result.point=path[i];result.teleport=!walk && cast;
        }
        previous=at;
    }
    return result;
}
bool DebugAi::Impl::frontierWalking(const Context &c) const {
    return frontierRoute && c.walk.active() && walkIntent.is_object() &&
        walkIntent.value("purpose",std::string{})=="exploration-frontier";
}
bool DebugAi::Impl::exploreFrontier(Context &c,const std::string &purpose,bool recoveryOnly,ExplorationFocus focus) {
    auto *memory=observeTerrain(c);
    if(!memory || !area) {wait("explore-wait","Waiting for actual terrain observations for "+purpose);return false;}
    auto &m=*memory;
    const auto feet=DebugExplorationMemory::playerCell(c.position);
    const bool owned=walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="exploration-frontier";
    if(frontierRoute && frontierRoute->area!=*area) frontierRoute.reset();
    if(!recoveryOnly && frontierRoute && frontierRoute->focus!=focus) {
        if(owned && c.walk.active()) {wait("explore-frontier","Finishing the submitted relay before changing exploration purpose");return false;}
        m.commitment.reset();frontierRoute.reset();
    }
    if(frontierRoute) {
        const auto *passage=m.passage(frontierRoute->passage);
        const bool stillApproach=passage && std::find(passage->approaches.begin(),passage->approaches.end(),
            DebugExplorationMemory::Cell{frontierRoute->target.x,frontierRoute->target.y})!=passage->approaches.end();
        if(!stillApproach) {
            note("exploration-direction-observed",std::to_string(frontierRoute->passage));
            if(owned) cancelWalk(c);
            frontierRoute.reset();
        } else {
            frontierRoute->evidence=passage->evidence;
            if(owned && c.walk.active()) {wait("explore-frontier",purpose);return false;}
        }
    }
    if(recoveryOnly) return false;
    if(owned && walkIntent.is_object()) {
        const auto outcome=walkState(c);cancelWalk(c);
        if(frontierRoute && outcome=="failed") ++frontierRoute->stalls;
    }
    if(openDoor(c)) return false;
    const auto defer=[&](const char *why) {
        if(!frontierRoute) return;
        DebugExplorationMemory::defer(m,frontierRoute->passage);
        note("exploration-direction-deferred",Json{{"direction",frontierRoute->passage},{"evidence",frontierRoute->evidence},
            {"reason",why},{"target",{frontierRoute->target.x,frontierRoute->target.y}}}.dump());
        frontierRoute.reset();
    };
    // Execution failures are not evidence that an opening has been explored.
    // Keep its identity; the current execution snapshot controls the retry.
    const bool teleport=teleportAvailable(c);
    bool restricted=false;
    if(teleport) for(size_t row=0;row<definitions->levels.rows().size();++row)
        if(definitions->levels.number(row,"Id")==area) restricted=definitions->levels.number(row,"Teleport").value_or(0)==2;
    searchDiagnostics={{"decision",decisions},{"area",*area},{"purpose",purpose},{"strategy","observed-passages"},
        { "from",{c.position.x,c.position.y}},{"terrainRevision",m.revision},{"passages",m.passages.size()},
        {"fromCell",feet},{"sourceMask",m.mask(feet.first,feet.second)},{"sourceStandable",m.stand(feet.first,feet.second)},
        {"unitObservationTiles",m.unitObserved.size()},
        {"unitObservationDirections",std::count_if(m.passages.begin(),m.passages.end(),[](const auto &p){return p.units;})},
        {"mapSketchPrimitives",m.sketch?m.sketch->primitives:0},
        {"deferredDirections",std::count_if(m.passages.begin(),m.passages.end(),[](const auto &p){return p.deferred==p.evidence;})},
        {"selected",nullptr}};
    if(!frontierRoute) {
        const auto direction=experienceDirection(c,focus);
        searchDiagnostics["experienceDirection"]=direction?Json(*direction):Json(nullptr);
        searchDiagnostics["experienceBounds"]=m.expectedBounds?Json(*m.expectedBounds):Json(nullptr);
        auto plan=c.walk.navigationSearch().observedRoute(*area,c.position,teleport?mobilityPolicy.distance:0,restricted,
            {},2,0,false,{},direction);
        searchDiagnostics["expandedStandingCells"]=plan.expanded;
        searchDiagnostics["doorFallback"]=plan.doorFallback;
        if(plan.points.empty()) {
            if(plan.outcome==DebugExplorationMemory::Plan::Outcome::ExecutionBlocked) {
                searchDiagnostics["result"]="execution-blocked";
                wait("explore-blocked","Known passage is temporarily blocked by execution constraints");return false;
            }
            if(m.commitment) {
                const auto pending=*m.commitment;DebugExplorationMemory::defer(m,pending);
                note("exploration-direction-deferred","Retained passage has no observed route");return false;
            }
            if(openDoor(c,true) || exploreLocalPad(c)) return false;
            searchDiagnostics["result"]=plan.outcome==DebugExplorationMemory::Plan::Outcome::StartUnavailable?"start-collision-unavailable":"no-reachable-pending-passage";
            wait(plan.outcome==DebugExplorationMemory::Plan::Outcome::StartUnavailable?"explore-collision-wait":"explore-wait",
                plan.outcome==DebugExplorationMemory::Plan::Outcome::StartUnavailable?
                    "Current feet have no usable acquired collision; waiting for client publication":
                    "No reachable pending passage in acquired terrain for "+purpose);return false;
        }
        frontierRoute=FrontierRoute{*area,plan.points.back(),std::move(plan.points),plan.passage,plan.evidence,~size_t{},0,{},{},focus};
        if(const auto *p=m.passage(plan.passage)) {m.heading=p->direction;m.commitment=p->id;}
        note(plan.backtracking?"exploration-backtrack":"exploration-direction-selected",Json{
            {"direction",plan.passage},{"target",{frontierRoute->target.x,frontierRoute->target.y}},
            {"observation",m.passage(plan.passage)->units?"units":"terrain-probe"},
            {"routeCost",plan.cost},{"pathPoints",frontierRoute->path.size()},{"purpose",purpose}}.dump());
    }
    auto &route=*frontierRoute;
    searchDiagnostics["selected"]={route.target.x,route.target.y};searchDiagnostics["direction"]=route.passage;
    if(feet.first==route.target.x && feet.second==route.target.y) {
        if(!DebugNavigationSearch::passageObservationReady(c.town,c.session.read(),*m.passage(route.passage),
            {float(route.target.x),float(route.target.y)},route.observationSince)) {
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
            {},0,route.passage);
        if(!again.points.empty()) {
            const auto endpoint=again.points.back();
            if(route.target!=endpoint) route.observationSince.reset();
            route.target=endpoint;route.evidence=again.evidence;route.path=std::move(again.points);
            searchDiagnostics["selected"]={route.target.x,route.target.y};
            if(feet.first==route.target.x && feet.second==route.target.y) {
                wait("explore-observation","At the replanned opening; waiting for confirmed observation");return false;
            }
            leg=terrainLeg(c,route.path);
        }
        if(again.outcome==DebugExplorationMemory::Plan::Outcome::ExecutionBlocked) {
            route.blockedSince={};searchDiagnostics["result"]="execution-blocked";
            wait("explore-blocked","Retaining the passage while its live route is blocked");return false;
        }
        if(!leg.point) {
            if(route.blockedSince==Clock::time_point{}) route.blockedSince=Clock::now();
            searchDiagnostics["result"]="no-live-relay";
            wait("explore-blocked","Waiting for a legal live relay for "+purpose);return false;
        }
    }
    route.blockedSince={};
    const size_t remaining=route.path.size()-1-leg.nearest;
    if(remaining+1<route.bestRemaining) {route.bestRemaining=remaining;route.stalls=0;}
    else ++route.stalls;
    searchDiagnostics["remainingPoints"]=remaining;searchDiagnostics["leg"]={leg.point->x,leg.point->y};
    wait("explore-frontier",purpose);
    walk(c,{{"goal","point"},{"x",leg.point->x},{"y",leg.point->y},{"interact",false},{"exploration",true},
        {"purpose","exploration-frontier"},{"exactPoint",true}});
    return false;
}
bool DebugAi::Impl::explore(Context &c,std::string purpose,bool recoveryOnly,ExplorationFocus focus) try {
    if(localPad && !recoveryOnly && exploreLocalPad(c)) return false;
    refreshNavigationAttempts(c);mapCacheBuilds=c.town.navigationCacheBuilds();
    return exploreFrontier(c,purpose,recoveryOnly,focus);
} catch(const DebugSearchPending &) {
    c.walk.navigationSearch().noteTravelWait();throw;
}
bool DebugAi::Impl::approachKnown(Context &c,const Selector &selector) {
    if(!area) return false;
    // A real quest-state change can create/move a town NPC. Absence checked
    // before that change must not permanently suppress its known service site.
    // Repeated snapshots with identical bits are not new evidence.
    const auto &questFlags=c.session.read().world.quests.playerFlags;
    if(checkedHintQuestFlags!=questFlags) {
        checkedHintQuestFlags=questFlags;
        std::erase_if(checkedHints,[](const auto &key){return std::get<1>(key)==1;});
    }
    std::optional<OnlinePoint> hint;
    if(selector.type==1 && (selector.superUnique || selector.eliteOnly)) {
        // A class-level map hint may belong to an ordinary monster or corpse.
        // Only real per-unit identity observations can guide a ranked hunt.
        const auto population=populationMemory.find(*area);
        float nearest=std::numeric_limits<float>::max();
        if(population!=populationMemory.end()) for(const auto &[key,enemy]:population->second.enemies) {
            if(enemy.identity!=selector.identity || (selector.superUnique && enemy.superUnique!=selector.superUnique) ||
                (selector.eliteOnly && !enemy.elite)) continue;
            const auto unit=c.session.read().world.units.find(key);
            if(unit==c.session.read().world.units.end()) {
                const auto &rooms=c.town.navigationRooms();
                const auto room=std::find_if(rooms.begin(),rooms.end(),[&](const auto &r) {
                    return r.level==*area && enemy.position.x>=r.x && enemy.position.y>=r.y &&
                        enemy.position.x<r.x+r.width && enemy.position.y<r.y+r.height;
                });
                if(room!=rooms.end()) {
                    const auto roomKey=std::tuple{int(room->level),room->x,room->y};
                    const auto nativeKey=std::tuple{uint8_t(room->level),uint16_t(room->x/5),uint16_t(room->y/5)};
                    if(c.session.read().world.rooms.contains(nativeKey) && population->second.searchedRooms.contains(roomKey) &&
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
    if(!hint && selector.type==1 && !selector.superUnique && !selector.eliteOnly) {
        hint=c.town.automapNpcPosition(*area,selector.identity);townLandmark=hint.has_value();
    }
    if(!hint && selector.type==2) hint=c.town.automapTargetPosition(*area,selector.identity);
    if(!hint) return approachTerrainClue(c,selector);
    const auto key=std::tuple{*area,selector.type,selector.identity,hint->x,hint->y};
    if(checkedHints.contains(key)) return false;
    if(distance(c.position,{float(hint->x),float(hint->y)})<=8) {
        const auto host=c.session.read().world.playerPosition;
        if(townLandmark || experienceLandmark) {
            if(approachHint!=hint) {approachHint=hint;approachObservation.observationSince.reset();}
            if(c.moving || !host || distance({float(host->x),float(host->y)},c.position)>3) {
                approachObservation.observationSince.reset();
                wait("known-target-assignment","Waiting for actual arrival at the known landmark");return true;
            }
            if(!DebugNavigationSearch::observationReady(c.town,c.session.read(),
                {float(host->x),float(host->y)},approachObservation.observationSince)) {
                wait("known-target-assignment","Waiting for acquired terrain and the landmark assignment");return true;
            }
        }
        checkedHints.insert(key);note("known-target-not-reassigned",selector.name);return false;
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
    const bool arrived=interaction?c.town.interactionReady(c.session.read(),*interaction,c.position):stances?
        std::find(stances->begin(),stances->end(),feet)!=stances->end():
        distance(currentCell,wanted)<=tolerance;
    if(arrived) return false;
    if(interaction && !c.walk.recoveringMovement() &&
        DebugNavigationSearch::directNpcApproach(c.town,c.session.read(),*interaction,c.position)) {
        // Refine a map relay into a real unit click as soon as its approach
        // is executable. Do this before retaining the active point child.
        approachDiagnostics={{"decision",decisions},{"purpose",purpose},{"target",{hint.x,hint.y}},
            {"strategy","direct-npc"},{"result","interaction-ray-confirmed"},{"from",{c.position.x,c.position.y}}};
        return false;
    }
    const auto assigned=interaction?c.session.read().world.units.find(*interaction):c.session.read().world.units.end();
    const uint64_t assignment=assigned==c.session.read().world.units.end()?0:assigned->second.assignmentRevision;
    // A map/history hint becoming an actual nearby assignment refines the
    // same destination; keep its already submitted relay while adopting identity.
    if(interaction && !approachObservation.unit && approachHint &&
        distance({float(approachHint->x),float(approachHint->y)},wanted)<=8) {
        approachObservation.unit=interaction;approachObservation.assignment=assignment;
    }
    const bool sameGoal=approachHint && approachObservation.unit==interaction && approachObservation.assignment==assignment &&
        (interaction || approachHint==hint);
    if(!sameGoal) {approachObservation={};approachObservation.unit=interaction;approachObservation.assignment=assignment;}
    auto *memory=observeTerrain(c);if(!memory || !area) {wait("known-target-observation","Waiting for observed terrain");return true;}
    if(sameGoal && approachFrontier && walkIntent.is_object() && walkIntent.value("purpose",std::string{})=="known-target-memory" &&
        walkIntent.value("x",0)==approachFrontier->x && walkIntent.value("y",0)==approachFrontier->y) {
        // Range alone cannot revoke a relay. Readiness or the checked nearby
        // NPC ray above can; otherwise retain the existing observation leg.
        if(c.walk.active()) {wait("known-target-approach",purpose);return true;}
        const auto state=walkState(c);
        if(state=="needs-replan") {
            approachObservation.relayRecovery=OnlinePoint{uint16_t(feet.first),uint16_t(feet.second)};
            approachObservation.relayTerrain=c.town.navigationSnapshot();
            approachObservation.observationSince.reset();
            note("known-target-relay-replan",Json{{"target",{hint.x,hint.y}},
                {"direction",approachObservation.passage},{"walk",c.walk.snapshot()}}.dump());
        }
        const bool failed=state=="failed";cancelWalk(c);
        if(failed) {
            suppressedHints[{hint.x,hint.y}]=Clock::now()+std::chrono::seconds(3);
            approachObservation.observationSince.reset();
        }
    }
    if(frontierWalking(c)) {explore(c,"retained passage before approaching "+purpose,true);return true;}
    std::erase_if(suppressedHints,[](const auto &p){return Clock::now()>=p.second;});
    if(suppressedHints.contains({hint.x,hint.y})) {wait("known-target-retry","Waiting for blocked route execution");return true;}
    auto &observation=approachObservation;
    if(observation.relayRecovery) {
        const auto anchor=*observation.relayRecovery;
        const auto terrain=c.town.navigationSnapshot();
        if(std::max(std::abs(feet.first-int(anchor.x)),std::abs(feet.second-int(anchor.y)))>8 ||
            !terrain || !observation.relayTerrain ||
            !terrain->sameGlobalRegion(*observation.relayTerrain,int(anchor.x)-8,int(anchor.y)-8,
                int(anchor.x)+8,int(anchor.y)+8)) {
            observation.relayRecovery.reset();observation.relayTerrain.reset();
        }
    }
    if(observation.passage) {
        const auto *p=memory->passage(observation.passage);
        if(!p || p->evidence!=observation.evidence || p->deferred==p->evidence) {
            observation.passage=0;observation.observationSince.reset();
        } else if(feet.first==observation.endpoint.x && feet.second==observation.endpoint.y) {
            if(!DebugNavigationSearch::passageObservationReady(c.town,c.session.read(),*p,
                {float(observation.endpoint.x),float(observation.endpoint.y)},observation.observationSince)) {
                wait("known-target-observation","Waiting for acquired collision toward "+purpose);return true;
            }
            DebugExplorationMemory::defer(*memory,observation.passage);
            observation.passage=0;observation.observationSince.reset();
        }
    }
    bool restricted=false;const bool teleport=teleportAvailable(c);
    if(teleport) for(size_t row=0;row<definitions->levels.rows().size();++row)
        if(definitions->levels.number(row,"Id")==area) restricted=definitions->levels.number(row,"Teleport").value_or(0)==2;
    auto approaches=std::move(stances);
    if(interaction) approaches=c.walk.navigationSearch().interactionApproaches(c.town,c.session.read(),*interaction);
    const auto plan=c.walk.navigationSearch().observedRoute(*area,c.position,teleport?mobilityPolicy.distance:0,restricted,
        wanted,tolerance,observation.passage,true,std::move(approaches));
    const auto leg=terrainLeg(c,plan.points);
    approachDiagnostics={{"decision",decisions},{"purpose",purpose},{"target",{hint.x,hint.y}},
        {"strategy","observed-route"},{"expanded",plan.expanded},{"direction",plan.passage},
        {"result",plan.outcome==DebugExplorationMemory::Plan::Outcome::ExecutionBlocked?"execution-blocked":
            plan.passage?"need-observation":plan.points.empty()?"no-executable-route":leg.point?"executable-route":"no-live-relay"},
        {"pathPoints",plan.points.size()},{"doorFallback",plan.doorFallback},{"from",{c.position.x,c.position.y}},
        {"fromCell",feet},{"sourceMask",memory->mask(feet.first,feet.second)},{"sourceStandable",memory->stand(feet.first,feet.second)},
        {"relay",leg.point?Json{leg.point->x,leg.point->y}:Json(nullptr)},
        {"endpoint",plan.points.empty()?Json(nullptr):Json{plan.points.back().x,plan.points.back().y}},
        {"requiresTeleport",leg.teleport},{"localRelayRecovery",observation.relayRecovery.has_value()}};
    const bool atInteractionStance=!plan.points.empty() && int(currentCell.x)==plan.points.back().x && int(currentCell.y)==plan.points.back().y;
    if(interaction && plan.outcome==DebugExplorationMemory::Plan::Outcome::Goal &&
        (atInteractionStance || (leg.point && *leg.point==plan.points.back())) &&
        std::abs(c.position.x-wanted.x)<=50 && std::abs(c.position.y-wanted.y)<=50) {
        approachDiagnostics["result"]="interaction-route-confirmed";
        note("known-target-interaction-ready",Json{{"target",{hint.x,hint.y}},{"routeCost",plan.cost},
            {"standingPosition",{plan.points.back().x,plan.points.back().y}}}.dump());
        observation.passage=0;observation.observationSince.reset();return false;
    }
    if(plan.passage && !plan.points.empty() && feet.first==plan.points.back().x && feet.second==plan.points.back().y) {
        observation.passage=plan.passage;observation.evidence=plan.evidence;observation.endpoint=plan.points.back();
        observation.observationSince.reset();approachHint=hint;
        wait("known-target-observation","Observing the reached passage toward "+purpose);return true;
    }
    if(!leg.point) {
        if(observation.passage && plan.outcome==DebugExplorationMemory::Plan::Outcome::Unreachable) {
            DebugExplorationMemory::defer(*memory,observation.passage);observation.passage=0;
        }
        wait(plan.points.empty() && plan.outcome!=DebugExplorationMemory::Plan::Outcome::ExecutionBlocked?
            "known-target-observation":"known-target-blocked",
            plan.points.empty()?"No currently executable known route or pending passage toward "+purpose:
                "Known route has no legal live relay toward "+purpose);return true;
    }
    if(plan.passage && observation.passage!=plan.passage)
        note("known-target-direction-selected",Json{{"target",{hint.x,hint.y}},{"direction",plan.passage},
            {"approach",{plan.points.back().x,plan.points.back().y}},{"routeCost",plan.cost}}.dump());
    observation.passage=plan.passage;observation.evidence=plan.evidence;observation.endpoint=plan.points.back();
    observation.observationSince.reset();
    wait(plan.passage?"known-target-observation":"known-target-memory",
        plan.passage?"Exploring the observed passage toward "+purpose:"Following observed terrain toward "+purpose);
    walk(c,{{"goal","point"},{"x",leg.point->x},{"y",leg.point->y},{"interact",false},
        {"purpose","known-target-memory"},{"exploration",exploration || plan.passage!=0},{"exactPoint",true},
        {"recoverableRelay",c.town.read().town && !observation.relayRecovery},
        {"adjustLane",!observation.relayRecovery}});
    approachHint=hint;approachFrontier=leg.point;
    return true;
} catch(const DebugSearchPending &) {
    c.walk.navigationSearch().noteTravelWait();throw;
}
// WalkExit already owns a resolved walking connection. As its rooms load,
// reconsider the actual floor on the far side before probing another frontier.
bool DebugAi::Impl::tryTeleportAcrossExit(Context &c,uint16_t to) {
    const auto *map=c.town.map();const auto origin=c.town.read().origin;
    if(c.town.read().town || !map || !origin) return false;
    const TeleportRooms neighborhood(c);
    if(!teleportAvailable(c)) return false;
    bool allowed=false;
    for(size_t row=0;row<definitions->levels.rows().size();++row) if(definitions->levels.number(row,"Id")==to) {
        allowed=definitions->levels.number(row,"Teleport").value_or(0)!=0;break;
    }
    if(!allowed) return false;
    const auto collision=c.town.navigationGrid(c.session.read());
    const Vec offset{float(origin->x),float(origin->y)};
    const auto threats=travelLandingThreats(c);
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
        if(span<2 || span>mobilityPolicy.distance || !neighborhood.contains(at) ||
            !collision.walkable(at-offset+Vec{.5f,.5f},playerMovement) ||
            frontierCoolingDown(point,3) || crowdedTravelLanding(threats,at) ||
            !c.combat.teleportPathAllowed(*area,collision,c.position-offset,at-offset) ||
            (mobilityTrials.contains(trial) && mobilityTrials.at(trial)>=2)) continue;
        if(span<best) {best=span;landing=point;}
    }
    if(!landing) return false;
    travelDiagnostics["result"]="observed-neighbor-teleport";
    travelDiagnostics["landing"]={landing->x,landing->y};
    wait("travel-teleport","Crossing to actually observed adjacent-level floor with normal Teleport");
    walk(c,{{"goal","point"},{"x",landing->x},{"y",landing->y},{"interact",false},{"purpose","known-target-teleport"}});
    return true;
}
bool DebugAi::Impl::approachWalkingConnection(Context &c,uint16_t destination) {
    observeTerrain(c);
    const auto connection=c.walk.navigationSearch().walkingConnection(c.town,c.position,destination);
    if(!connection.bearing) return false;
    std::vector<DebugExplorationMemory::Cell> stances;
    for(const auto &opening:connection.openings) stances.emplace_back(opening.point.x,opening.point.y);
    travelDiagnostics["connectionOpenings"]=stances.size();
    travelDiagnostics["connectionBearing"]={connection.bearing->x,connection.bearing->y};
    const auto purpose="walking connection to "+std::to_string(destination);
    const bool pending=approachPoint(c,*connection.bearing,purpose,0,false,{},std::move(stances));
    // Preserve normal failed-edge recovery when the shared planner proves no
    // route or pending direction. Temporary execution blocking still waits.
    return pending && !(approachDiagnostics.is_object() && approachDiagnostics.value("decision",uint64_t{})==decisions &&
        approachDiagnostics.value("purpose",std::string{})==purpose &&
        approachDiagnostics.value("result",std::string{})=="no-executable-route");
}
bool DebugAi::Impl::travel(Context &c,uint16_t destination,bool allowWaypoint) {
    if(!area) return false;
    if(*area==destination) {
        cancelWalk(c);travelDestination.reset();travelRoute.clear();travelVisitedAreas.clear();travelPhase=TravelPhase::Route;legDestination.reset();return true;
    }
    const bool awaitingTransfer=legDestination && legDestination!=area &&
        (travelPhase==TravelPhase::WaypointWait || travelPhase==TravelPhase::CrossWait);
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
    if(travelDestination!=destination) {
        unavailableWaypointArea.reset();failedWalkEdges.clear();cancelWalk(c);travelDestination=destination;travelPhase=TravelPhase::Route;legDestination.reset();
        waypointDestination.reset();legUnit.reset();travelRoute.clear();travelVisitedAreas.clear();
    }
    travelVisitedAreas.insert(*area);
    if(legDestination && *area==*legDestination) {
        if(!travelRoute.empty()) failedWalkEdges.erase({travelRoute.front(),*area});
        cancelWalk(c);travelPhase=TravelPhase::Route;legDestination.reset();waypointDestination.reset();legUnit.reset();
    }
    if(!travelRoute.empty() && travelRoute.front()!=*area) {
        const auto reached=std::find(travelRoute.begin(),travelRoute.end(),int(*area));
        if(reached==travelRoute.end()) {
            note("travel-route-deviation","Actual area is outside the retained route; planning from the observed arrival");
            travelRoute.clear();
        } else travelRoute.erase(travelRoute.begin(),reached);
    }
    if(travelRoute.size()>1) {
        const auto &connections=c.town.navigationConnections();
        const auto edge=connections.find(*area);
        const bool assignedPortal=std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &target) {
            return target.destination==travelRoute[1] && ordinaryConnection(target,c.session.read());
        });
        if(!assignedPortal && (edge==connections.end() || std::find(edge->second.begin(),edge->second.end(),travelRoute[1])==edge->second.end())) {
            note("travel-route-invalidated","The next retained connection is absent from the current resolved graph");
            travelRoute.clear();
        }
    }
    if(travelRoute.empty()) {
        travelRoute=route(c,*area,destination);
        if(!travelRoute.empty()) note("travel-route-planned",Json(travelRoute).dump());
    }
    const auto now=Clock::now();
    const auto &targets=c.town.read().mapTargets;
    const auto localWaypoint=std::find_if(targets.begin(),targets.end(),[&](const auto &entry) {
        return entry.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(entry.unit)==area;
    });
    const auto waypointPosition=[&]() -> std::optional<OnlinePoint> {
        if(localWaypoint!=targets.end()) return localWaypoint->position;
        if(area==unavailableWaypointArea) return {};
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
        const auto &current=travelRoute;
        float shortest=routeCost(c,current);std::optional<uint16_t> best,observationWaypoint;
        const bool ordinaryAvailable=std::isfinite(shortest);
        const bool recoveringRoute=current.size()>1 && failedWalkEdges.contains({*area,current[1]}) && failedWalkEdges.at({*area,current[1]})>=2;
        const float approach=navigationCost(c,*area,c.position,source);
        travelDiagnostics={{"from",*area},{"destination",destination},{"ordinaryRoute",current},
            {"ordinaryCost",std::isfinite(shortest)?Json(shortest):Json(nullptr)},
            {"waypointApproachCost",approach},{"waypoints",Json::array()},{"selectedWaypoint",nullptr}};
        for(const auto level:knownWaypointAreas) if(level!=*area) {
            // Do not replace a viable committed route with a waypoint back
            // into its own visited prefix on every new estimate. An absent
            // ordinary route still permits every legal recovery waypoint.
            if(ordinaryAvailable && !recoveringRoute && travelVisitedAreas.contains(level)) {
                travelDiagnostics["waypoints"].push_back({{"level",level},{"result","visited-on-current-journey"}});
                continue;
            }
            const auto rest=route(c,level,destination);
            // A newly joined client may not have materialized the other act's
            // connections yet. An owned waypoint is still a legal transfer:
            // observe that act, then replan from its actual scene. This is a
            // fallback, not an invented onward edge or a same-act retry loop.
            if(!ordinaryAvailable && rest.empty() && !observationWaypoint && canObserveDestinationAct(level))
                observationWaypoint=level;
            const auto landing=c.town.knownWaypointPosition(level);
            const auto start=landing?std::optional<Vec>{{float(landing->x),float(landing->y)}}:std::nullopt;
            const float cost=approach+16+routeCost(c,rest,start);
            travelDiagnostics["waypoints"].push_back({{"level",level},{"route",rest},
                {"cost",std::isfinite(cost)?Json(cost):Json(nullptr)}});
            if(cost<shortest) {shortest=cost;best=level;}
        }
        if(!best && observationWaypoint) {
            best=observationWaypoint;
            travelDiagnostics["result"]="observe-destination-act-via-unlocked-waypoint";
        }
        if(best) travelDiagnostics["selectedWaypoint"]=*best;
        return best;
    };
    if(travelPhase==TravelPhase::WaypointWait || travelPhase==TravelPhase::CrossWait) {
        if(travelPhase==TravelPhase::CrossWait && !legUnit && !c.moving &&
            retryPassage(Passage::Transfer,"resume ordinary walking boundary approach")) {
            // A walking pointer can stop at a newly occupied seam. It has no
            // irreversible service result to replay: choose a fresh ordinary
            // approach from current client collision instead of waiting forever.
            if(legDestination) {
                auto &failures=failedWalkEdges[{*area,*legDestination}];failures=std::min(failures+1,16u);
            }
            cancelWalk(c);travelRoute.clear();travelPhase=TravelPhase::Route;legDestination.reset();exitRetryAt=now+std::chrono::seconds(1);
            note("walking-crossing-stopped","Replanning the ordinary boundary approach from observed occupancy");return false;
        }
        wait("travel-wait","Waiting for the destination's native scene assignment");
        if(now>=actionDeadline) throw std::runtime_error("Travel was submitted but the destination was not observed");
        return false;
    }
    if(travelPhase==TravelPhase::WaypointOpen) {
        const bool open=c.session.read().world.waypointSource && c.session.read().world.waypointHistory &&
            std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &entry) {
                return entry.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(entry.unit)==area &&
                    entry.unit.id==*c.session.read().world.waypointSource && c.session.read().world.units.contains(entry.unit);
            });
        if(!open) {
            if(localWaypoint==targets.end()) {
                if(const auto hint=c.town.knownWaypointPosition(*area);hint && approachPoint(c,*hint,"known waypoint")) return false;
                if(travelRoute.size()>1 && !(explorationReturn && explorationReturn->returning)) {
                    unavailableWaypointArea=area;waypointDestination.reset();travelPhase=TravelPhase::Route;cancelWalk(c);
                    note("waypoint-source-unavailable","Using the ordinary route until the source is assigned");return false;
                }
            }
            wait("waypoint","Opening the actual assigned waypoint");
            if(teleportAvailable(c)) {
                if(localWaypoint==targets.end()) {explore(c,"assignment of the waypoint",false,ExplorationFocus::Units);return false;}
                if(!walk(c,{{"goal","unit"},{"unitType",localWaypoint->unit.type},{"unitId",localWaypoint->unit.id},
                    {"interact",true},{"confirmMenu",true},{"purpose","travel-waypoint"}})) return false;
            } else if(!walk(c,{{"goal","waypoint"},{"purpose","travel-waypoint"}})) return false;
        }
        if(waypointDestination && !knownWaypointAreas.contains(*waypointDestination)) {
            if(explorationReturn && explorationReturn->returning)
                throw std::runtime_error("The actual waypoint menu did not confirm the exploration return destination");
            note("waypoint-memory-corrected","The actual menu disproved the planned unlock; reselecting from current knowledge");
            waypointDestination.reset();
        }
        if(!waypointDestination) waypointDestination=chooseWaypoint();
        if(!waypointDestination) {
            c.control.useWaypoint(0);cancelWalk(c);travelPhase=TravelPhase::Route;return false;
        }
        const auto entry=std::find_if(c.town.read().waypoints.begin(),c.town.read().waypoints.end(),
            [&](const auto &v){return v.level==waypointDestination && v.unlocked;});
        if(entry==c.town.read().waypoints.end()) throw std::runtime_error("Waypoint destination is not unlocked");
        if(c.control.useWaypoint(entry->level,entry->number)) {
            legDestination=entry->level;travelPhase=TravelPhase::WaypointWait;setActionDeadline(std::chrono::seconds(20));cancelWalk(c);
            note("waypoint-request-submitted","from="+std::to_string(*area)+" to="+std::to_string(entry->level));
        }
        return false;
    }
    if(travelPhase==TravelPhase::WalkExit || travelPhase==TravelPhase::WarpExit) {
        const bool warp=travelPhase==TravelPhase::WarpExit;
        if(!warp && approachTownGate(c,*legDestination)) return false;
        if(warp && legUnit && std::none_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &t){return t.unit==legUnit;})) {
            if(c.walk.active() && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="point") {
                wait("known-target-approach","Continuing the observed exit's position approach while its assignment is outside the client scene");
                return false;
            }
            cancelWalk(c);travelPhase=TravelPhase::Route;legUnit.reset();note("exit-assignment-withdrawn");return false;
        }
        if(!warp && tryTeleportAcrossExit(c,*legDestination)) return false;
        if(!warp && !c.walk.active() && approachWalkingConnection(c,*legDestination)) return false;
        if(!warp && walkIntent.is_object() && walkIntent.value("goal",std::string{})=="exit" && walkState(c)=="failed") {
            auto &failures=failedWalkEdges[{*area,*legDestination}];
            failures=std::min(failures+1,16u);
            note("walking-connection-not-yet-reachable",std::to_string(*area)+"->"+std::to_string(*legDestination)+
                " attempt "+std::to_string(failures)+": "+c.walk.snapshot().value("reason",std::string{}));
            exitRetryAt=now+std::chrono::seconds(5);
            cancelWalk(c);travelPhase=TravelPhase::Route;travelRoute.clear();
            if(failures>=2) {
                travelRoute=route(c,*area,destination);
                if(travelRoute.size()>1 && travelRoute[1]!=*legDestination) {
                    legDestination.reset();note("travel-alternate-route",Json(travelRoute).dump());return false;
                }
            }
            explore(c,"unobserved boundary toward level "+std::to_string(*legDestination),false,ExplorationFocus::Connection);return false;
        }
        wait("travel-exit","Approaching the connection to "+std::to_string(*legDestination));
        if(warp && legUnit && legUnit->type==2) {
            const auto &world=c.session.read().world;const auto unit=world.units.find(*legUnit);
            if(unit!=world.units.end() && unit->second.mode==0)
                for(size_t row=0;row<definitions->objects.rows().size();++row)
                    if(definitions->objects.number(row,"Id")==unit->second.classId && definitions->objects.number(row,"OperateFn")==16) {
                        if(pendingDoor) {wait("door-wait","Waiting for the trapdoor to open before entering");return false;}
                        walk(c,{{"goal","unit"},{"unitType",2},{"unitId",legUnit->id},{"interact",true},{"purpose","door"}});
                        return false;
                    }
        }
        Json intent=warp?Json{{"goal","unit"},{"unitType",legUnit->type},{"unitId",legUnit->id},{"interact",true}}:
            Json{{"goal","exit"},{"level",*legDestination},{"interact",false}};
        if(!walk(c,intent)) return false;
        if(warp && legUnit) rememberTravel(c,*legUnit);
        const auto observed=c.walk.snapshot();
        if(!warp) {
            const auto &t=observed.at("crossingTarget");
            if(t.is_null()) throw std::runtime_error("Walking exit has no planned crossing target");
            int x=t.at("x"),y=t.at("y");
            if(x<0 || y<0 || x>65535 || y>65535) throw std::runtime_error("Exit crossing outside native coordinates");
            const auto origin=c.town.read().origin;
            if(origin) {
                const Vec offset{float(origin->x),float(origin->y)};
                const auto grid=c.town.navigationGrid(c.session.read());
                const Vec crossing=Vec{float(x),float(y)}-offset+Vec{.5f,.5f};
                const bool known=grid.movementMask(int(crossing.x),int(crossing.y))!=0xffff;
                if(known && !grid.nativeMovementSegment(c.position-offset+Vec{.5f,.5f},crossing,playerMovement)) {
                    cancelWalk(c);
                    note("walking-crossing-occupied","Selecting another opening after new client unit/terrain updates");return false;
                }
            }
            // The ordinary move is still subject to client-map and server gates.
            const auto owner=DebugActionOwner::Navigation;
            const auto action="walking-crossing:"+std::to_string(*area)+"->"+std::to_string(*legDestination)+
                "@"+std::to_string(x)+","+std::to_string(y);
            const auto result=submitInput(c,owner,action,DebugMotion,
                [&]{return c.control.move({uint16_t(x),uint16_t(y)},true,{},c.position);},[&]{return c.control.reason();});
            if(result==DebugSubmitResult::Waiting) return false;
            if(result==DebugSubmitResult::Expired)
                throw std::runtime_error("Exit crossing input was not accepted: "+submissionWaits.at(owner).reason);
            // The approach already completed. Keep the newly accepted crossing pointer alive.
            walkIntent=nullptr;
        } else cancelWalk(c);
        ++interactions;travelPhase=TravelPhase::CrossWait;setActionDeadline(std::chrono::seconds(eventPolicy.waitSeconds));
        if(warp) actionDeadline=passageWaits.at(Passage::Transfer).deadline;
        else actionDeadline=beginPassage(Passage::Transfer,std::chrono::milliseconds(eventPolicy.initialWaitMs),actionDeadline).deadline;
        return false;
    }
    // Costs for unknown entry points are estimates. Repricing after every
    // boundary can prefer the area just left and create a two-area loop.
    // Retain the chosen sequence until progress, a real deviation, a resolved
    // missing connection, or an explicit approach failure changes the plan.
    const auto &path=travelRoute;
    const float ordinaryCost=routeCost(c,path);
    travelDiagnostics={{"from",*area},{"destination",destination},{"ordinaryRoute",path},
        {"ordinaryCost",std::isfinite(ordinaryCost)?Json(ordinaryCost):Json(nullptr)},{"selectedWaypoint",nullptr}};
    if(allowWaypoint && !(deferredWaypoint && now<deferredWaypoint->retryAt)) {
        const bool source=waypointPosition().has_value();
        // 63 reports unlocked destinations only after an actual menu is opened.
        // Being in town does not disclose its waypoint's location. Do not
        // replace the requested exit route with an unrelated room search.
        const bool nearby=std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &v) {
            return v.interaction==OnlineMapInteraction::Waypoint && c.town.assignedUnitLevel(v.unit)==area &&
                distance(c.position,{float(v.position.x),float(v.position.y)})<=8;
        });
        bool mayShorten=false;
        const bool noMemory=!waypointMemoryRevision && knownWaypointAreas.empty();
        if(source && noMemory) {
            const float approach=navigationCost(c,*area,c.position,waypointPosition());
            for(size_t row=0;row<definitions->levels.rows().size() && !mayShorten;++row) {
                const auto number=definitions->levels.number(row,"Waypoint"),level=definitions->levels.number(row,"Id");
                if(!number || *number<0 || *number>=112 || !level || *level<=0 || *level==*area) continue;
                const auto rest=route(c,*level,destination);
                mayShorten=approach+16+routeCost(c,rest)<ordinaryCost;
            }
        }
        if((nearby || source) && mayShorten && noMemory) {travelPhase=TravelPhase::WaypointOpen;return false;}
        if(source) waypointDestination=chooseWaypoint();
    }
    if(allowWaypoint && !c.town.read().town && !explorationReturn && !questRun.pending &&
        !questRun.worldWait && !questRun.encounterAnchored && !portalIssued &&
        supplyStage==SupplyStage::None && !c.town.read().townPortalSkills.empty()) {
        const auto town=c.session.read().load.townArea.value_or(0);
        const auto townWaypoint=c.town.knownWaypointPosition(town);
        if(town && townWaypoint && !anotherAct) {
            float bestCost=ordinaryCost;
            if(waypointDestination) {
                const auto landing=c.town.knownWaypointPosition(*waypointDestination);
                const auto start=landing?std::optional<Vec>{{float(landing->x),float(landing->y)}}:std::nullopt;
                bestCost=std::min(bestCost,navigationCost(c,*area,c.position,waypointPosition())+16+
                    routeCost(c,route(c,*waypointDestination,destination),start));
            }
            const float portalCost=48+navigationCost(c,town,{},townWaypoint)+16;
            std::optional<uint16_t> relay;
            const float directCost=bestCost;
            for(const auto level:knownWaypointAreas) {
                if(level==*area || level==town || travelVisitedAreas.contains(level)) continue;
                const auto landing=c.town.knownWaypointPosition(level);
                const auto start=landing?std::optional<Vec>{{float(landing->x),float(landing->y)}}:std::nullopt;
                const float cost=portalCost+routeCost(c,route(c,level,destination),start);
                if(cost<bestCost) {bestCost=cost;relay=level;}
            }
            if(relay) {
                travelDiagnostics["townRelay"]={{"waypoint",*relay},{"cost",bestCost},
                    {"directCost",std::isfinite(directCost)?Json(directCost):Json(nullptr)}};
                note("travel-town-waypoint-shortcut",travelDiagnostics["townRelay"].dump());
                explorationReturn=ExplorationReturn{destination,false,now+std::chrono::seconds(120),relay};
                cancelWalk(c);portalAttempted=portalIssued=portalUsed=false;oldPortals.clear();
                return false;
            }
        }
    }
    if(waypointDestination) {travelPhase=TravelPhase::WaypointOpen;return false;}
    if(path.empty()) {
        bool localWaypoint=false;
        for(size_t row=0;row<definitions->levels.rows().size();++row) if(definitions->levels.number(row,"Id")==area) {
            const auto number=definitions->levels.number(row,"Waypoint");
            localWaypoint=number && *number>=0 && *number<112;break;
        }
        bool possible=!waypointMemoryRevision && knownWaypointAreas.empty();
        if(!possible) for(const auto level:knownWaypointAreas) if(level!=area &&
            (!route(c,level,destination).empty() || canObserveDestinationAct(level))) {possible=true;break;}
        if(allowWaypoint && localWaypoint && possible && !waypointPosition()) {
            // An unresolved ordinary graph must not force the caller to give
            // us the waypoint's coordinates. Only this fallback may search for
            // an unknown source; its actual assignment and 63 remain required.
            travelDiagnostics["result"]="observe-waypoint-source-without-ordinary-route";
            explore(c,"an actual current-level waypoint / travel connection");return false;
        }
        throw std::runtime_error("No observed portal, resolved level route or available unlocked waypoint route; AI does not invent a transfer or unlock");
    }
    legDestination=uint16_t(path.at(1));
    if(approachTownGate(c,*legDestination) || openDoor(c)) return false;
    for(const auto &entry:c.town.read().mapTargets) if(entry.destination==legDestination && ordinaryConnection(entry,c.session.read())) {
        legUnit=entry.unit;travelPhase=TravelPhase::WarpExit;return false;
    }
    if(approachKnown(c,Selector{5,*legDestination,"known connection to "+std::to_string(*legDestination)})) return false;
    const auto edges=c.town.walkingBoundaries();
    const bool walkingEdge=std::any_of(edges.begin(),edges.end(),[&](const auto &edge){return edge.destination==legDestination;});
    if(!walkingEdge) {
        auto hints=c.town.automapEntrancePositions(*area);
        std::stable_sort(hints.begin(),hints.end(),[&](OnlinePoint first,OnlinePoint second) {
            const bool retainedFirst=approachHint==first,retainedSecond=approachHint==second;
            if(retainedFirst!=retainedSecond) return retainedFirst;
            return distance(c.position,{float(first.x),float(first.y)})<distance(c.position,{float(second.x),float(second.y)});
        });
        for(const auto hint:hints) {
            const auto key=std::tuple{*area,uint8_t{5},*legDestination,hint.x,hint.y};
            if(checkedHints.contains(key)) continue;
            if(const auto excluded=suppressedHints.find({hint.x,hint.y});excluded!=suppressedHints.end() && now<excluded->second) continue;
            if(distance(c.position,{float(hint.x),float(hint.y)})<=8) {checkedHints.insert(key);continue;}
            travelDiagnostics["automapEntranceHint"]={hint.x,hint.y};
            if(approachPoint(c,hint,"natural automap entrance observation",6)) return false;
            suppressedHints[{hint.x,hint.y}]=now+std::chrono::seconds(30);
            break;
        }
    }
    if(walkingEdge && tryTeleportAcrossExit(c,*legDestination)) return false;
    if(walkingEdge && approachWalkingConnection(c,*legDestination)) return false;

    if(frontierWalking(c)) {explore(c,"connection to level "+std::to_string(*legDestination),false,ExplorationFocus::Connection);return false;}
    if(now>=exitRetryAt && walkingEdge) {
        travelPhase=TravelPhase::WalkExit;return false;
    }
    explore(c,"connection to level "+std::to_string(*legDestination),false,ExplorationFocus::Connection);return false;
}
}
