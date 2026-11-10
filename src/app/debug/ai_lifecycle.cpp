#include "ai_impl.hpp"

namespace d2x::debug_ai {
void NavigationState::resetStep() {
    explorationReturn.reset();routeExecution.reset();localPad.reset();frontierRoute.reset();
    searchDiagnostics=nullptr;approachDiagnostics=nullptr;mobilityDiagnostics=nullptr;travelDiagnostics=nullptr;
    terrainClueDiagnostics=nullptr;vaughtSearch.aim.reset();frontierAttempts.clear();checkedHints.clear();
    walkIntent={};approachHint.reset();approachFrontier.reset();approachObservation={};
    operatedDoors.clear();pendingDoor.reset();doorDeadline={};pendingWaypoint.reset();closeWaypoint.reset();waypointDeadline={};
    travelDestination.reset();travelRoute.clear();travelVisitedAreas.clear();travelPhase=TravelPhase::Route;
    legDestination.reset();legUnit.reset();waypointDestination.reset();exitRetryAt={};npcOpenRetries=0;
    npcOpeningRecovery=false;navigationRetries.clear();submittedTravel.reset();travelRetries.clear();passageWaits.clear();
    mobilityTrials.clear();mobilityGoal.reset();mobilityRoute.clear();mobilityRouteProgress=mobilityPendingProgress=mobilityRouteLength=0;
}
void NavigationState::suspend(Clock::duration elapsed) {
    for(auto &[kind,pending]:passageWaits) {(void)kind;pending.retryAt+=elapsed;pending.deadline+=elapsed;}
    if(routeExecution) routeExecution->deadline+=elapsed;
    if(localPad && !localPad->waiting) localPad->deadline+=elapsed;
}
void CombatState::resetStep() {
    combatRun={};combatDiagnostics=nullptr;hazardDodge.reset();hazardDiagnostics=nullptr;
}
void CombatState::suspend(Clock::duration elapsed) {
    if(!combatTarget) return;
    combatRun.progressAt+=elapsed;combatRun.strategyAt+=elapsed;
    if(combatRun.delayObservedAt!=Clock::time_point{}) combatRun.delayObservedAt+=elapsed;
    if(combatRun.suspendedAt!=Clock::time_point{}) combatRun.suspendedAt+=elapsed;
    if(combatRun.approachUntil!=Clock::time_point{}) combatRun.approachUntil+=elapsed;
    if(combatRun.kiteUntil!=Clock::time_point{}) combatRun.kiteUntil+=elapsed;
    if(combatRun.nextKite!=Clock::time_point{}) combatRun.nextKite+=elapsed;
}
void SupplyState::resetPlan() {
    supplyStage=SupplyStage::None;supplyOrigin.reset();supplyPortalUsed=false;
    potionRequest.reset();nextPotion={};shelfReopens=0;shopIssued=false;shelfObserved=false;
    healingIssued=false;healingObserved=false;supplyNpc.reset();shopDeadline={};drinks=purchases=0;
    lastPotion=nullptr;supplyVitals=nullptr;quiverTransfer.reset();quiverTopUpDeferred=false;
}
void SupplyState::suspend(Clock::duration elapsed) {
    if(shelfObserved) shopDeadline+=elapsed;
}
void QuestState::resetPlan() {
    questRun={};questPlanDiagnostics=nullptr;tombSymbol.reset();
}
void QuestState::suspend(Clock::duration elapsed,bool pendingRequiresInput) {
    if(questRun.pending && pendingRequiresInput) questRun.pending->deadline+=elapsed;
    if(questRun.portalDeadline!=Clock::time_point{}) questRun.portalDeadline+=elapsed;
    // Preserve both existing pending-operation and portal wait adjustments.
    if(questRun.pending && questRun.pending->action==QuestPending::Action::Portal) questRun.pending->deadline+=elapsed;
    if(questRun.worldWait) questRun.worldWait->deadline+=elapsed;
    if(questRun.witnessUntil!=Clock::time_point{}) questRun.witnessUntil+=elapsed;
    if(questRun.missingTopicSince!=Clock::time_point{}) questRun.missingTopicSince+=elapsed;
}
} // namespace d2x::debug_ai

namespace d2x {
void DebugAi::Impl::resetStep() {
    behaviorLog.endPhase();phaseReported=false;
    navigation.resetStep();submissionWaits.clear();battle.resetStep();continuation=Continuation::None;
    phase.clear();reason.clear();target.reset();targetAssignment=0;
    objectIssued=portalIssued=portalAttempted=portalUsed=greetingIssued=false;
    pickup.reset();pickupApproaches.clear();oldPortals.clear();actionDeadline={};actionWaitSubmitted=false;
}
void DebugAi::Impl::suspendExecution(Context &c,Clock::duration suspended) {
    for(auto &[owner,pending]:submissionWaits) {
        (void)owner;pending.deadline+=suspended;pending.retryAt+=suspended;
    }
    if(supply.supplyStage==SupplyStage::None) goalDeadline+=suspended;
    else supply.supplyDeadline+=suspended;
    questState.suspend(suspended,questState.questRun.pending && questWaitRequiresInput(c));
    if(!actionWaitSubmitted && actionDeadline!=Clock::time_point{}) actionDeadline+=suspended;
    navigation.suspend(suspended);
    if(const auto found=navigation.passageWaits.find(Passage::Door);found!=navigation.passageWaits.end()) {
        if(navigation.pendingDoor) navigation.doorDeadline=found->second.deadline;
        if(questState.questRun.pending && cursor<goals.size() && questState.questRun.step<goals[cursor].steps.size() &&
            goals[cursor].steps[questState.questRun.step].action==QuestAction::Portal &&
            questState.questRun.pending->action==QuestPending::Action::Object) questState.questRun.pending->deadline=found->second.deadline;
    }
    if(const auto found=navigation.passageWaits.find(Passage::Transfer);found!=navigation.passageWaits.end()) {
        if(navigation.travelPhase==TravelPhase::CrossWait || portalUsed) actionDeadline=found->second.deadline;
        if(supply.supplyPortalUsed) supply.supplyPortalDeadline=found->second.deadline;
    }
    if(portalIssued && !portalUsed) if(const auto found=navigation.passageWaits.find(Passage::CreatePortal);found!=navigation.passageWaits.end())
        actionDeadline=found->second.deadline;
    battle.suspend(suspended);
    supply.suspend(suspended);
}
void DebugAi::Impl::observeArea(Context &c,Clock::time_point now) {
    auto &town=c.town;const auto &view=c.session.read();
    if(area!=town.read().area || areaGeneration!=view.world.areaGeneration) {
        const bool leavingTown=area && area==view.load.townArea && town.read().area && town.read().area!=view.load.townArea;
        if(town.read().town) battle.excursionSkillPrepared=false;
        supply.quiverTopUpDeferred=false;
        navigation.townExitArea.reset();
        navigation.deferredWaypoint.reset();
        questState.summitTrialKills.reset();navigation.waypointDepartureArea.reset();navigation.nearbyWaypointArea.reset();navigation.missingWaypointAt.reset();
        battle.hazardDodge.reset();battle.hazardDiagnostics=nullptr;navigation.passageWaits.clear();
        navigation.routeExecution.reset();
        navigation.frontierRoute.reset();
        cancelWalk(c);area=town.read().area;areaGeneration=view.world.areaGeneration;navigation.frontierAttempts.clear();
        if(leavingTown && teleportAvailable(c,false)) {
            navigation.townExitArea=area;navigation.townExitObserveUntil=now+std::chrono::milliseconds(1500);
        }
        navigation.checkedHints.clear();navigation.approachHint.reset();navigation.approachFrontier.reset();
        pickupApproaches.clear();
        continuation=Continuation::None;
        // Ordinary movement may cross back over a border. Its old exit
        // belongs to the previous area; recompute from the observed one.
        // Accepted warp/waypoint operations retain their result wait.
        if(navigation.travelPhase!=TravelPhase::WaypointWait &&
            !(navigation.travelPhase==TravelPhase::CrossWait && navigation.legUnit)) {
            navigation.travelPhase=TravelPhase::Route;navigation.legDestination.reset();navigation.legUnit.reset();navigation.waypointDestination.reset();
        }
        battle.combatTarget.reset();battle.combatOwned=false;battle.retreatPoint.reset();note("area-observed",area?std::to_string(*area):"loading");
        battle.combatRun={};
        navigation.mobilityTarget.reset();navigation.mobilityHold=false;
        navigation.mobilityGoal.reset();navigation.mobilityRoute.clear();
        navigation.mobilityRouteProgress=navigation.mobilityPendingProgress=navigation.mobilityRouteLength=0;
    }
}
} // namespace d2x
